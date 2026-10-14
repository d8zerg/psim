#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace psim::sim {

struct Message {
  std::string tenant;
  std::string id;
  std::string payload;
  bool operator==(const Message&) const = default;
};

using MessageKey = std::pair<std::string, std::string>;

[[nodiscard]] inline MessageKey key_of(const Message& message) {
  return {message.tenant, message.id};
}

struct Publication {
  std::string topic;
  std::size_t partition;
  Message message;
};

struct CommitOffset {
  std::string group;
  std::string topic;
  std::size_t partition;
  std::size_t next;
};

struct KafkaTransaction {
  std::string producer;
  std::uint64_t epoch;
  std::vector<Publication> publications;
  std::vector<CommitOffset> offsets;
};

/// Durable partition logs and read_committed transactions, including producer fencing.
/// Transport faults belong to the scheduler/actor: a lost response never erases durable data.
/// Models delivery semantics, not the Kafka wire protocol or broker replication.
class Broker {
 public:
  void create_topic(std::string topic, std::size_t partitions);
  void append(const std::string& topic, std::size_t partition, Message message);
  [[nodiscard]] std::optional<Message> read(const std::string& topic, std::size_t partition, std::size_t offset) const;
  [[nodiscard]] std::size_t size(const std::string& topic, std::size_t partition) const;
  [[nodiscard]] std::size_t offset(const std::string& group, const std::string& topic, std::size_t partition) const;
  [[nodiscard]] KafkaTransaction begin(std::string producer);
  /// All records and offsets become visible together. Stale epochs/regressing offsets fail.
  [[nodiscard]] bool commit(const KafkaTransaction& transaction);

 private:
  using Logs = std::map<std::string, std::vector<std::vector<Message>>>;
  using Offsets = std::map<std::tuple<std::string, std::string, std::size_t>, std::size_t>;
  Logs logs_;
  Offsets offsets_;
  std::map<std::string, std::uint64_t> epochs_;
};

}  // namespace psim::sim
