#include "psim/platform/kafka/consumer.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <librdkafka/rdkafka.h>
#include <nlohmann/json.hpp>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "detail.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/metrics.hpp"
#include "psim/platform/log.hpp"

namespace psim::platform::kafka {

namespace {

using Clock = std::chrono::steady_clock;

// Leaving the group waits for the coordinator; a broker that is gone must not hold the shutdown.
constexpr std::chrono::seconds kCloseTimeout{30};

}  // namespace

void GroupMetadataDeleter::operator()(rd_kafka_consumer_group_metadata_t* metadata) const noexcept {
  rd_kafka_consumer_group_metadata_destroy(metadata);
}

struct Consumer::State {
  State() = default;
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;

  ConsumerOptions options;
  Dependencies deps;
  detail::KafkaPtr handle;
  detail::QueuePtr queue;
  std::unique_ptr<detail::QueueEvents> events;
  RebalanceListener* listener = nullptr;
  std::set<TopicPartition> assignment;
  Offsets positions;
  std::map<std::string, std::pair<prometheus::Counter*, prometheus::Counter*>, std::less<>> series;
  bool closed = false;
  bool fatal = false;

  ~State() {
    if (events) {
      events->close();
    }
    events.reset();
    queue.reset();
    if (handle && !closed) {
      // Leaving without close(): the group notices by session timeout, as after a crash.
      rd_kafka_destroy_flags(handle.release(), RD_KAFKA_DESTROY_F_NO_CONSUMER_CLOSE);
    }
  }

  void log_warn(std::string_view message, std::initializer_list<log::Field> fields) const {
    if (deps.logger != nullptr) {
      deps.logger->warn(message, fields);
    }
  }

  void on_rebalance(rd_kafka_resp_err_t reason, rd_kafka_topic_partition_list_t* list) {
    const bool cooperative = std::strcmp(rd_kafka_rebalance_protocol(handle.get()), "COOPERATIVE") == 0;
    const auto partitions = detail::partitions_of(list);
    if (reason == RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS) {
      if (cooperative) {
        const detail::ErrorPtr error(rd_kafka_incremental_assign(handle.get(), list));
        if (error) {
          log_warn("kafka assign failed", {{"error", rd_kafka_error_string(error.get())}});
        }
      } else {
        std::ignore = rd_kafka_assign(handle.get(), list);
      }
      assignment.insert(partitions.begin(), partitions.end());
      count_rebalance("assign", partitions.size());
      if (deps.logger != nullptr) {
        deps.logger->info("kafka partitions assigned",
                          {{"group", options.group_id}, {"count", static_cast<std::uint64_t>(partitions.size())}});
      }
      if (listener != nullptr) {
        listener->on_assigned(partitions);
      }
      return;
    }
    const bool lost = reason != RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS || rd_kafka_assignment_lost(handle.get()) != 0;
    if (listener != nullptr) {
      listener->on_revoked(partitions, lost);
    }
    for (const auto& partition : partitions) {
      assignment.erase(partition);
      if (deps.metrics != nullptr) {
        deps.metrics->consumer_lag(options.group_id, partition.topic, partition.partition).Set(0);
      }
    }
    if (cooperative) {
      const detail::ErrorPtr error(rd_kafka_incremental_unassign(handle.get(), list));
      if (error) {
        log_warn("kafka unassign failed", {{"error", rd_kafka_error_string(error.get())}});
      }
    } else {
      std::ignore = rd_kafka_assign(handle.get(), nullptr);
      assignment.clear();
    }
    count_rebalance(lost ? "lost" : "revoke", partitions.size());
    if (deps.logger != nullptr) {
      deps.logger->info(
          "kafka partitions revoked",
          {{"group", options.group_id}, {"count", static_cast<std::uint64_t>(partitions.size())}, {"lost", lost}});
    }
  }

  void count_rebalance(std::string_view kind, std::size_t partitions) const {
    if (deps.metrics != nullptr) {
      deps.metrics->rebalances(options.group_id, kind).Increment(static_cast<double>(partitions));
    }
  }

