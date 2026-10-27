#include "common.hpp"

#include <fcntl.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <signal.h>  // NOLINT(modernize-deprecated-headers): POSIX kill
#include <spawn.h>
#include <stdlib.h>  // NOLINT(modernize-deprecated-headers): POSIX process status macros
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "psim/platform/files.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/s3/client.hpp"
#include "psim/platform/security/secret.hpp"
#include "psim/platform/security/tls.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::ctl {
namespace {
int open_private(const fs::path& path, int flags) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX file creation with an explicit private mode
  return ::open(path.c_str(), flags, 0600);
}
}  // namespace

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw OperationError(std::string(message));
  }
}

double seconds() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

double recovery_time(std::string_view value) {
  require((value.size() == 20 || value.size() == 27) && value[4] == '-' && value[7] == '-' && value[10] == 'T'
              && value[13] == ':' && value[16] == ':' && value.back() == 'Z',
          "recovery time must be UTC with seconds or microseconds");
  const auto number = [&value](std::size_t position, std::size_t length) {
    const auto part = value.substr(position, length);
    unsigned int result = 0;
    const auto* const end = std::to_address(part.end());
    const auto parsed = std::from_chars(std::to_address(part.begin()), end, result);
    require(parsed.ec == std::errc{} && parsed.ptr == end, "invalid recovery time");
    return result;
  };
  const std::chrono::year_month_day day{std::chrono::year{static_cast<int>(number(0, 4))},
                                        std::chrono::month{number(5, 2)}, std::chrono::day{number(8, 2)}};
  const auto hour = number(11, 2);
  const auto minute = number(14, 2);
  const auto second = number(17, 2);
  require(day.ok() && hour < 24 && minute < 60 && second < 60 && (value.size() == 20 || value[19] == '.'),
          "invalid recovery time");
  const auto point = std::chrono::sys_days(day) + std::chrono::hours(hour) + std::chrono::minutes(minute)
                     + std::chrono::seconds(second) + std::chrono::microseconds(value.size() == 27 ? number(20, 6) : 0);
  return std::chrono::duration<double>(point.time_since_epoch()).count();
}

std::string read_file(const fs::path& path, std::size_t maximum) {
  return take(p::files::read(path, maximum));
}

Json read_json(const fs::path& path) {
  return Json::parse(read_file(path));
}

void write_file(const fs::path& path, std::string_view body, bool replace) {
  const auto temporary = path.string() + "." + p::stream::uuid7() + ".part";
  const int fd = open_private(
      temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW);  // NOLINT(cppcoreguidelines-pro-type-vararg)
  require(fd >= 0, "cannot create output file");
  std::size_t offset = 0;
  bool complete = true;
  while (offset < body.size()) {
    const auto count = ::write(fd, body.substr(offset).data(), body.size() - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      complete = false;
      break;
    }
    offset += static_cast<std::size_t>(count);
  }
  complete = complete && ::fsync(fd) == 0;
  (void)::close(fd);
  if (complete) {
    if (replace) {
      complete = ::rename(temporary.c_str(), path.c_str()) == 0;
    } else {
      complete = ::link(temporary.c_str(), path.c_str()) == 0;
    }
  }
  (void)::unlink(temporary.c_str());
  require(complete, "atomic output publication failed");
  sync_file(path.parent_path().empty() ? fs::path(".") : path.parent_path());
}

void sync_file(const fs::path& path) {
  take(p::files::sync(path));
}

void append_file(const fs::path& path, std::string_view body) {
  take(p::files::append(path, body));
}

void write_json(const fs::path& path, const Json& value) {
  write_file(path, value.dump() + "\n", true);
}

