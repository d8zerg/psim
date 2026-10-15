#include "store.hpp"

#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/time_generator_v7.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <fcntl.h>
#include <google/protobuf/message.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <openssl/evp.h>
#include <psim/connector/v1/connector.pb.h>
#include <stdio.h>  // NOLINT(modernize-deprecated-headers): POSIX renameat needs the C declaration
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/connector/client.hpp"

namespace psim::platform::connector::detail {
namespace {
std::string digest(std::string_view data) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> hash{};
  unsigned int size = 0;
  if (EVP_Digest(data.data(), data.size(), hash.data(), &size, EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("buffer checksum failed");
  }
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string out;
  for (unsigned int i = 0; i < size; ++i) {
    out += kHex[hash[i] >> 4U];
    out += kHex[hash[i] & 15U];
  }
  return out;
}

std::string frame(const nlohmann::json& value) {
  const auto text = value.dump();
  return digest(text) + " " + text + "\n";
}

void write_all(int file, std::string_view data) {
  while (!data.empty()) {
    const auto count = ::write(file, data.data(), data.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      throw std::runtime_error("buffer write failed; directory must be reopened");
    }
    data.remove_prefix(static_cast<std::size_t>(count));
  }
}

void sync(int file) {
  if (::fsync(file) != 0) {
    throw std::runtime_error("buffer sync failed; directory must be reopened");
  }
}

void close_fd(int& file) {
  if (file >= 0) {
    (void)::close(file);
    file = -1;
  }
}
}  // namespace

std::string uuid7() {
  static std::mutex mutex;
  static boost::uuids::time_generator_v7 generator;
  const std::scoped_lock lock(mutex);
  return boost::uuids::to_string(generator());
}

bool uuid(std::string_view text) {
  try {
    return text.size() == 36 && boost::uuids::to_string(boost::uuids::string_generator()(std::string(text))) == text;
  } catch (const std::exception&) {
    return false;
  }
}

nlohmann::json encode(const google::protobuf::Message& message) {
  std::string text;
  if (!google::protobuf::util::MessageToJsonString(message, &text).ok()) {
    throw std::runtime_error("cannot encode buffer record");
  }
  return nlohmann::json::parse(text);
}

void decode(const nlohmann::json& json, google::protobuf::Message* message) {
  if (!google::protobuf::util::JsonStringToMessage(json.dump(), message).ok()) {
    throw std::runtime_error("invalid buffer record");
  }
}

Store::Store(Options options) : options_(std::move(options)) {
  if (options_.buffer_directory.empty() || options_.max_buffer_bytes < 16384 || options_.max_pending_events == 0
      || options_.max_commands == 0) {
    throw std::invalid_argument("invalid buffer bounds");
  }
  try {
    const auto path = std::filesystem::absolute(options_.buffer_directory).lexically_normal();
    if (path == path.root_path()) {
      throw std::invalid_argument("buffer must use a dedicated directory");
    }
    std::vector<std::filesystem::path> parents;
    auto entry = path;
    while (entry != entry.root_path()) {
      parents.push_back(entry.parent_path());
      if (std::filesystem::exists(entry.parent_path())) {
        break;
      }
      entry = entry.parent_path();
    }
    std::filesystem::create_directories(options_.buffer_directory);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open flags and mode have fixed types
    directory_ = ::open(options_.buffer_directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat metadata{};
    if (directory_ < 0 || ::fstat(directory_, &metadata) != 0 || metadata.st_uid != ::geteuid()
        || ::fchmod(directory_, 0700) != 0) {
      throw std::runtime_error("buffer directory is not privately owned");
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open flags and mode have fixed types
    lock_ = ::openat(directory_, "lock", O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_ < 0 || ::flock(lock_, LOCK_EX | LOCK_NB) != 0) {
      throw std::runtime_error("buffer directory already in use");
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open flags and mode have fixed types
    file_ = ::openat(directory_, "journal", O_CREAT | O_RDWR | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (file_ < 0 || ::fstat(file_, &metadata) != 0 || !S_ISREG(metadata.st_mode) || metadata.st_nlink != 1
        || metadata.st_uid != ::geteuid() || ::fchmod(file_, 0600) != 0) {
      throw std::runtime_error("buffer journal is not a private regular file");
    }
    sync(directory_);
    for (const auto& parent : parents) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX directory open has fixed flags
      int ancestor = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
      if (ancestor < 0) {
        throw std::runtime_error("cannot sync buffer parent directory");
      }
      try {
        sync(ancestor);
      } catch (...) {
        close_fd(ancestor);
        throw;
      }
      close_fd(ancestor);
    }
    recover();
  } catch (...) {
    close_fd(file_);
    close_fd(lock_);
    close_fd(directory_);
    throw;
  }
}

Store::~Store() {
  close_fd(file_);
  close_fd(lock_);
  close_fd(directory_);
}

void Store::recover() {
  struct stat metadata{};
  if (::fstat(file_, &metadata) != 0 || metadata.st_size < 0
      || std::cmp_greater(metadata.st_size, options_.max_buffer_bytes)) {
    throw std::runtime_error("buffer exceeds configured disk bound");
  }
  std::string data(static_cast<std::size_t>(metadata.st_size), '\0');
  std::size_t read = 0;
  while (read < data.size()) {
    const auto size =
        ::pread(file_, std::span(data).subspan(read).data(), data.size() - read, static_cast<off_t>(read));
    if (size < 0 && errno == EINTR) {
      continue;
    }
    if (size <= 0) {
      throw std::runtime_error("cannot recover buffer journal");
    }
    read += static_cast<std::size_t>(size);
  }
  std::size_t offset = 0;
  for (;;) {
    const auto end = data.find('\n', offset);
    if (end == std::string::npos) {
      break;
    }
    const auto line = std::string_view(data).substr(offset, end - offset);
    if (line.size() < 66 || line[64] != ' ' || digest(line.substr(65)) != line.substr(0, 64)) {
      throw std::runtime_error("buffer checksum mismatch; refusing silent data loss");
    }
    apply(nlohmann::json::parse(line.substr(65)));
    offset = end + 1;
  }
  if (offset != data.size()) {
    if (::ftruncate(file_, static_cast<off_t>(offset)) != 0) {
      throw std::runtime_error("cannot remove incomplete buffer tail");
    }
    sync(file_);
  }
  bytes_ = offset;
  if (events_.size() > options_.max_pending_events || commands_.size() > options_.max_commands) {
    throw std::runtime_error("recovered buffer exceeds configured record bound");
  }
}

void Store::apply(const nlohmann::json& operation) {
  if (operation.contains("snapshot")) {
    events_.clear();
    order_.clear();
    cursors_.clear();
    commands_.clear();
    for (const auto& item : operation.at("snapshot")) {
      apply(item);
    }
  } else if (operation.contains("event")) {
    wire::ConnectorEvent event;
    decode(operation.at("event"), &event);
    events_[event.event_id()] = event;
    order_.push_back(event.event_id());
    auto& cursor = cursors_[event.source_ref()];
    cursor.set_source_ref(event.source_ref());
    cursor.set_source_epoch(event.source_epoch());
    cursor.set_source_seq(event.source_seq());
  } else if (operation.contains("ack")) {
    for (const auto& id : operation.at("ack")) {
      events_.erase(id.get<std::string>());
    }
    while (!order_.empty() && !events_.contains(order_.front())) {
      order_.pop_front();
    }
  } else if (operation.contains("cursor")) {
    wire::SourceCursor cursor;
    decode(operation.at("cursor"), &cursor);
    cursors_[cursor.source_ref()] = cursor;
  } else if (operation.contains("command")) {
    commands_[operation.at("id").get<std::string>()] = operation;
  } else if (operation.contains("result")) {
    commands_.at(operation.at("id").get<std::string>())["result"] = operation.at("result");
  } else {
    throw std::runtime_error("unknown buffer operation");
  }
}

nlohmann::json Store::snapshot() const {
  auto items = nlohmann::json::array();
  for (const auto& id : order_) {
    if (!events_.contains(id)) {
      continue;
    }
    items.push_back({{"event", encode(events_.at(id))}});
  }
  for (const auto& [source, cursor] : cursors_) {
    items.push_back({{"cursor", encode(cursor)}});
  }
  for (const auto& [id, command] : commands_) {
    items.push_back(command);
  }
  return {{"snapshot", items}};
}

void Store::compact() {
  const auto data = frame(snapshot());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open flags and mode have fixed types
  int replacement = ::openat(directory_, "compact", O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (replacement < 0) {
    throw std::runtime_error("cannot compact buffer");
  }
  try {
    write_all(replacement, data);
    sync(replacement);
    close_fd(replacement);
    if (::renameat(directory_, "compact", directory_, "journal") != 0) {
      throw std::runtime_error("cannot replace compacted buffer");
    }
    sync(directory_);
    close_fd(file_);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open flags and mode have fixed types
    file_ = ::openat(directory_, "journal", O_RDWR | O_APPEND | O_CLOEXEC | O_NOFOLLOW);
    if (file_ < 0) {
      throw std::runtime_error("cannot reopen compacted buffer");
    }
    bytes_ = data.size();
  } catch (...) {
    close_fd(replacement);
    failed_ = true;
    throw;
  }
}

void Store::record(const nlohmann::json& operation, bool admission) {
  if (failed_) {
    throw std::runtime_error("buffer must be reopened after storage failure");
  }
  const auto data = frame(operation);
  const std::size_t reserve = admission ? 4096 : 0;
  if (bytes_ + data.size() + reserve > options_.max_buffer_bytes) {
    compact();
  }
  if (bytes_ + data.size() + reserve > options_.max_buffer_bytes) {
    throw std::runtime_error("buffer full; apply backpressure to the source");
  }
  try {
    write_all(file_, data);
    sync(file_);
    bytes_ += data.size();
    apply(operation);
  } catch (...) {
    failed_ = true;
    throw;
  }
}

wire::ConnectorEvent Store::append(wire::ConnectorEvent event) {
  const std::scoped_lock lock(mutex_);
  if (events_.size() >= options_.max_pending_events || event.source_ref().empty() || event.source_ref().size() > 256
      || event.raw_code().empty() || event.raw_code().size() > 128 || event.attributes_size() > 64) {
    throw std::invalid_argument("invalid event or event buffer full");
  }
  const auto previous = cursors_.find(event.source_ref());
  if (event.source_epoch() == 0) {
    event.set_source_epoch(previous == cursors_.end() ? 1 : previous->second.source_epoch());
  }
  if (previous != cursors_.end() && event.source_epoch() < previous->second.source_epoch()) {
    throw std::invalid_argument("source epoch must not decrease");
  }
  const auto sequence = previous != cursors_.end() && event.source_epoch() == previous->second.source_epoch()
                            ? previous->second.source_seq()
                            : 0;
  if (sequence == std::numeric_limits<std::uint64_t>::max()) {
    throw std::invalid_argument("source sequence exhausted; increment epoch");
  }
  if (!event.event_id().empty() || event.source_seq() != 0) {
    throw std::invalid_argument("event identity and sequence belong to the SDK");
  }
  event.set_event_id(uuid7());
  event.set_source_seq(sequence + 1);
  if (!event.has_occurred_at()) {
    *event.mutable_occurred_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
  }
  if (!google::protobuf::util::TimeUtil::IsTimestampValid(event.occurred_at()) || event.ByteSizeLong() > 65500) {
    throw std::invalid_argument("invalid event timestamp or event too large");
  }
  record({{"event", encode(event)}}, true);
  return event;
}

std::vector<wire::ConnectorEvent> Store::pending(std::size_t count, std::size_t bytes) const {
  const std::scoped_lock lock(mutex_);
  std::vector<wire::ConnectorEvent> result;
  std::size_t size = 32;
  for (const auto& id : order_) {
    if (!events_.contains(id)) {
      continue;
    }
    const auto& event = events_.at(id);
    size += event.ByteSizeLong() + 8;
    if (result.size() >= count || size > bytes) {
      break;
    }
    result.push_back(event);
  }
  return result;
}

void Store::acknowledge(const std::vector<std::string>& ids) {
  const std::scoped_lock lock(mutex_);
  record({{"ack", ids}});
}

std::vector<wire::SourceCursor> Store::cursors() const {
  const std::scoped_lock lock(mutex_);
  std::vector<wire::SourceCursor> result;
  result.reserve(cursors_.size());
  for (const auto& [source, cursor] : cursors_) {
    result.push_back(cursor);
  }
  return result;
}

Claim Store::claim(const wire::Command& command) {
  const std::scoped_lock lock(mutex_);
  if (const auto found = commands_.find(command.command_id()); found != commands_.end()) {
    Claim result;
    decode(found->second.at("result"), &result.result);
    if (found->second.at("command") != encode(command)) {
      result.result.set_success(false);
      result.result.set_code("COMMON_IDEMPOTENCY_KEY_REUSED");
      result.result.set_message("command identity reused with a different payload");
    }
    return result;
  }
  if (commands_.size() >= options_.max_commands) {
    throw std::runtime_error("durable command ledger full");
  }
  Claim result;
  result.execute = true;
  result.result.set_command_id(command.command_id());
  result.result.set_code("COMMAND_INVALID_STATE");
  result.result.set_message("execution outcome unknown; automatic reexecution is forbidden");
  record({{"id", command.command_id()}, {"command", encode(command)}, {"result", encode(result.result)}}, true);
  return result;
}

void Store::complete(const wire::CommandResult& result) {
  const std::scoped_lock lock(mutex_);
  record({{"id", result.command_id()}, {"result", encode(result)}});
}

std::size_t Store::count() const {
  const std::scoped_lock lock(mutex_);
  return events_.size();
}

std::size_t Store::bytes() const {
  const std::scoped_lock lock(mutex_);
  return bytes_;
}
}  // namespace psim::platform::connector::detail
