#include "psim/platform/log.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <format>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace psim::platform::log {

namespace {

constexpr std::array<std::string_view, 5> kLevelNames{"trace", "debug", "info", "warn", "error"};

void append_field(std::string& line, std::string_view key) {
  line += ",\"";
  append_escaped(line, key);
  line += "\":";
}

void append_value(std::string& line, const decltype(Field::value)& value) {
  std::visit(
      [&line](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string_view>) {
          line += '"';
          append_escaped(line, v);
          line += '"';
        } else if constexpr (std::is_same_v<T, bool>) {
          line += v ? "true" : "false";
        } else {
          std::format_to(std::back_inserter(line), "{}", v);
        }
      },
      value);
}

}  // namespace

std::optional<Level> parse_level(std::string_view name) noexcept {
  for (std::size_t i = 0; i < kLevelNames.size(); ++i) {
    if (kLevelNames.at(i) == name) {
      return static_cast<Level>(i);
    }
  }
  return std::nullopt;
}

std::string_view to_string(Level level) noexcept {
  return kLevelNames.at(static_cast<std::size_t>(level));
}

void append_escaped(std::string& out, std::string_view text) {
  for (const char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          std::format_to(std::back_inserter(out), "\\u{:04x}", static_cast<unsigned>(c));
        } else {
          out += c;
        }
    }
  }
}

void StdoutSink::write(std::string_view line) {
  const std::scoped_lock lock(mutex_);
  // One write per line keeps lines whole when several processes share the stream.
  std::string buffer(line);
  buffer += '\n';
  (void)std::fwrite(buffer.data(), 1, buffer.size(), stdout);
  (void)std::fflush(stdout);
}

void MemorySink::write(std::string_view line) {
  const std::scoped_lock lock(mutex_);
  lines_.emplace_back(line);
}

std::vector<std::string> MemorySink::lines() const {
  const std::scoped_lock lock(mutex_);
  return lines_;
}

Logger::Logger(LoggerOptions options, std::shared_ptr<Sink> sink)
    : options_(std::move(options)), sink_(std::move(sink)), level_(options_.level) {}

void Logger::log(Level level, std::string_view message, std::initializer_list<Field> fields) {
  if (!enabled(level)) {
    return;
  }
  const auto now = std::chrono::floor<std::chrono::milliseconds>(options_.now());
  std::string line;
  line.reserve(256);
  std::format_to(std::back_inserter(line), R"({{"ts":"{:%FT%TZ}","level":"{}","service":")", now, to_string(level));
  append_escaped(line, options_.service);
  line += R"(","instance":")";
  append_escaped(line, options_.instance);
  line += '"';
  if (options_.trace_context) {
    if (const auto ids = options_.trace_context()) {
      std::format_to(std::back_inserter(line), R"(,"trace_id":"{}","span_id":"{}")", ids->trace_id, ids->span_id);
    }
  }
  line += R"(,"msg":")";
  append_escaped(line, message);
  line += '"';
  for (const auto& field : fields) {
    append_field(line, field.key);
    append_value(line, field.value);
  }
  line += '}';
  sink_->write(line);
}

}  // namespace psim::platform::log
