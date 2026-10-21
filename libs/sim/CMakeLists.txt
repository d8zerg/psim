# Manual simulation tooling; deliberately absent from CTest and the CI pipelines (ADR-044).
add_library(psim_sim STATIC src/scheduler.cpp src/broker.cpp src/store.cpp src/pipeline.cpp)
add_library(psim::sim ALIAS psim_sim)
target_include_directories(psim_sim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(psim_sim PUBLIC psim::platform_async)
psim_target_defaults(psim_sim)

add_executable(psim_sim_checks tests/random_test.cpp tests/scheduler_test.cpp tests/broker_test.cpp
  tests/store_test.cpp tests/pipeline_test.cpp)
target_link_libraries(psim_sim_checks PRIVATE psim::sim GTest::gtest_main)
psim_target_defaults(psim_sim_checks)

add_executable(psim-sim src/main.cpp)
target_link_libraries(psim-sim PRIVATE psim::sim)
psim_target_defaults(psim-sim)

add_executable(psim_incident_sim tests/incident_test.cpp)
psim_target_defaults(psim_incident_sim)
target_link_libraries(psim_incident_sim PRIVATE psim::sim psim::domain_incident GTest::gtest_main)
