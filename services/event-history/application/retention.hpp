#pragma once
#include <cstddef>

#include "facts.hpp"
#include "psim/platform/pg/store.hpp"

namespace psim::event_history {
boost::asio::awaitable<p::Result<void>> initialize_history(p::pg::Pool* pool);

class Retention {
 public:
  Retention(p::pg::Pool* journal, p::clickhouse::Pool* destination) : journal_(journal), destination_(destination) {}

  boost::asio::awaitable<p::Result<void>> accept(processing::v1::SignalRecord record, std::int64_t seconds);
  boost::asio::awaitable<p::Result<void>> tick();

 private:
  std::size_t batch_size_ = 128;
  p::pg::Pool* journal_;
  p::clickhouse::Pool* destination_;
};
}  // namespace psim::event_history