std::string file_hash(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  require(input.good(), "cannot hash input file");
  const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  require(hash && EVP_DigestInit_ex(hash.get(), EVP_sha256(), nullptr) == 1, "digest initialization failed");
  std::array<char, 65536> block{};
  while (input) {
    input.read(block.data(), static_cast<std::streamsize>(block.size()));
    require(EVP_DigestUpdate(hash.get(), block.data(), static_cast<std::size_t>(input.gcount())) == 1,
            "digest update failed");
  }
  require(input.eof(), "input hashing failed");
  std::array<unsigned char, EVP_MAX_MD_SIZE> output{};
  unsigned int size = 0;
  require(EVP_DigestFinal_ex(hash.get(), output.data(), &size) == 1, "digest finalization failed");
  std::ostringstream text;
  for (unsigned int i = 0; i < size; ++i) {
    text << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(output.at(i));
  }
  return text.str();
}

std::string secret(const Json& config, const std::string& name) {
  return std::string(take(p::security::read_secret(config.at(name).get<std::string>())).view());
}

p::security::TlsOptions tls(const Json& config) {
  return {.ca_file = config.value("ca_file", ""),
          .certificate_file = config.value("certificate_file", ""),
          .private_key_file = config.value("key_file", "")};
}

p::pg::ConnectionOptions postgres(const Json& config) {
  return {.host = config.at("host"),
          .port = config.value("port", "5432"),
          .database = config.value("database", "psim"),
          .user = config.at("user"),
          .password = config.contains("password_file") ? secret(config, "password_file") : "",
          .properties = {{"sslmode", config.value("allow_plaintext", false) ? "disable" : "verify-full"},
                         {"target_session_attrs", "any"},
                         {"sslrootcert", config.value("ca_file", "")},
                         {"sslcert", config.value("certificate_file", "")},
                         {"sslkey", config.value("key_file", "")}}};
}

p::kafka::ClientOptions kafka(const Json& config) {
  p::kafka::ClientOptions options{.bootstrap = config.at("brokers"),
                                  .client_id = "psimctl",
                                  .allow_plaintext = config.value("allow_plaintext", false)};
  if (!options.allow_plaintext) {
    options.security = p::kafka::TransportSecurity{.tls = tls(config)};
  }
  return options;
}

p::s3::Options storage(const Json& config) {
  return {.endpoint = config.at("endpoint"),
          .region = config.value("region", "us-east-1"),
          .bucket = config.at("bucket"),
          .access_key_file = config.at("access_key_file"),
          .secret_key_file = config.at("secret_key_file"),
          .tls = tls(config),
          .allow_plaintext = config.value("allow_plaintext", false),
          .timeout = std::chrono::minutes(5)};
}

void process(const std::vector<std::string>& arguments, const std::map<std::string, std::string>& environment,
             const fs::path& input, const fs::path& output) {
  require(!arguments.empty() && fs::path(arguments.front()).is_absolute(), "absolute executable path required");
  std::vector<std::string> values = arguments;
  std::vector<char*> argv;
  argv.reserve(values.size());
  for (auto& value : values) {
    argv.push_back(value.data());
  }
  argv.push_back(nullptr);
  std::vector<std::string> entries{"PATH=/usr/bin:/bin", "LANG=C"};
  for (const auto& [key, value] : environment) {
    auto entry = key;
    entry += "=";
    entry += value;
    entries.push_back(std::move(entry));
  }
  std::vector<char*> env;
  env.reserve(entries.size());
  for (auto& value : entries) {
    env.push_back(value.data());
  }
  env.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  require(posix_spawn_file_actions_init(&actions) == 0, "cannot initialize child process");
  const auto in = input.empty() ? fs::path("/dev/null") : input;
  const auto out = output.empty() ? fs::path("/dev/null") : output;
  bool valid = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, in.c_str(), O_RDONLY, 0) == 0;
  valid = valid
          && posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600)
                 == 0;
  valid = valid && posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) == 0;
  pid_t child = 0;
  const int result = valid ? ::posix_spawn(&child, argv.front(), &actions, nullptr, argv.data(), env.data()) : -1;
  (void)posix_spawn_file_actions_destroy(&actions);
  require(result == 0, "cannot start native utility: " + fs::path(arguments.front()).filename().string());
  int status = 0;
  pid_t waited = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(1);
  while (true) {
    waited = ::waitpid(child, &status, WNOHANG);
    if (waited == 0 && std::chrono::steady_clock::now() >= deadline) {
      (void)::kill(child, SIGKILL);
      waited = ::waitpid(child, &status, 0);
      while (waited < 0 && errno == EINTR) {
        waited = ::waitpid(child, &status, 0);
      }
      require(false, "native utility exceeded its deadline");
    }
    if (waited == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (waited != 0 && (waited >= 0 || errno != EINTR)) {
      break;
    }
  }
  require(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "native utility failed: " + fs::path(arguments.front()).filename().string());
}

