#pragma once
#include <map>
#include <span>
#include <string>
#include <string_view>

#include "common.hpp"

namespace psim::monitoring {
int run(std::span<char*> arguments);
using Labels = std::map<std::string, std::string>;
std::string sample(std::string_view name, double value, const Labels& labels = {});
std::string backup_metrics(const fs::path& path, const Json& expected);
asio::awaitable<std::string> monitor_kafka(Json config, p::async::BlockingPool* blocking);
}  // namespace psim::monitoring
