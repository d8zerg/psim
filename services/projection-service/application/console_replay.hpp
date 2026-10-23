#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "console_store.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/pg/pool.hpp"

namespace psim::projection {
inline constexpr std::int64_t kConsoleWriterLock = 590073;
std::vector<std::string> console_topics();
boost::asio::awaitable<platform::Result<void>> console_catch_up(ConsoleStore* store, platform::kafka::Consumer* source,
                                                                platform::kafka::Offsets targets,
                                                                std::chrono::milliseconds timeout);
boost::asio::awaitable<platform::Result<std::string>> console_rebuild(platform::pg::Pool* pool,
                                                                      platform::kafka::ClientOptions options,
                                                                      platform::kafka::Dependencies dependencies,
                                                                      std::chrono::milliseconds timeout);
}  // namespace psim::projection
