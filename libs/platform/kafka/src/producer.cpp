#include "psim/platform/kafka/producer.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <librdkafka/rdkafka.h>
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "detail.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/async/detail/wait_list.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/metrics.hpp"
#include "psim/platform/log.hpp"

namespace psim::platform::kafka {

namespace {

using Clock = std::chrono::steady_clock;

// Delivery of one send(): shared by the waiting coroutine and the delivery report, either of
// which may finish first.
struct Pending {
  std::optional<Result<Delivery>> result;
  asio::steady_timer* event = nullptr;

  void complete(Result<Delivery> outcome) {
    result = std::move(outcome);
    if (event != nullptr) {
      event->cancel();
    }
  }
};

struct TopicSeries {
  prometheus::Counter* messages = nullptr;
  prometheus::Counter* bytes = nullptr;
  prometheus::Counter* errors = nullptr;
};

bool purged(rd_kafka_resp_err_t code) {
  return code == RD_KAFKA_RESP_ERR__PURGE_QUEUE || code == RD_KAFKA_RESP_ERR__PURGE_INFLIGHT;
}

}  // namespace

struct Producer::State {
  State() = default;
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;

  ProducerOptions options;
  Dependencies deps;
  detail::KafkaPtr handle;
  detail::QueuePtr queue;
  std::unique_ptr<detail::QueueEvents> events;
  async::detail::WaitList space;  // coroutines waiting for room in the local queue
  std::optional<Error> delivery_error;
  std::map<std::string, TopicSeries, std::less<>> series;
  bool transaction = false;
  bool fatal = false;
  // A blocking transactional call runs in the pool and serves delivery reports there; the pump
  // stays away from the handle meanwhile.
  bool blocking_call = false;

  ~State() {
    if (events) {
      events->close();
    }
    if (handle) {
      // Undelivered messages: their reports free the pending sends.
      rd_kafka_purge(handle.get(), RD_KAFKA_PURGE_F_QUEUE | RD_KAFKA_PURGE_F_INFLIGHT);
      rd_kafka_poll(handle.get(), 0);
    }
    events.reset();
    queue.reset();
  }

  TopicSeries& topic_series(std::string_view topic) {
    if (const auto it = series.find(topic); it != series.end()) {
      return it->second;
    }
    TopicSeries entry;
    if (deps.metrics != nullptr) {
      entry = {.messages = &deps.metrics->produced_messages(topic),
               .bytes = &deps.metrics->produced_bytes(topic),
               .errors = &deps.metrics->delivery_errors(topic)};
    }
    return series.emplace(std::string(topic), entry).first->second;
  }

  void on_delivery(const rd_kafka_message_t* message) {
    const std::unique_ptr<std::shared_ptr<Pending>> pending(static_cast<std::shared_ptr<Pending>*>(message->_private));
    const std::string_view topic = rd_kafka_topic_name(message->rkt);
    auto& counters = topic_series(topic);
    if (message->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
      if (counters.errors != nullptr) {
        counters.errors->Increment();
      }
      auto error = detail::to_error(message->err, "deliver to " + std::string(topic));
      if (pending) {
        (*pending)->complete(std::unexpected(std::move(error)));
      } else if (!delivery_error && !purged(message->err)) {
        delivery_error = std::move(error);  // purges come from an abort, which reports itself
      }
      return;
    }
    if (counters.messages != nullptr) {
      counters.messages->Increment();
      counters.bytes->Increment(static_cast<double>(message->len));
    }
    if (pending) {
      (*pending)->complete(Delivery{.partition = message->partition, .offset = message->offset});
    }
  }

  void on_error(int code, const char* reason) {
    const auto err = static_cast<rd_kafka_resp_err_t>(code);
    if (err == RD_KAFKA_RESP_ERR__FATAL) {
      fatal = true;
      std::array<char, 512> text{};
      const auto original = rd_kafka_fatal_error(handle.get(), text.data(), text.size());
      if (deps.logger != nullptr) {
        deps.logger->error("kafka producer failed",
                           {{"error", rd_kafka_err2name(original)}, {"reason", std::string_view(text.data())}});
      }
      return;
    }
    if (deps.logger != nullptr) {
      deps.logger->warn("kafka producer error", {{"error", rd_kafka_err2name(err)}, {"reason", reason}});
    }
  }

  /// Serve delivery reports and errors waiting in the queue, then wake senders waiting for room.
  void serve() {
    if (blocking_call) {
      return;
    }
    while (rd_kafka_poll(handle.get(), 0) > 0) {
    }
    space.notify_all();
  }

