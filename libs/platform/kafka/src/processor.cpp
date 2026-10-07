#include "psim/platform/kafka/processor.hpp"

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/async/clock.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/metrics.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/log.hpp"

namespace psim::platform::kafka {

namespace {

using std::chrono::milliseconds;

constexpr milliseconds kMaxPollWait{100};

std::string rfc3339(async::WallTime time) {
  return std::format("{:%FT%TZ}", std::chrono::floor<milliseconds>(time));
}

}  // namespace

milliseconds RetryPolicy::delay(std::uint32_t attempt) const noexcept {
  const double factor = std::pow(multiplier, static_cast<double>(attempt > 0 ? attempt - 1 : 0));
  const double value = static_cast<double>(initial.count()) * factor;
  return value >= static_cast<double>(max.count()) ? max : milliseconds(static_cast<std::int64_t>(value));
}

Processor::Processor(Consumer* input, Producer* output, async::Clock* clock, ProcessorOptions options,
                     Dependencies deps)
    : input_(input),
      output_(output),
      clock_(clock),
      options_(std::move(options)),
      deps_(deps),
      sink_(output),
      dead_letter_topic_("psim.dlq." + options_.stage + ".v1") {}

Processor::~Processor() {
  input_->set_listener(nullptr);
}

asio::awaitable<Result<void>> Processor::run(Handler* handler) {
  const bool exactly_once = options_.semantics == Semantics::kExactlyOnce;
  input_->set_listener(this);
  if (exactly_once && !initialized_) {
    if (auto init = co_await output_->init_transactions(options_.timeout); !init) {
      co_return std::unexpected(std::move(init.error()).context("processor " + options_.stage));
    }
    initialized_ = true;
  }
  last_commit_ = clock_->steady();
  while (!stopping_) {
    resume_due();
    if (auto committed = co_await commit(false); !committed) {
      co_return committed;
    }
    auto batch = co_await input_->poll(options_.max_batch, poll_wait());
    if (!batch) {
      co_return std::unexpected(std::move(batch.error()));
    }
    std::vector<TopicPartition> skipped;
    for (const auto& message : *batch) {
      const auto id = message.topic_partition();
      if (std::ranges::contains(skipped, id) || !input_->assignment().contains(id)) {
        continue;  // retried later, or revoked by a rebalance during this batch
      }
      auto& partition = partitions_[id];
      if (partition.stopped) {
        continue;
      }
      if (partition.committed < 0) {
        partition.committed = message.offset();  // the group resumed here
      }
      if (exactly_once && !output_->in_transaction()) {
        if (auto begun = output_->begin_transaction(); !begun) {
          co_return begun;
        }
        transaction_started_ = clock_->steady();
      }
      if (exactly_once && partition.transaction_start < 0) {
        partition.transaction_start = message.offset();
      }
      auto step = co_await process(handler, &message, &partition);
      if (!step) {
        co_return std::unexpected(std::move(step.error()));
      }
      if (*step == Step::kSkipPartition) {
        skipped.push_back(id);
      }
    }
  }
  co_return co_await commit(true);
}

asio::awaitable<Result<Processor::Step>> Processor::process(Handler* handler, const Message* message,
                                                            Partition* partition) {
  Result<void> result;
  std::optional<std::string> program_error;
  try {
    result = co_await handler->handle(message, &sink_);
  } catch (const std::exception& e) {
    program_error = e.what();
  } catch (...) {
    program_error = "unknown exception";
  }
  if (program_error) {
    const Error error(ErrorCode::kCommonInternal, "handler failed: " + *program_error);
    if (auto sent = co_await dead_letter(message, error); !sent) {
      retry_later(*message, *partition, sent.error());
      co_return Step::kSkipPartition;
    }
    partition->next = message->offset() + 1;
    partition->attempts = 0;
    dirty_ = true;
    if (++partition->program_errors >= options_.circuit_breaker) {
      stop_partition(message->topic_partition(), *partition, partition->next);
      co_return Step::kSkipPartition;
    }
    co_return Step::kNext;
  }
  if (!result) {
    if (result.error().retryable()) {
      retry_later(*message, *partition, result.error());
      co_return Step::kSkipPartition;
    }
    if (auto sent = co_await dead_letter(message, result.error()); !sent) {
      retry_later(*message, *partition, sent.error());
      co_return Step::kSkipPartition;
    }
  }
  partition->next = message->offset() + 1;
  partition->attempts = 0;
  partition->program_errors = 0;
  dirty_ = true;
  co_return Step::kNext;
}

asio::awaitable<Result<void>> Processor::dead_letter(const Message* message, Error error) {
  Record record{.topic = dead_letter_topic_,
                .key = std::string(message->key()),
                .value = std::string(message->value()),
                .headers = message->headers(),
                .partition = std::nullopt};
  record.headers.emplace_back("psim-dlq-stage", options_.stage);
  record.headers.emplace_back("psim-dlq-error-code", std::string(error.name()));
  record.headers.emplace_back("psim-dlq-error", error.message());
  record.headers.emplace_back("psim-dlq-source-topic", std::string(message->topic()));
  record.headers.emplace_back("psim-dlq-source-partition", std::to_string(message->partition()));
  record.headers.emplace_back("psim-dlq-source-offset", std::to_string(message->offset()));
  record.headers.emplace_back("psim-dlq-at", rfc3339(clock_->wall()));
  if (auto sent = co_await output_->enqueue(std::move(record)); !sent) {
    co_return sent;
  }
  if (deps_.metrics != nullptr) {
    deps_.metrics->dead_letters(options_.stage, error.name()).Increment();
  }
  if (deps_.logger != nullptr) {
    deps_.logger->warn("message sent to the dead letter topic",
                       {{"stage", options_.stage},
                        {"code", error.name()},
                        {"error", error.message()},
                        {"topic", message->topic()},
                        {"partition", static_cast<std::int64_t>(message->partition())},
                        {"offset", message->offset()}});
  }
  co_return Result<void>{};
}

// Transient error: rewind the partition to the message, pause it and resume after the backoff;
// other partitions go on (ADR-006, item 4).
void Processor::retry_later(const Message& message, Partition& partition, const Error& error) {
  const auto id = message.topic_partition();
  ++partition.attempts;
  const auto delay = options_.retry.delay(partition.attempts);
  partition.resume_at = clock_->steady() + delay;
  if (auto sought = input_->seek({{id, message.offset()}}); !sought && deps_.logger != nullptr) {
    deps_.logger->warn("kafka seek failed", {{"error", sought.error().message()}});
  }
  std::ignore = input_->pause({id});
  if (deps_.metrics != nullptr) {
    deps_.metrics->retries(options_.stage).Increment();
  }
  if (deps_.logger != nullptr) {
    deps_.logger->warn("message will be retried", {{"stage", options_.stage},
                                                   {"code", error.name()},
                                                   {"error", error.message()},
                                                   {"topic", message.topic()},
                                                   {"partition", static_cast<std::int64_t>(message.partition())},
                                                   {"offset", message.offset()},
                                                   {"attempt", static_cast<std::uint64_t>(partition.attempts)},
                                                   {"delay_ms", static_cast<std::int64_t>(delay.count())}});
  }
}

void Processor::stop_partition(const TopicPartition& id, Partition& partition, std::int64_t next) {
  partition.stopped = true;
  std::ignore = input_->seek({{id, next}});
  std::ignore = input_->pause({id});
  if (deps_.metrics != nullptr) {
    deps_.metrics->stopped_partitions(options_.stage).Increment();
  }
  if (deps_.logger != nullptr) {
    deps_.logger->error("partition stopped after repeated program errors",
                        {{"stage", options_.stage},
                         {"topic", id.topic},
                         {"partition", static_cast<std::int64_t>(id.partition)},
                         {"errors", static_cast<std::uint64_t>(partition.program_errors)}});
  }
}

void Processor::resume_due() {
  const auto now = clock_->steady();
  for (auto& [id, partition] : partitions_) {
    if (partition.resume_at && *partition.resume_at <= now && !partition.stopped) {
      partition.resume_at.reset();
      std::ignore = input_->resume({id});
    }
  }
}

std::chrono::milliseconds Processor::poll_wait() const {
  const auto now = clock_->steady();
  auto due =
      (options_.semantics == Semantics::kExactlyOnce ? transaction_started_ : last_commit_) + options_.commit_interval;
  for (const auto& [id, partition] : partitions_) {
    if (partition.resume_at) {
      due = std::min(due, *partition.resume_at);
    }
  }
  const auto wait = std::chrono::duration_cast<milliseconds>(due - now);
  return std::clamp(wait, milliseconds(1), kMaxPollWait);
}

asio::awaitable<Result<void>> Processor::commit(bool force) {
  const auto now = clock_->steady();
  if (options_.semantics == Semantics::kExactlyOnce) {
    if (output_->in_transaction() && (force || now - transaction_started_ >= options_.commit_interval)) {
      co_return co_await commit_transaction();
    }
    co_return Result<void>{};
  }
  if (dirty_ && (force || now - last_commit_ >= options_.commit_interval)) {
    co_return co_await commit_offsets();
  }
  co_return Result<void>{};
}

Offsets Processor::pending_offsets(bool transaction_only) const {
  Offsets offsets;
  for (const auto& [id, partition] : partitions_) {
    const bool touched = transaction_only ? partition.transaction_start >= 0 : partition.next > partition.committed;
    if (touched && partition.next >= 0) {
      offsets[id] = partition.next;
    }
  }
  return offsets;
}

// ALO: outputs reach Kafka first, then the offsets move; if the outputs fail, the partitions go
// back to the committed offsets and the messages are processed again.
asio::awaitable<Result<void>> Processor::commit_offsets() {
  last_commit_ = clock_->steady();
  if (auto flushed = co_await output_->flush(options_.timeout); !flushed) {
    if (deps_.logger != nullptr) {
      deps_.logger->warn("outputs not delivered, reprocessing from the committed offsets",
                         {{"stage", options_.stage}, {"error", flushed.error().message()}});
    }
    rewind(false);
    co_return Result<void>{};
  }
  const auto offsets = pending_offsets(false);
  if (auto committed = co_await input_->commit(offsets); !committed) {
    if (deps_.logger != nullptr) {
      deps_.logger->warn("offset commit failed", {{"stage", options_.stage}, {"error", committed.error().message()}});
    }
    co_return Result<void>{};  // the next interval commits again; a revoked partition is forgotten
  }
  for (const auto& [id, offset] : offsets) {
    if (const auto it = partitions_.find(id); it != partitions_.end()) {
      it->second.committed = offset;
    }
  }
  dirty_ =
      std::ranges::any_of(partitions_, [](const auto& entry) { return entry.second.next > entry.second.committed; });
  co_return Result<void>{};
}

// EOS-K: outputs and offsets commit atomically; on failure the transaction is aborted and its
// partitions rewound to where it started.
asio::awaitable<Result<void>> Processor::commit_transaction() {
  const auto offsets = pending_offsets(true);
  auto committed = co_await output_->commit_transaction(input_, offsets, options_.timeout);
  if (committed) {
    if (deps_.metrics != nullptr) {
      deps_.metrics->transactions("commit").Increment();
    }
    for (auto& [id, partition] : partitions_) {
      if (partition.transaction_start >= 0) {
        partition.committed = partition.next;
        partition.transaction_start = -1;
      }
    }
    dirty_ = false;
    co_return Result<void>{};
  }
  if (output_->fatal()) {
    co_return std::unexpected(std::move(committed.error()).context("processor " + options_.stage));
  }
  if (deps_.logger != nullptr) {
    deps_.logger->warn("transaction aborted", {{"stage", options_.stage}, {"error", committed.error().message()}});
  }
  if (auto aborted = co_await output_->abort_transaction(options_.timeout); !aborted && output_->fatal()) {
    co_return std::unexpected(std::move(aborted.error()).context("processor " + options_.stage));
  }
  if (deps_.metrics != nullptr) {
    deps_.metrics->transactions("abort").Increment();
  }
  rewind(true);
  co_return Result<void>{};
}

void Processor::rewind(bool transaction_only) {
  Offsets offsets;
  for (auto& [id, partition] : partitions_) {
    const std::int64_t to = transaction_only ? partition.transaction_start : partition.committed;
    if (to >= 0 && (!transaction_only || partition.transaction_start >= 0)) {
      offsets[id] = to;
      partition.next = to;
    }
    partition.transaction_start = -1;
  }
  if (auto sought = input_->seek(offsets); !sought && deps_.logger != nullptr) {
    deps_.logger->warn("kafka seek failed", {{"error", sought.error().message()}});
  }
  dirty_ = false;
}

void Processor::on_assigned(const std::vector<TopicPartition>& partitions) {
  for (const auto& id : partitions) {
    forget(id);  // a fresh start from the committed offset
  }
}

// Inside Consumer::poll(), between messages: commit what is done before the partitions leave.
void Processor::on_revoked(const std::vector<TopicPartition>& partitions, bool lost) {
  if (options_.semantics == Semantics::kExactlyOnce) {
    if (output_->in_transaction()) {
      Result<void> committed = fail(ErrorCode::kCommonUnavailable, "partitions lost");
      if (!lost) {
        committed = output_->commit_transaction_now(input_, pending_offsets(true), options_.timeout);
      }
      if (committed) {
        for (auto& [id, partition] : partitions_) {
          if (partition.transaction_start >= 0) {
            partition.committed = partition.next;
            partition.transaction_start = -1;
          }
        }
      } else if (!output_->fatal()) {
        std::ignore = output_->abort_transaction_now(options_.timeout);
        rewind(true);
      }
      if (deps_.metrics != nullptr) {
        deps_.metrics->transactions(committed ? "commit" : "abort").Increment();
      }
    }
  } else if (!lost && output_->flush_now(options_.timeout)) {
    Offsets offsets;
    for (const auto& id : partitions) {
      if (const auto it = partitions_.find(id); it != partitions_.end() && it->second.next > it->second.committed) {
        offsets[id] = it->second.next;
      }
    }
    if (auto committed = input_->commit_now(offsets); !committed && deps_.logger != nullptr) {
      deps_.logger->warn("offset commit on revoke failed",
                         {{"stage", options_.stage}, {"error", committed.error().message()}});
    }
  }
  for (const auto& id : partitions) {
    forget(id);
  }
}

void Processor::forget(const TopicPartition& id) {
  const auto it = partitions_.find(id);
  if (it == partitions_.end()) {
    return;
  }
  if (it->second.stopped && deps_.metrics != nullptr) {
    deps_.metrics->stopped_partitions(options_.stage).Decrement();
  }
  partitions_.erase(it);
}

}  // namespace psim::platform::kafka
