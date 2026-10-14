#include "psim/sim/store.hpp"

#include <cstddef>
#include <optional>
#include <utility>

#include "psim/sim/broker.hpp"

namespace psim::sim {

StoreTransaction Store::begin() const {
  return {.version = version_, .inbox = inbox_, .outbox = outbox_};
}

bool Store::apply(StoreTransaction& transaction, Message message, bool publish) {
  if (!transaction.inbox.emplace(key_of(message), message).second) {
    return false;
  }
  if (publish) {
    transaction.outbox.push_back({.message = std::move(message)});
  }
  return true;
}

bool Store::commit(StoreTransaction transaction) {
  if (transaction.version != version_) {
    return false;
  }
  inbox_ = std::move(transaction.inbox);
  outbox_ = std::move(transaction.outbox);
  ++version_;
  return true;
}

std::optional<std::size_t> Store::pending() const {
  for (std::size_t index = 0; index < outbox_.size(); ++index) {
    if (!outbox_.at(index).sent) {
      return index;
    }
  }
  return std::nullopt;
}

const OutboxEntry& Store::entry(std::size_t index) const {
  return outbox_.at(index);
}

void Store::acknowledge(std::size_t index) {
  outbox_.at(index).sent = true;
  ++version_;
}

}  // namespace psim::sim
