#pragma once

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/s3/client.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::ctl {
namespace p = platform;
namespace asio = boost::asio;
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Await = asio::awaitable<Json>;
using Param = p::pg::Parameter;

class OperationError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

void require(bool condition, std::string_view message);

template <class T>
T take(p::Result<T> result) {
  if (!result) {
    throw OperationError(std::string(result.error().name()));
  }
  if constexpr (!std::is_void_v<T>) {
    return std::move(*result);
  }
}

double seconds();
double recovery_time(std::string_view value);
std::string read_file(const fs::path& path, std::size_t maximum = static_cast<std::size_t>(16 * 1024 * 1024));
Json read_json(const fs::path& path);
void write_file(const fs::path& path, std::string_view body, bool replace = false);
void write_json(const fs::path& path, const Json& value);
void sync_file(const fs::path& path);
void append_file(const fs::path& path, std::string_view body);
std::string file_hash(const fs::path& path);
std::string secret(const Json& config, const std::string& name);
p::security::TlsOptions tls(const Json& config);
p::pg::ConnectionOptions postgres(const Json& config);
p::kafka::ClientOptions kafka(const Json& config);
p::s3::Options storage(const Json& config);
void process(const std::vector<std::string>& arguments, const std::map<std::string, std::string>& environment = {},
             const fs::path& input = {}, const fs::path& output = {});
std::map<std::string, std::string> pg_environment(const Json& config);

class Temporary {
 public:
  Temporary();
  ~Temporary();
  Temporary(const Temporary&) = delete;
  Temporary& operator=(const Temporary&) = delete;
  Temporary(Temporary&&) = delete;
  Temporary& operator=(Temporary&&) = delete;
  [[nodiscard]] fs::path path(std::string_view name) const;

 private:
  fs::path directory_;
};

class FileLock {
 public:
  explicit FileLock(const fs::path& path, bool wait = false);
  ~FileLock();
  FileLock(const FileLock&) = delete;
  FileLock& operator=(const FileLock&) = delete;
  FileLock(FileLock&&) = delete;
  FileLock& operator=(FileLock&&) = delete;

 private:
  int descriptor_;
};

struct Arguments {
  std::vector<std::string> words;
  std::map<std::string, std::vector<std::string>> flags;
  explicit Arguments(std::span<char*> values);
  [[nodiscard]] std::string one(const std::string& key, std::string fallback = {}) const;
  [[nodiscard]] bool has(const std::string& key) const;
  [[nodiscard]] std::int64_t number(const std::string& key) const;
};

struct Context {
  Json config;
  fs::path config_path;
  p::async::BlockingPool blocking{4, 32};
};

Await backup(Context* context, Arguments args);
Await kafka_operation(Context* context, Arguments args);
Await verify_plans(Context* context);
Await api(Context* context, std::string path, Json body = nullptr, std::string request_id = {});
asio::awaitable<void> export_topics(Context* context, fs::path destination);
Await import_topics(Context* context, fs::path source);
void audit(const Context& context, const Arguments& args, std::string_view outcome, std::string_view id);
void backup_status(const Context& context, const Json& updates = Json::object());
}  // namespace psim::ctl