  // Lag per assigned partition from the librdkafka statistics (statistics.interval.ms).
  void on_statistics(std::string_view json) const {
    if (deps.metrics == nullptr) {
      return;
    }
    const auto stats = nlohmann::json::parse(json, nullptr, false);
    if (stats.is_discarded() || !stats.contains("topics")) {
      return;
    }
    for (const auto& [topic, data] : stats["topics"].items()) {
      for (const auto& [id, partition] : data.value("partitions", nlohmann::json::object()).items()) {
        const auto number = partition.value("partition", -1);
        const auto lag = partition.value("consumer_lag", std::int64_t{-1});
        if (number < 0 || lag < 0 || !assignment.contains({.topic = topic, .partition = number})) {
          continue;
        }
        deps.metrics->consumer_lag(options.group_id, topic, number).Set(static_cast<double>(lag));
      }
    }
  }

  void on_error(int code, const char* /*reason*/) {
    const auto err = static_cast<rd_kafka_resp_err_t>(code);
    if (err == RD_KAFKA_RESP_ERR__FATAL) {
      fatal = true;
    }
    log_warn("kafka consumer error", {{"error", rd_kafka_err2name(err)}, {"reason", rd_kafka_err2name(err)}});
  }

  void count(const rd_kafka_message_t* message) {
    if (deps.metrics == nullptr) {
      return;
    }
    const std::string_view topic = rd_kafka_topic_name(message->rkt);
    auto it = series.find(topic);
    if (it == series.end()) {
      it = series
               .emplace(std::string(topic),
                        std::pair{&deps.metrics->consumed_messages(topic), &deps.metrics->consumed_bytes(topic)})
               .first;
    }
    it->second.first->Increment();
    it->second.second->Increment(static_cast<double>(message->len));
  }

