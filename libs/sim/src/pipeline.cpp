#include "psim/sim/pipeline.hpp"

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <format>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "psim/sim/broker.hpp"
#include "psim/sim/random.hpp"
#include "psim/sim/store.hpp"

namespace psim::sim {
namespace {

using Milliseconds = std::chrono::milliseconds;

class Pipeline {
 public:
  Pipeline(Options options, FaultScript script, Transform transform)
      : options_(options), random_(options.seed), script_(std::move(script)), transform_(std::move(transform)) {
    report_.options = options;
  }

  Report run() {
    try {
      prepare();
      for (std::size_t partition = 0; partition < options_.partitions; ++partition) {
        scheduler_.spawn(kafka(partition));
        scheduler_.spawn(database(partition));
        scheduler_.spawn(sink(partition));
      }
      scheduler_.spawn(relay());
      scheduler_.run(options_.budget);
      reconcile();
      report_.ok = true;
    } catch (const std::exception& error) {
      report_.error = error.what();
      record("failure " + report_.error);
    }
    report_.handlers = scheduler_.handlers();
    report_.elapsed = scheduler_.clock().steady().time_since_epoch();
    report_.delivered = sink_.effects().size();
    return std::move(report_);
  }

 private:
  void record(std::string text) {
    auto entry = std::format("{} {}", scheduler_.clock().steady().time_since_epoch().count(), text);
    // Stable FNV-1a, including record boundaries; never std::hash or wall-clock time.
    for (const char ch : entry) {
      report_.fingerprint ^= static_cast<unsigned char>(ch);
      report_.fingerprint *= 1099511628211ULL;
    }
    report_.fingerprint ^= 10U;
    report_.fingerprint *= 1099511628211ULL;
    report_.trace.push_back(std::move(entry));
  }

  void prepare() {
    if (options_.events == 0 || options_.events > 10000 || options_.partitions == 0 || options_.partitions > 64) {
      throw std::invalid_argument("events must be 1..10000 and partitions 1..64");
    }
    for (const auto* topic : {"raw", "normalized", "changelog", "delivered"}) {
      broker_.create_topic(topic, options_.partitions);
    }
    kafka_done_.resize(options_.partitions, false);
    database_done_.resize(options_.partitions, false);
    std::vector<std::pair<std::size_t, Message>> inputs;
    for (std::size_t index = 0; index < options_.events; ++index) {
      // The same id exists in two tenants, exercising isolation rather than global-id dedupe.
      Message message{.tenant = std::format("tenant-{}", index % 2),
                      .id = std::format("event-{}", index / 2),
                      .payload = std::format("source-{}:seq-{}", index % 4, index)};
      expected_.emplace(key_of(message), message);
      const auto partition = index % options_.partitions;
      inputs.emplace_back(partition, message);
      inputs.emplace_back(partition, std::move(message));
    }
    // Fisher-Yates uses our bounded sampler, giving portable duplication and reordering.
    for (std::size_t remaining = inputs.size(); remaining > 1; --remaining) {
      const auto index = static_cast<std::size_t>(random_.bounded(remaining));
      std::swap(inputs.at(remaining - 1), inputs.at(index));
    }
    for (auto& [partition, message] : inputs) {
      record(std::format("input {} {} {} {}", partition, message.tenant, message.id, message.payload));
      broker_.append("raw", partition, std::move(message));
    }
    report_.expected = expected_.size();
  }

  Action choose(Stage stage, Point point, std::size_t partition) {
    const auto decision = decisions_++;
    Action action = Action::kPass;
    if (script_) {
      action = script_(stage, point, partition, decision);
    } else if (decision < options_.fault_decisions) {
      const auto choice = random_.bounded(10);
      if (choice == 0) {
        action = Action::kCrash;
      } else if (choice == 1) {
        action = Action::kUnavailable;
      } else if (choice == 2) {
        action = Action::kDelay;
      } else if (choice == 3 && (point == Point::kRead || point == Point::kPublish || point == Point::kAcknowledge)) {
        action = Action::kDrop;
      } else if (choice == 4 && point == Point::kPublish) {
        action = Action::kDuplicate;
      }
    }
    ++report_.actions.at(static_cast<std::size_t>(action));
    record(std::format("decision {} stage={} point={} partition={} action={}", decision, static_cast<int>(stage),
                       static_cast<int>(point), partition, static_cast<int>(action)));
    return action;
  }

