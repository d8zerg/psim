#include "psim/platform/async/blocking_pool.hpp"

#include <boost/system/error_code.hpp>

#include <atomic>
#include <cstddef>

namespace psim::platform::async {

BlockingPool::BlockingPool(std::size_t threads, std::size_t max_tasks)
    : pool_(threads), slots_(pool_.get_executor(), max_tasks) {
  for (std::size_t i = 0; i < max_tasks; ++i) {
    (void)slots_.try_send(boost::system::error_code{});
  }
}

BlockingPool::~BlockingPool() {
  stop();
}

void BlockingPool::stop() {
  stopped_.store(true, std::memory_order_release);
  slots_.close();
  pool_.join();
}

}  // namespace psim::platform::async
