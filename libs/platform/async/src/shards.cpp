#include "psim/platform/async/shards.hpp"

#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace psim::platform::async {

std::size_t available_cpus() noexcept {
  cpu_set_t set;
  CPU_ZERO(&set);  // NOLINT: libc macro
  if (::sched_getaffinity(0, sizeof(set), &set) == 0) {
    return std::max<std::size_t>(1, static_cast<std::size_t>(CPU_COUNT(&set)));  // NOLINT: libc macro
  }
  return std::max<std::size_t>(1, std::thread::hardware_concurrency());
}

Shards::Shards(std::size_t count, std::string name) : name_(std::move(name)) {
  const auto shards = count == 0 ? available_cpus() : count;
  contexts_.reserve(shards);
  for (std::size_t i = 0; i < shards; ++i) {
    contexts_.push_back(std::make_unique<asio::io_context>(1));  // one thread per context
  }
  guards_.resize(shards);
}

Shards::~Shards() {
  stop(std::chrono::milliseconds(0));
}

asio::any_io_executor Shards::executor(std::size_t index) const {
  return contexts_.at(index)->get_executor();
}

void Shards::start() {
  if (!threads_.empty()) {
    return;
  }
  for (std::size_t i = 0; i < contexts_.size(); ++i) {
    guards_[i].emplace(contexts_[i]->get_executor());
    threads_.emplace_back([this, i] {
      // Thread names show up in top, perf and core dumps: psim-shard-3 (at most 15 characters).
      const auto thread_name = (name_ + "-" + std::to_string(i)).substr(0, 15);
      ::pthread_setname_np(::pthread_self(), thread_name.c_str());
      contexts_[i]->run();
    });
  }
}

void Shards::stop(std::chrono::milliseconds grace) {
  for (auto& guard : guards_) {
    guard.reset();
  }
  const auto deadline = std::chrono::steady_clock::now() + grace;
  for (std::size_t i = 0; i < threads_.size(); ++i) {
    while (!contexts_[i]->stopped() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    contexts_[i]->stop();
    threads_[i].join();
  }
  threads_.clear();
  for (auto& context : contexts_) {
    context->restart();
  }
}

}  // namespace psim::platform::async