  static asio::awaitable<void> pump(std::shared_ptr<State> self) {
    while (!self->events->closed()) {
      self->serve();
      if (!co_await self->events->wait(detail::kServeInterval)) {
        break;
      }
    }
  }

  /// Queue a message; `queue_full` tells a full local queue (wait and retry) from other errors.
  // The record is not const: librdkafka takes mutable pointers, although it copies (RD_KAFKA_MSG_F_COPY).
  Result<void> produce(Record& record, std::shared_ptr<Pending>* pending, bool* queue_full) const {
    rd_kafka_headers_t* headers = nullptr;
    if (!record.headers.empty()) {
      headers = rd_kafka_headers_new(record.headers.size());
      for (const auto& [name, value] : record.headers) {
        rd_kafka_header_add(headers, name.data(), static_cast<ssize_t>(name.size()), value.data(),
                            static_cast<ssize_t>(value.size()));
      }
    }
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): rd_kafka_vu_t is the tagged union of the C API
    std::array<rd_kafka_vu_t, 7> args{};
    std::size_t count = 0;
    args.at(count).vtype = RD_KAFKA_VTYPE_TOPIC;
    args.at(count++).u.cstr = record.topic.c_str();
    args.at(count).vtype = RD_KAFKA_VTYPE_VALUE;
    args.at(count).u.mem.ptr = record.value.data();
    args.at(count++).u.mem.size = record.value.size();
    args.at(count).vtype = RD_KAFKA_VTYPE_KEY;
    args.at(count).u.mem.ptr = record.key.data();
    args.at(count++).u.mem.size = record.key.size();
    args.at(count).vtype = RD_KAFKA_VTYPE_MSGFLAGS;
    args.at(count++).u.i = RD_KAFKA_MSG_F_COPY;
    args.at(count).vtype = RD_KAFKA_VTYPE_OPAQUE;
    args.at(count++).u.ptr = pending;
    if (record.partition) {
      args.at(count).vtype = RD_KAFKA_VTYPE_PARTITION;
      args.at(count++).u.i32 = *record.partition;
    }
    if (headers != nullptr) {
      args.at(count).vtype = RD_KAFKA_VTYPE_HEADERS;
      args.at(count++).u.headers = headers;
    }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    const detail::ErrorPtr error(rd_kafka_produceva(handle.get(), args.data(), count));
    if (!error) {
      return {};  // librdkafka owns the headers now
    }
    if (headers != nullptr) {
      rd_kafka_headers_destroy(headers);
    }
    *queue_full = rd_kafka_error_code(error.get()) == RD_KAFKA_RESP_ERR__QUEUE_FULL;
    return std::unexpected(detail::to_error(error.get(), "produce to " + record.topic));
  }

  /// Produce, waiting for room while the local queue is full.
  asio::awaitable<Result<void>> produce_waiting(Record* record, std::shared_ptr<Pending>* pending) {
    while (true) {
      if (fatal) {
        co_return fail(ErrorCode::kCommonInternal, "kafka producer is in a fatal state");
      }
      bool queue_full = false;
      auto result = produce(*record, pending, &queue_full);
      if (result || !queue_full) {
        co_return result;
      }
      serve();
      if (!co_await space.wait()) {
        co_return fail(ErrorCode::kCommonUnavailable, "produce to " + record->topic + ": cancelled");
      }
    }
  }

  Result<void> take_delivery_error() {
    if (delivery_error) {
      auto error = std::move(*delivery_error);
      delivery_error.reset();
      return std::unexpected(std::move(error));
    }
    return {};
  }

  Result<void> transactional(std::string_view what, bool pool) const {
    if (options.transactional_id.empty()) {
      return fail(ErrorCode::kCommonInvalidArgument, std::string(what) + ": the producer is not transactional");
    }
    if (pool && deps.blocking == nullptr) {
      return fail(ErrorCode::kCommonInvalidArgument, std::string(what) + ": no blocking pool");
    }
    return {};
  }

  Result<void> check(rd_kafka_error_t* raw, std::string_view what) {
    const detail::ErrorPtr error(raw);
    if (!error) {
      return {};
    }
    if (rd_kafka_error_is_fatal(error.get()) != 0) {
      fatal = true;
    }
    return std::unexpected(detail::to_error(error.get(), what));
  }