std::map<std::string, std::string> pg_environment(const Json& config) {
  auto options = postgres(config);
  return {{"PGHOST", options.host},
          {"PGPORT", options.port},
          {"PGDATABASE", options.database},
          {"PGUSER", options.user},
          {"PGPASSWORD", options.password},
          {"PGSSLMODE", options.properties.at("sslmode")},
          {"PGSSLROOTCERT", options.properties.at("sslrootcert")},
          {"PGSSLCERT", options.properties.at("sslcert")},
          {"PGSSLKEY", options.properties.at("sslkey")},
          {"PGCONNECT_TIMEOUT", "10"}};
}

Temporary::Temporary() {
  directory_ = fs::temp_directory_path() / ("psimctl-" + p::stream::uuid7());
  require(fs::create_directory(directory_), "cannot create temporary directory");
  fs::permissions(directory_, fs::perms::owner_all);
}

Temporary::~Temporary() {
  std::error_code error;
  fs::remove_all(directory_, error);
}

fs::path Temporary::path(std::string_view name) const {
  return directory_ / name;
}

FileLock::FileLock(const fs::path& path, bool wait)
    : descriptor_(
          open_private(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW)) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
  if (descriptor_ < 0 || ::flock(descriptor_, LOCK_EX | (wait ? 0 : LOCK_NB)) != 0) {
    if (descriptor_ >= 0) {
      (void)::close(descriptor_);
    }
    throw std::runtime_error("operation lock unavailable");
  }
}

FileLock::~FileLock() {
  (void)::close(descriptor_);
}

Arguments::Arguments(std::span<char*> values) {
  for (std::size_t i = 0; i < values.size(); ++i) {
    const std::string value(values[i]);
    if (value.starts_with("--")) {
      auto& list = flags[value];
      if (i + 1 < values.size() && !std::string_view(values[i + 1]).starts_with("--")) {
        list.emplace_back(values[++i]);
      }
    } else {
      words.push_back(value);
    }
  }
}

std::string Arguments::one(const std::string& key, std::string fallback) const {
  const auto found = flags.find(key);
  if (found == flags.end()) {
    return fallback;
  }
  require(found->second.size() == 1, "option requires one value");
  return found->second.front();
}

bool Arguments::has(const std::string& key) const {
  return flags.contains(key);
}

std::int64_t Arguments::number(const std::string& key) const {
  const auto text = one(key);
  std::int64_t value = 0;
  const auto* const end = std::to_address(text.end());
  const auto parsed = std::from_chars(text.data(), end, value);
  require(parsed.ec == std::errc{} && parsed.ptr == end, "invalid integer option");
  return value;
}

void audit(const Context& context, const Arguments& args, std::string_view outcome, std::string_view id) {
  const fs::path directory(context.config.at("state_directory").get<std::string>());
  fs::create_directories(directory);
  fs::permissions(directory, fs::perms::owner_all);
  const Json entry{{"id", id},
                   {"actor", context.config.at("actor")},
                   {"uid", ::getuid()},
                   {"deployment", context.config.at("deployment")},
                   {"command", args.words},
                   {"options", args.flags},
                   {"outcome", outcome},
                   {"at_seconds", seconds()}};
  append_file(directory / "operations.jsonl", entry.dump() + "\n");
}
}  // namespace psim::ctl
