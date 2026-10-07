#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace psim::platform::log {

enum class Level : std::uint8_t { kTrace, kDebug, kInfo, kWarn, kError };

[[nodiscard]] std::optional<Level> parse_level(std::string_view name) noexcept;
[[nodiscard]] std::string_view to_string(Level level) noexcept;

/// A field of a log record. Values are copied into the line before log() returns.
struct Field {
  std::string_view key;
  std::variant<std::string_view, std::int64_t, std::uint64_t, double, bool> value;
};

/// Destination of formatted lines; one call per line, no trailing newline.
class Sink {
 public:
  Sink() = default;
  Sink(const Sink&) = delete;
  Sink& operator=(const Sink&) = delete;
  Sink(Sink&&) = delete;
  Sink& operator=(Sink&&) = delete;
  virtual ~Sink() = default;
  virtual void write(std::string_view line) = 0;
};

/// Standard output: the Collector reads container logs (crosscutting.md 6.3).
class StdoutSink final : public Sink {
 public:
  void write(std::string_view line) override;

 private:
  std::mutex mutex_;
};

/// Lines kept in memory, for tests.
class MemorySink final : public Sink {
 public:
  void write(std::string_view line) override;
  [[nodiscard]] std::vector<std::string> lines() const;

 private:
  mutable std::mutex mutex_;
  std::vector<std::string> lines_;
};

struct TraceIds {
  std::string trace_id;
  std::string span_id;
};

struct LoggerOptions {
  std::string service;
  std::string instance;
  Level level = Level::kInfo;
  /// Ids of the active span, added to every record (set by the observability layer).
  std::function<std::optional<TraceIds>()> trace_context;
  std::function<std::chrono::system_clock::time_point()> now = [] {
    return std::chrono::system_clock::now();
  };
};

/// Structured logger: one JSON object per line with ts, level, service, instance, trace_id,
/// span_id, msg and the fields of the record. Thread-safe; the level can change at run time.
class Logger {
 public:
  Logger(LoggerOptions options, std::shared_ptr<Sink> sink);

  void set_level(Level level) noexcept { level_.store(level, std::memory_order_relaxed); }

  [[nodiscard]] Level level() const noexcept { return level_.load(std::memory_order_relaxed); }

  [[nodiscard]] bool enabled(Level level) const noexcept { return level >= this->level(); }

  void log(Level level, std::string_view message, std::initializer_list<Field> fields = {});

  void debug(std::string_view message, std::initializer_list<Field> fields = {}) {
    log(Level::kDebug, message, fields);
  }

  void info(std::string_view message, std::initializer_list<Field> fields = {}) { log(Level::kInfo, message, fields); }

  void warn(std::string_view message, std::initializer_list<Field> fields = {}) { log(Level::kWarn, message, fields); }

  void error(std::string_view message, std::initializer_list<Field> fields = {}) {
    log(Level::kError, message, fields);
  }

 private:
  LoggerOptions options_;
  std::shared_ptr<Sink> sink_;
  std::atomic<Level> level_;
};

/// JSON string escaping (RFC 8259), appended to `out` without quotes.
void append_escaped(std::string& out, std::string_view text);

}  // namespace psim::platform::log
