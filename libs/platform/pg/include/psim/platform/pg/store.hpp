#pragma once

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/transaction.hpp"

namespace psim::platform::async {
class Clock;
}

namespace psim::platform::kafka {
class Producer;
}

namespace psim::platform::pg {
struct OutboxMessage {
  std::string id;
  kafka::Record record;
};

/// Inbox and outbox share the transaction of a service's repository changes (ADR-008). The
/// consumer acknowledges Kafka only after this transaction commits. A repeated identity with
/// a different fingerprint is rejected rather than silently treated as a duplicate.
class MessageStore {
 public:
  explicit MessageStore(std::string schema) : schema_(std::move(schema)) {}

  asio::awaitable<Result<void>> initialize(Connection* connection);
  asio::awaitable<Result<bool>> accept(Transaction* transaction, std::string consumer, std::string id,
                                       std::string fingerprint);
  asio::awaitable<Result<void>> append(Transaction* transaction, OutboxMessage message);

  [[nodiscard]] const std::string& schema() const noexcept { return schema_; }

 private:
  std::string schema_;
};

/// Publisher succeeds only after durable acknowledgement of every message in the batch.
using Publisher = std::function<asio::awaitable<Result<void>>(std::span<const OutboxMessage>)>;
/// Use a dedicated idempotent producer with an empty transactional_id.
[[nodiscard]] Publisher kafka_publisher(kafka::Producer* producer, Duration timeout = Duration{30000});

/// Transaction-locked relay leader per service schema. A failed publish or COMMIT leaves rows
/// pending. A crash after broker acknowledgement can repeat the batch: delivery is at least
/// once; the downstream inbox provides one committed effect per event identity.
class OutboxRelay {
 public:
  OutboxRelay(Pool* pool, MessageStore store, Publisher publisher, std::size_t batch_size = 100)
      : pool_(pool), store_(std::move(store)), publisher_(std::move(publisher)), batch_size_(batch_size) {}

  asio::awaitable<Result<std::size_t>> drain_once();
  /// Polling is authoritative; LISTEN/NOTIFY can be an optimization, never the only trigger.
  asio::awaitable<Result<void>> run(async::Clock* clock, Duration interval = Duration{100});

 private:
  Pool* pool_;
  MessageStore store_;
  Publisher publisher_;
  std::size_t batch_size_;
};
}  // namespace psim::platform::pg