  boost::asio::awaitable<bool> checkpoint(Stage stage, Point point, std::size_t partition) {
    const auto action = choose(stage, point, partition);
    // Every I/O boundary yields; seeded latency also changes inter-partition execution order.
    const auto delay = action == Action::kDelay ? 20 : 1 + static_cast<int>(random_.bounded(3));
    std::ignore = co_await scheduler_.clock().sleep_for(Milliseconds{delay});
    if (action == Action::kCrash) {
      ++report_.restarts;
      record(std::format("restart stage={} partition={}", static_cast<int>(stage), partition));
    }
    co_return action == Action::kPass || action == Action::kDelay || action == Action::kDuplicate;
  }

  Message read_record(const std::string& topic, std::size_t partition, std::size_t offset) const {
    auto message = broker_.read(topic, partition, offset);
    if (!message) {
      throw std::runtime_error("durable record disappeared from the broker");
    }
    return std::move(*message);
  }

  std::set<MessageKey> restore(std::size_t partition) const {
    std::set<MessageKey> state;
    for (std::size_t offset = 0; offset < broker_.size("changelog", partition); ++offset) {
      state.insert(key_of(read_record("changelog", partition, offset)));
    }
    return state;
  }

  boost::asio::awaitable<void> kafka(std::size_t partition) {
    auto state = restore(partition);
    const auto producer = std::format("normalizer-{}", partition);
    while (broker_.offset("normalizer", "raw", partition) < broker_.size("raw", partition)) {
      if (!co_await checkpoint(Stage::kKafka, Point::kRead, partition)) {
        state = restore(partition);
        continue;
      }
      const auto offset = broker_.offset("normalizer", "raw", partition);
      const auto message = read_record("raw", partition, offset);
      auto transaction = broker_.begin(producer);
      if (!state.contains(key_of(message))) {
        const auto output = transform_ ? transform_(message) : message;
        transaction.publications.push_back({.topic = "normalized", .partition = partition, .message = output});
        transaction.publications.push_back({.topic = "changelog", .partition = partition, .message = message});
      }
      transaction.offsets.push_back(
          {.group = "normalizer", .topic = "raw", .partition = partition, .next = offset + 1});
      if (!co_await checkpoint(Stage::kKafka, Point::kBeforeCommit, partition)) {
        state = restore(partition);
        continue;  // volatile transaction is discarded by process death/failure
      }
      if (!broker_.commit(transaction)) {
        throw std::runtime_error("unexpected fenced Kafka transaction");
      }
      record(std::format("kafka commit partition={} offset={} id={}", partition, offset, message.id));
      if (!co_await checkpoint(Stage::kKafka, Point::kAfterCommit, partition)) {
        state = restore(partition);  // changelog + output + offsets survived the lost response
        continue;
      }
      state.insert(key_of(message));
    }
    kafka_done_.at(partition) = true;
  }

  boost::asio::awaitable<void> database(std::size_t partition) {
    for (;;) {
      const auto offset = broker_.offset("database", "normalized", partition);
      if (offset == broker_.size("normalized", partition)) {
        if (kafka_done_.at(partition)) {
          break;
        }
        std::ignore = co_await scheduler_.clock().sleep_for(Milliseconds{1});
        continue;
      }
      if (!co_await checkpoint(Stage::kDatabase, Point::kRead, partition)) {
        continue;
      }
      const auto message = read_record("normalized", partition, offset);
      auto transaction = database_.begin();
      std::ignore = Store::apply(transaction, message);
      if (!co_await checkpoint(Stage::kDatabase, Point::kBeforeCommit, partition)) {
        continue;
      }
      if (!database_.commit(std::move(transaction))) {
        record("database conflict");
        continue;  // another partition/relay changed the version; retry from durable state
      }
      record(std::format("database commit partition={} id={}", partition, message.id));
      if (!co_await checkpoint(Stage::kDatabase, Point::kAfterCommit, partition)) {
        continue;  // SQL commit survived; the uncommitted Kafka offset forces inbox redelivery
      }
      auto ack = broker_.begin(std::format("database-{}", partition));
      ack.offsets.push_back({.group = "database", .topic = "normalized", .partition = partition, .next = offset + 1});
      if (!broker_.commit(ack)) {
        throw std::runtime_error("database offset commit failed");
      }
    }
    database_done_.at(partition) = true;
  }