  // Blocking transactional calls; they run in the pool or inside a rebalance callback.
  Result<void> commit_blocking(rd_kafka_consumer_group_metadata_t* group, const Offsets& offsets, int timeout_ms) {
    if (group != nullptr && !offsets.empty()) {
      const auto list = detail::to_list(offsets);
      if (auto sent = check(rd_kafka_send_offsets_to_transaction(handle.get(), list.get(), group, timeout_ms),
                            "send offsets to the transaction");
          !sent) {
        return sent;
      }
    }
    // A retriable failure leaves the transaction open: commit again (librdkafka documentation).
    for (int attempt = 0;; ++attempt) {
      const detail::ErrorPtr error(rd_kafka_commit_transaction(handle.get(), timeout_ms));
      if (!error) {
        return {};
      }
      if (rd_kafka_error_is_retriable(error.get()) == 0 || attempt == 2) {
        if (rd_kafka_error_is_fatal(error.get()) != 0) {
          fatal = true;
        }
        return std::unexpected(detail::to_error(error.get(), "commit the transaction"));
      }
    }
  }

  Result<void> abort_blocking(int timeout_ms) {
    return check(rd_kafka_abort_transaction(handle.get(), timeout_ms), "abort the transaction");
  }

  /// Run a blocking transactional call in the pool with the pump paused, then serve its reports.
  template <typename Call>
  asio::awaitable<Result<void>> in_pool(Call call) {
    blocking_call = true;
    auto result = co_await deps.blocking->run(std::move(call));
    blocking_call = false;
    serve();
    co_return result;
  }
};

Producer::Producer(std::shared_ptr<State> state) : state_(std::move(state)) {}

Producer::~Producer() {
  if (state_ && state_->events) {
    state_->events->close();
  }
}

Result<std::unique_ptr<Producer>> Producer::create(const asio::any_io_executor& executor, ProducerOptions options,
                                                   Dependencies deps) {
  // Idempotence gives acks=all, ordered retries without duplicates (ADR-041); zstd as the topics.
  detail::Properties defaults{
      {"compression.type", "zstd"},
      {"linger.ms", "5"},
      {"batch.size", "1000000"},
      {"queue.buffering.max.messages", "200000"},
      {"delivery.timeout.ms", std::to_string(options.delivery_timeout.count())},
  };
  detail::Properties fixed{{"enable.idempotence", "true"}, {"acks", "all"}};
  if (!options.transactional_id.empty()) {
    fixed.emplace("transactional.id", options.transactional_id);
    // librdkafka requires delivery.timeout.ms <= transaction.timeout.ms (at least 1 s).
    constexpr std::chrono::milliseconds kTransactionTimeout{60000};
    defaults.emplace("transaction.timeout.ms",
                     std::to_string(std::max(options.delivery_timeout, kTransactionTimeout).count()));
  }
  auto conf = detail::make_conf(options.client, defaults, fixed);
  if (!conf) {
    return std::unexpected(std::move(conf.error()));
  }
  auto state = std::make_shared<State>();
  state->options = std::move(options);
  state->deps = deps;
  rd_kafka_conf_set_opaque(conf->get(), state.get());
  rd_kafka_conf_set_dr_msg_cb(conf->get(), [](rd_kafka_t*, const rd_kafka_message_t* message, void* opaque) {
    static_cast<State*>(opaque)->on_delivery(message);
  });
  rd_kafka_conf_set_error_cb(conf->get(), [](rd_kafka_t*, int code, const char* reason, void* opaque) {
    static_cast<State*>(opaque)->on_error(code, reason);
  });
  std::array<char, 512> text{};
  detail::KafkaPtr handle(rd_kafka_new(RD_KAFKA_PRODUCER, conf->get(), text.data(), text.size()));
  if (!handle) {
    return fail(ErrorCode::kCommonInvalidArgument, "create kafka producer: " + std::string(text.data()));
  }
  std::ignore = conf->release();  // owned by the handle now
  state->handle = std::move(handle);
  state->queue.reset(rd_kafka_queue_get_main(state->handle.get()));
  state->events = std::make_unique<detail::QueueEvents>(executor, state->queue.get());
  asio::co_spawn(executor, State::pump(state), asio::detached);
  return std::unique_ptr<Producer>(new Producer(std::move(state)));
}

asio::awaitable<Result<void>> Producer::enqueue(Record record) {
  co_return co_await state_->produce_waiting(&record, nullptr);
}

asio::awaitable<Result<Delivery>> Producer::send(Record record) {
  auto pending = std::make_shared<Pending>();
  auto holder = std::make_unique<std::shared_ptr<Pending>>(pending);
  if (auto produced = co_await state_->produce_waiting(&record, holder.get()); !produced) {
    co_return std::unexpected(std::move(produced.error()));
  }
  std::ignore = holder.release();  // the delivery report frees it
  if (!pending->result) {
    asio::steady_timer event(co_await asio::this_coro::executor, asio::steady_timer::time_point::max());
    pending->event = &event;
    std::ignore = co_await event.async_wait(asio::as_tuple(asio::use_awaitable));
    pending->event = nullptr;
  }
  if (!pending->result) {
    co_return fail(ErrorCode::kCommonUnavailable, "send to " + record.topic + ": cancelled, outcome unknown");
  }
  co_return std::move(*pending->result);
}