  /// Take what the queue holds now, serving rebalances and statistics on the way; messages of
  /// partitions revoked meanwhile are dropped (their new owner reads them again).
  void take(std::size_t max, std::vector<Message>& out) {
    std::vector<rd_kafka_message_t*> raw(max);
    const auto got = rd_kafka_consume_batch_queue(queue.get(), 0, raw.data(), raw.size());
    for (ssize_t i = 0; i < got; ++i) {
      Message message(raw[static_cast<std::size_t>(i)]);
      const auto* view = raw[static_cast<std::size_t>(i)];
      if (view->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        if (view->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
          positions[message.topic_partition()] = message.offset();
        }
        if (view->err != RD_KAFKA_RESP_ERR__PARTITION_EOF) {
          log_warn("kafka consumer error", {{"error", rd_kafka_err2name(view->err)}});
        }
        continue;
      }
      if (!assignment.contains(message.topic_partition())) {
        continue;
      }
      count(view);
      positions[message.topic_partition()] = message.offset() + 1;
      out.push_back(std::move(message));
    }
  }
};

Consumer::Consumer(std::shared_ptr<State> state) : state_(std::move(state)) {}

Consumer::~Consumer() {
  if (state_ && state_->events) {
    state_->events->close();
  }
}

Result<std::unique_ptr<Consumer>> Consumer::create(const asio::any_io_executor& executor, ConsumerOptions options,
                                                   Dependencies deps) {
  if (options.group_id.empty() || options.topics.empty()) {
    return fail(ErrorCode::kCommonInvalidArgument, "kafka consumer: group and topics are required");
  }
  const detail::Properties defaults{
      {"auto.offset.reset", options.auto_offset_reset},
      {"statistics.interval.ms", "5000"},
      {"fetch.wait.max.ms", "100"},
      {"enable.partition.eof", "true"},
  };
  // Offsets move only when the layer commits them (ADR-041).
  detail::Properties fixed{
      {"group.id", options.group_id},
      {"enable.auto.commit", "false"},
      {"enable.auto.offset.store", "false"},
      {"isolation.level", options.read_committed ? "read_committed" : "read_uncommitted"},
      {"group.protocol", options.group_protocol},
  };
  if (options.group_protocol == "classic") {
    fixed.emplace("partition.assignment.strategy", "cooperative-sticky");
  }
  auto conf = detail::make_conf(options.client, defaults, fixed);
  if (!conf) {
    return std::unexpected(std::move(conf.error()));
  }
  auto state = std::make_shared<State>();
  state->options = std::move(options);
  state->deps = deps;
  rd_kafka_conf_set_opaque(conf->get(), state.get());
  rd_kafka_conf_set_rebalance_cb(
      conf->get(), [](rd_kafka_t*, rd_kafka_resp_err_t reason, rd_kafka_topic_partition_list_t* list, void* opaque) {
        static_cast<State*>(opaque)->on_rebalance(reason, list);
      });
  rd_kafka_conf_set_stats_cb(conf->get(), [](rd_kafka_t*, char* json, std::size_t size, void* opaque) {
    static_cast<State*>(opaque)->on_statistics(std::string_view(json, size));
    return 0;  // librdkafka frees the text
  });
  rd_kafka_conf_set_error_cb(conf->get(), [](rd_kafka_t*, int code, const char* reason, void* opaque) {
    static_cast<State*>(opaque)->on_error(code, reason);
  });
  std::array<char, 512> text{};
  detail::KafkaPtr handle(rd_kafka_new(RD_KAFKA_CONSUMER, conf->get(), text.data(), text.size()));
  if (!handle) {
    return fail(ErrorCode::kCommonInvalidArgument, "create kafka consumer failed");
  }
  std::ignore = conf->release();
  rd_kafka_poll_set_consumer(handle.get());  // one queue for messages, rebalances and statistics
  state->handle = std::move(handle);
  state->queue.reset(rd_kafka_queue_get_consumer(state->handle.get()));
  state->events = std::make_unique<detail::QueueEvents>(executor, state->queue.get());
  const auto topics = detail::to_list(std::vector<TopicPartition>{});
  for (const auto& topic : state->options.topics) {
    rd_kafka_topic_partition_list_add(topics.get(), topic.c_str(), RD_KAFKA_PARTITION_UA);
  }
  if (const auto err =
          state->options.manual ? RD_KAFKA_RESP_ERR_NO_ERROR : rd_kafka_subscribe(state->handle.get(), topics.get());
      err) {
    return std::unexpected(detail::to_error(err, "subscribe"));
  }
  return std::unique_ptr<Consumer>(new Consumer(std::move(state)));
}

void Consumer::set_listener(RebalanceListener* listener) noexcept {
  state_->listener = listener;
}

asio::awaitable<Result<std::vector<Message>>> Consumer::poll(std::size_t max, Duration wait) {
  auto state = state_;
  std::vector<Message> batch;
  const auto deadline = Clock::now() + wait;
  while (true) {
    if (state->fatal) {
      co_return fail(ErrorCode::kCommonInternal, "kafka consumer is in a fatal state");
    }
    state->take(max, batch);
    const auto now = Clock::now();
    if (!batch.empty() || now >= deadline || state->events->closed()) {
      co_return batch;
    }
    const auto left = std::chrono::duration_cast<Duration>(deadline - now) + Duration(1);
    if (!co_await state->events->wait(std::min(left, detail::kServeInterval))) {
      co_return batch;
    }
  }
}

asio::awaitable<Result<void>> Consumer::commit(Offsets offsets) {
  if (offsets.empty()) {
    co_return Result<void>{};
  }
  if (state_->deps.blocking == nullptr) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "kafka commit: no blocking pool");
  }
  auto state = state_;
  co_return co_await state->deps.blocking->run([state, offsets = std::move(offsets)]() -> Result<void> {
    const auto list = detail::to_list(offsets);
    if (const auto err = rd_kafka_commit(state->handle.get(), list.get(), 0); err) {
      return std::unexpected(detail::to_error(err, "commit offsets"));
    }
    return {};
  });
}