  boost::asio::awaitable<void> relay() {
    for (;;) {
      const auto pending = database_.pending();
      if (!pending) {
        if (std::ranges::all_of(database_done_, [](bool done) { return done; })) {
          break;
        }
        std::ignore = co_await scheduler_.clock().sleep_for(Milliseconds{1});
        continue;
      }
      const auto message = database_.entry(*pending).message;
      // Route every duplicate of an outbox entry to the same partition.
      const auto partition = *pending % options_.partitions;
      const auto action = choose(Stage::kRelay, Point::kPublish, partition);
      std::ignore = co_await scheduler_.clock().sleep_for(Milliseconds{action == Action::kDelay ? 20 : 1});
      if (action == Action::kCrash) {
        ++report_.restarts;
      }
      if (action == Action::kDrop || action == Action::kCrash || action == Action::kUnavailable) {
        continue;
      }
      broker_.append("delivered", partition, message);
      ++report_.publications;
      if (action == Action::kDuplicate) {
        broker_.append("delivered", partition, message);
        ++report_.publications;
      }
      record(std::format("relay publish partition={} id={}", partition, message.id));
      if (!co_await checkpoint(Stage::kRelay, Point::kAfterPublish, partition)) {
        continue;  // durable publish but no outbox acknowledgement: duplicate on restart
      }
      if (!co_await checkpoint(Stage::kRelay, Point::kAcknowledge, partition)) {
        continue;
      }
      database_.acknowledge(*pending);
    }
    relay_done_ = true;
  }

  boost::asio::awaitable<void> sink(std::size_t partition) {
    std::size_t offset = 0;
    for (;;) {
      if (offset == broker_.size("delivered", partition)) {
        if (relay_done_) {
          break;
        }
        std::ignore = co_await scheduler_.clock().sleep_for(Milliseconds{1});
        continue;
      }
      if (!co_await checkpoint(Stage::kSink, Point::kRead, partition)) {
        offset = broker_.offset("sink", "delivered", partition);
        continue;
      }
      const auto message = read_record("delivered", partition, offset);
      auto transaction = sink_.begin();
      std::ignore = Store::apply(transaction, message, false);
      if (!co_await checkpoint(Stage::kSink, Point::kBeforeCommit, partition)) {
        offset = broker_.offset("sink", "delivered", partition);
        continue;
      }
      if (!sink_.commit(std::move(transaction))) {
        continue;
      }
      record(std::format("sink commit partition={} id={}", partition, message.id));
      if (!co_await checkpoint(Stage::kSink, Point::kAfterCommit, partition)) {
        offset = broker_.offset("sink", "delivered", partition);
        continue;
      }
      auto ack = broker_.begin(std::format("sink-{}", partition));
      ack.offsets.push_back({.group = "sink", .topic = "delivered", .partition = partition, .next = ++offset});
      if (!broker_.commit(ack)) {
        throw std::runtime_error("sink offset commit failed");
      }
    }
  }

  void reconcile() {
    std::map<MessageKey, Message> normalized;
    for (std::size_t partition = 0; partition < options_.partitions; ++partition) {
      for (std::size_t offset = 0; offset < broker_.size("normalized", partition); ++offset) {
        const auto message = read_record("normalized", partition, offset);
        if (!normalized.emplace(key_of(message), message).second) {
          throw std::runtime_error("oracle: duplicate transactional output");
        }
        ++report_.normalized;
      }
      for (const auto& [group, topic] :
           {std::pair{"normalizer", "raw"}, {"database", "normalized"}, {"sink", "delivered"}}) {
        if (broker_.offset(group, topic, partition) != broker_.size(topic, partition)) {
          throw std::runtime_error("oracle: unconsumed durable records");
        }
      }
    }
    if (normalized != expected_ || database_.effects() != expected_ || sink_.effects() != expected_) {
      throw std::runtime_error("oracle: missing, unexpected or corrupted message/effect");
    }
    if (database_.pending()) {
      throw std::runtime_error("oracle: unacknowledged outbox");
    }
    record(std::format("oracle ok expected={} normalized={} delivered={}", expected_.size(), normalized.size(),
                       sink_.effects().size()));
  }

  Options options_;
  Random random_;
  FaultScript script_;
  Transform transform_;
  Report report_;
  Broker broker_;
  Store database_;
  Store sink_;
  std::map<MessageKey, Message> expected_;
  std::vector<bool> kafka_done_;
  std::vector<bool> database_done_;
  bool relay_done_ = false;
  std::size_t decisions_ = 0;
  // Destroy suspended actors before the models they reference, including exceptional exits.
  Scheduler scheduler_;
};

}  // namespace

Report run_pipeline(Options options, FaultScript script, Transform transform) {
  return Pipeline(options, std::move(script), std::move(transform)).run();
}

}  // namespace psim::sim
