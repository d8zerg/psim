#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "psim/platform/kafka/consumer.hpp"

namespace psim::platform::stream {
namespace asio = boost::asio;

/// A private read-committed replica. Bootstrap reaches captured broker ends, including
/// control records and compacted gaps; an unresolved transaction fails the bounded bootstrap.
class Replica {
 public:
  using Apply = std::function<Result<void>(const kafka::Message&)>;
  static asio::awaitable<Result<std::unique_ptr<Replica>>> open(asio::any_io_executor executor,
                                                                kafka::ClientOptions options,
                                                                kafka::Dependencies dependencies,
                                                                std::vector<std::string> topics, Apply apply,
                                                                std::vector<std::int32_t> partitions = {});
  asio::awaitable<Result<void>> update();
  asio::awaitable<void> close();

 private:
  Replica(std::unique_ptr<kafka::Consumer> source, Apply apply);
  asio::awaitable<Result<void>> poll(std::chrono::milliseconds wait);
  std::unique_ptr<kafka::Consumer> source_;
  Apply apply_;
};
}  // namespace psim::platform::stream
