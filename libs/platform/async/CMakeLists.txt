# psim::platform_async - execution model of the services (step 2.2, ADR-016, ADR-040): shards
# (thread per core), deadlines and cancellation, backpressure (bounded queue, credits, semaphore),
# pool for blocking calls, Clock port with a manual clock for deterministic tests.
find_package(Boost REQUIRED CONFIG)

add_library(psim_platform_async STATIC src/clock.cpp src/credits.cpp src/shards.cpp src/blocking_pool.cpp)
add_library(psim::platform_async ALIAS psim_platform_async)
target_include_directories(psim_platform_async PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(psim_platform_async PUBLIC Boost::headers psim::platform_errors)
psim_target_defaults(psim_platform_async)

psim_add_test(psim_platform_async_test
  SOURCES tests/clock_test.cpp tests/deadline_test.cpp tests/credits_test.cpp tests/queue_test.cpp
          tests/shards_test.cpp tests/blocking_pool_test.cpp
  LIBS psim::platform_async)

psim_add_benchmark(psim_platform_async_bench
  SOURCES benchmarks/async_bench.cpp
  LIBS psim::platform_async)
