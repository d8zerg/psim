#include "psim/sim/broker.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace psim::sim {

void Broker::create_topic(std::string topic, std::size_t partitions) {
  if (partitions == 0 || logs_.contains(topic)) {
    throw std::invalid_argument("topic must be new and have partitions");
  }
  logs_.emplace(std::move(topic), std::vector<std::vector<Message>>(partitions));
}

void Broker::append(const std::string& topic, std::size_t partition, Message message) {
  logs_.at(topic).at(partition).push_back(std::move(message));
}

std::optional<Message> Broker::read(const std::string& topic, std::size_t partition, std::size_t offset) const {
  const auto& log = logs_.at(topic).at(partition);
  if (offset >= log.size()) {
    return std::nullopt;
  }
  return log.at(offset);
}

std::size_t Broker::size(const std::string& topic, std::size_t partition) const {
  return logs_.at(topic).at(partition).size();
}

std::size_t Broker::offset(const std::string& group, const std::string& topic, std::size_t partition) const {
  std::ignore = logs_.at(topic).at(partition);
  const auto found = offsets_.find({group, topic, partition});
  return found == offsets_.end() ? 0 : found->second;
}

KafkaTransaction Broker::begin(std::string producer) {
  const auto epoch = ++epochs_[producer];
  return {.producer = std::move(producer), .epoch = epoch, .publications = {}, .offsets = {}};
}

bool Broker::commit(const KafkaTransaction& transaction) {
  const auto producer = epochs_.find(transaction.producer);
  if (producer == epochs_.end() || producer->second != transaction.epoch) {
    return false;
  }
  // Validate and stage a snapshot first: even an invalid final offset leaves no partial output.
  auto logs = logs_;
  auto offsets = offsets_;
  for (const auto& publication : transaction.publications) {
    logs.at(publication.topic).at(publication.partition).push_back(publication.message);
  }
  for (const auto& commit : transaction.offsets) {
    auto& next = offsets[{commit.group, commit.topic, commit.partition}];
    if (commit.next < next || commit.next > logs_.at(commit.topic).at(commit.partition).size()) {
      return false;
    }
    next = commit.next;
  }
  logs_.swap(logs);
  offsets_.swap(offsets);
  // A transaction can commit at most once; another begin also fences older handles.
  ++producer->second;
  return true;
}

}  // namespace psim::sim
