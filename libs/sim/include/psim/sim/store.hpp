#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "psim/sim/broker.hpp"

namespace psim::sim {

struct OutboxEntry {
  Message message;
  bool sent = false;
};

struct StoreTransaction {
  std::uint64_t version;
  std::map<MessageKey, Message> inbox;
  std::vector<OutboxEntry> outbox;
};

/// Durable inbox, observable effect, and outbox in one optimistic transaction.
/// Transactions abandoned by a crashed actor have no effects; committed state survives restart.
class Store {
 public:
  [[nodiscard]] StoreTransaction begin() const;
  /// Dedupe within the tenant. Returns false if this message has already been applied.
  [[nodiscard]] static bool apply(StoreTransaction& transaction, Message message, bool publish = true);
  [[nodiscard]] bool commit(StoreTransaction transaction);
  [[nodiscard]] std::optional<std::size_t> pending() const;
  [[nodiscard]] const OutboxEntry& entry(std::size_t index) const;
  void acknowledge(std::size_t index);

  [[nodiscard]] const std::map<MessageKey, Message>& effects() const noexcept { return inbox_; }

 private:
  std::uint64_t version_ = 0;
  std::map<MessageKey, Message> inbox_;
  std::vector<OutboxEntry> outbox_;
};

}  // namespace psim::sim