Result<void> Consumer::commit_now(const Offsets& offsets) {
  if (offsets.empty()) {
    return {};
  }
  const auto list = detail::to_list(offsets);
  if (const auto err = rd_kafka_commit(state_->handle.get(), list.get(), 0); err) {
    return std::unexpected(detail::to_error(err, "commit offsets"));
  }
  return {};
}

asio::awaitable<Result<Offsets>> Consumer::committed(std::vector<TopicPartition> partitions, Duration timeout) {
  if (state_->deps.blocking == nullptr) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "kafka committed offsets: no blocking pool");
  }
  auto state = state_;
  co_return co_await state->deps.blocking->run(
      [state, partitions = std::move(partitions), ms = static_cast<int>(timeout.count())]() -> Result<Offsets> {
        const auto list = detail::to_list(partitions);
        if (const auto err = rd_kafka_committed(state->handle.get(), list.get(), ms); err) {
          return std::unexpected(detail::to_error(err, "read committed offsets"));
        }
        auto offsets = detail::offsets_of(list.get());
        std::erase_if(offsets, [](const auto& entry) { return entry.second < 0; });
        return offsets;
      });
}

Result<void> Consumer::seek(const Offsets& offsets) {
  if (offsets.empty()) {
    return {};
  }
  const auto list = detail::to_list(offsets);
  const detail::ErrorPtr error(rd_kafka_seek_partitions(state_->handle.get(), list.get(), 0));
  if (error) {
    return std::unexpected(detail::to_error(error.get(), "seek"));
  }
  for (int i = 0; i < list->cnt; ++i) {
    if (const auto err = list->elems[i].err; err) {  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      return std::unexpected(detail::to_error(err, "seek"));
    }
  }
  return {};
}

Result<void> Consumer::assign(const Offsets& offsets) {
  if (!state_->options.manual) {
    return fail(ErrorCode::kCommonInvalidArgument, "manual assignment requires a manual consumer");
  }
  const auto list = detail::to_list(offsets);
  if (const auto error = rd_kafka_assign(state_->handle.get(), list.get()); error) {
    return std::unexpected(detail::to_error(error, "assign replay partitions"));
  }
  state_->assignment.clear();
  state_->positions = offsets;
  for (const auto& [partition, offset] : offsets) {
    state_->assignment.insert(partition);
  }
  return {};
}

const Offsets& Consumer::positions() const noexcept {
  return state_->positions;
}

asio::awaitable<Result<Offsets>> Consumer::end_offsets(Duration timeout) {
  auto bounds = co_await offset_bounds(timeout);
  if (!bounds) {
    co_return std::unexpected(bounds.error());
  }
  co_return std::move(bounds->end);
}

asio::awaitable<Result<OffsetBounds>> Consumer::offset_bounds(Duration timeout) {
  auto state = state_;
  if (state->deps.blocking == nullptr || timeout.count() <= 0 || timeout.count() > 3600000) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "replay bounds require a blocking pool and bounded timeout");
  }
  co_return co_await state->deps.blocking->run([state, timeout]() -> Result<OffsetBounds> {
    OffsetBounds offsets;
    for (const auto& topic : state->options.topics) {
      using Topic = std::unique_ptr<rd_kafka_topic_t, decltype(&rd_kafka_topic_destroy)>;
      const Topic handle(rd_kafka_topic_new(state->handle.get(), topic.c_str(), nullptr), rd_kafka_topic_destroy);
      const rd_kafka_metadata_t* metadata = nullptr;
      if (const auto error =
              rd_kafka_metadata(state->handle.get(), 0, handle.get(), &metadata, static_cast<int>(timeout.count()));
          error) {
        return std::unexpected(detail::to_error(error, "read replay metadata"));
      }
      const std::unique_ptr<const rd_kafka_metadata_t, decltype(&rd_kafka_metadata_destroy)> owned(
          metadata, rd_kafka_metadata_destroy);
      for (int index = 0; index < metadata->topic_cnt; ++index) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): librdkafka C array
        const auto& entry = metadata->topics[index];
        if (entry.err) {
          return std::unexpected(detail::to_error(entry.err, "read replay topic"));
        }
        for (int part = 0; part < entry.partition_cnt; ++part) {
          // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): librdkafka C array
          const auto id = entry.partitions[part].id;
          std::int64_t low = 0;
          std::int64_t high = 0;
          if (const auto error = rd_kafka_query_watermark_offsets(state->handle.get(), topic.c_str(), id, &low, &high,
                                                                  static_cast<int>(timeout.count()));
              error) {
            return std::unexpected(detail::to_error(error, "read replay watermark"));
          }
          offsets.begin[{topic, id}] = low;
          offsets.end[{topic, id}] = high;
        }
      }
    }
    return offsets;
  });
}