asio::awaitable<Result<void>> Producer::flush(Duration timeout) {
  const auto deadline = Clock::now() + timeout;
  asio::steady_timer timer(co_await asio::this_coro::executor);
  while (true) {
    state_->serve();
    if (rd_kafka_outq_len(state_->handle.get()) == 0) {
      break;
    }
    if (Clock::now() >= deadline) {
      co_return fail(ErrorCode::kCommonDeadlineExceeded, "kafka flush: messages still in flight");
    }
    timer.expires_after(detail::kFlushInterval);
    if (auto [ec] = co_await timer.async_wait(asio::as_tuple(asio::use_awaitable)); ec) {
      co_return fail(ErrorCode::kCommonUnavailable, "kafka flush: cancelled");
    }
  }
  co_return state_->take_delivery_error();
}

Result<void> Producer::flush_now(Duration timeout) {
  if (const auto err = rd_kafka_flush(state_->handle.get(), static_cast<int>(timeout.count())); err) {
    return std::unexpected(detail::to_error(err, "kafka flush"));
  }
  state_->space.notify_all();
  return state_->take_delivery_error();
}

asio::awaitable<Result<void>> Producer::init_transactions(Duration timeout) {
  if (auto usable = state_->transactional("init transactions", true); !usable) {
    co_return usable;
  }
  auto state = state_;
  co_return co_await state->in_pool([state, ms = static_cast<int>(timeout.count())]() -> Result<void> {
    return state->check(rd_kafka_init_transactions(state->handle.get(), ms), "init transactions");
  });
}

Result<void> Producer::begin_transaction() {
  if (auto usable = state_->transactional("begin transaction", false); !usable) {
    return usable;
  }
  if (auto begun = state_->check(rd_kafka_begin_transaction(state_->handle.get()), "begin the transaction"); !begun) {
    return begun;
  }
  state_->transaction = true;
  return {};
}

asio::awaitable<Result<void>> Producer::commit_transaction(const Consumer* source, Offsets offsets, Duration timeout) {
  if (auto usable = state_->transactional("commit transaction", true); !usable) {
    co_return usable;
  }
  // Deliver first with the pump: the commit itself then has nothing left to flush.
  if (auto flushed = co_await flush(timeout); !flushed) {
    co_return flushed;
  }
  auto state = state_;
  std::shared_ptr<rd_kafka_consumer_group_metadata_t> group;
  if (source != nullptr) {
    group = source->group_metadata();
  }
  auto committed =
      co_await state->in_pool([state, group, offsets, ms = static_cast<int>(timeout.count())]() -> Result<void> {
        return state->commit_blocking(group.get(), offsets, ms);
      });
  if (committed) {
    state_->transaction = false;
  }
  co_return committed;
}

Result<void> Producer::commit_transaction_now(const Consumer* source, const Offsets& offsets, Duration timeout) {
  if (auto usable = state_->transactional("commit transaction", false); !usable) {
    return usable;
  }
  const GroupMetadata group = source != nullptr ? source->group_metadata() : GroupMetadata{};
  auto committed = state_->commit_blocking(group.get(), offsets, static_cast<int>(timeout.count()));
  state_->serve();
  if (committed) {
    state_->transaction = false;
  }
  return committed;
}

asio::awaitable<Result<void>> Producer::abort_transaction(Duration timeout) {
  if (auto usable = state_->transactional("abort transaction", true); !usable) {
    co_return usable;
  }
  auto state = state_;
  auto aborted = co_await state->in_pool(
      [state, ms = static_cast<int>(timeout.count())]() -> Result<void> { return state->abort_blocking(ms); });
  state_->delivery_error.reset();  // failed messages of the aborted transaction do not matter
  if (aborted) {
    state_->transaction = false;
  }
  co_return aborted;
}

Result<void> Producer::abort_transaction_now(Duration timeout) {
  if (auto usable = state_->transactional("abort transaction", false); !usable) {
    return usable;
  }
  auto aborted = state_->abort_blocking(static_cast<int>(timeout.count()));
  state_->serve();
  state_->delivery_error.reset();
  if (aborted) {
    state_->transaction = false;
  }
  return aborted;
}

bool Producer::in_transaction() const noexcept {
  return state_->transaction;
}

bool Producer::fatal() const noexcept {
  return state_->fatal;
}

rd_kafka_t* Producer::handle() const noexcept {
  return state_->handle.get();
}

}  // namespace psim::platform::kafka
