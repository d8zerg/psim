#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "psim/platform/error.hpp"

namespace psim::correlation::live {
namespace p = platform;
using Changes = std::map<std::string, std::optional<std::string>>;

// Accessed serially on the blocking pool; Kafka is the authoritative state journal.
class StateStore {
 public:
  static p::Result<std::unique_ptr<StateStore>> open(const std::string& directory);
  ~StateStore();
  StateStore(const StateStore&) = delete;
  StateStore& operator=(const StateStore&) = delete;
  StateStore(StateStore&&) = delete;
  StateStore& operator=(StateStore&&) = delete;
  p::Result<std::optional<std::string>> get(const std::string& key) const;
  p::Result<std::map<std::string, std::string>> entries() const;
  p::Result<std::map<std::string, std::string>> page(const std::string& after, std::size_t limit = 128) const;
  p::Result<void> apply(const Changes& changes, std::int64_t next_offset);
  p::Result<void> clear();
  p::Result<std::int64_t> checkpoint() const;

 private:
  struct Impl;
  explicit StateStore(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
}  // namespace psim::correlation::live