asio::awaitable<Result<void>> Consumer::seek_and_wait(Offsets offsets, Duration timeout) {
  auto state = state_;
  if (state->deps.blocking == nullptr || timeout.count() <= 0 || timeout.count() > 3600000) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "seek requires a blocking pool and a bounded timeout");
  }
  co_return co_await state->deps.blocking->run([state, offsets = std::move(offsets), timeout]() -> Result<void> {
    if (offsets.empty()) {
      return {};
    }
    const auto list = detail::to_list(offsets);
    const detail::ErrorPtr error(
        rd_kafka_seek_partitions(state->handle.get(), list.get(), static_cast<int>(timeout.count())));
    if (error) {
      return std::unexpected(detail::to_error(error.get(), "seek"));
    }
    for (int index = 0; index < list->cnt; ++index) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): librdkafka C array
      if (const auto code = list->elems[index].err;
          code) {  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): librdkafka C array
        return std::unexpected(detail::to_error(code, "seek"));
      }
    }
    return {};
  });
}

Result<void> Consumer::pause(const std::vector<TopicPartition>& partitions) {
  const auto list = detail::to_list(partitions);
  if (const auto err = rd_kafka_pause_partitions(state_->handle.get(), list.get()); err) {
    return std::unexpected(detail::to_error(err, "pause partitions"));
  }
  return {};
}

Result<void> Consumer::resume(const std::vector<TopicPartition>& partitions) {
  const auto list = detail::to_list(partitions);
  if (const auto err = rd_kafka_resume_partitions(state_->handle.get(), list.get()); err) {
    return std::unexpected(detail::to_error(err, "resume partitions"));
  }
  return {};
}

const std::set<TopicPartition>& Consumer::assignment() const noexcept {
  return state_->assignment;
}

const std::string& Consumer::group() const noexcept {
  return state_->options.group_id;
}

GroupMetadata Consumer::group_metadata() const {
  return GroupMetadata(rd_kafka_consumer_group_metadata(state_->handle.get()));
}

asio::awaitable<void> Consumer::close() {
  auto state = state_;
  if (state->closed) {
    co_return;
  }
  if (state->fatal) {
    co_return;  // a fatal consumer cannot leave the group; the destructor drops it
  }
  // Asynchronous close: the revocation reaches the listener through the queue served here.
  const detail::ErrorPtr error(rd_kafka_consumer_close_queue(state->handle.get(), state->queue.get()));
  if (error) {
    state->log_warn("kafka consumer close failed", {{"error", rd_kafka_error_string(error.get())}});
  }
  std::vector<Message> dropped;
  const auto deadline = Clock::now() + kCloseTimeout;
  while (rd_kafka_consumer_closed(state->handle.get()) == 0 && !state->fatal && Clock::now() < deadline) {
    state->take(64, dropped);
    dropped.clear();
    if (!co_await state->events->wait(detail::kFlushInterval * 5)) {
      break;
    }
  }
  state->closed = rd_kafka_consumer_closed(state->handle.get()) != 0;
}

rd_kafka_t* Consumer::handle() const noexcept {
  return state_->handle.get();
}

}  // namespace psim::platform::kafka
