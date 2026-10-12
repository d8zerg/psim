# Explicit expand/contract migrations of step 2.4. Manifest is a trusted release artifact.
find_package(nlohmann_json REQUIRED CONFIG)
add_executable(psim-pg-migrate src/main.cpp)
psim_target_defaults(psim-pg-migrate)
target_link_libraries(psim-pg-migrate PRIVATE psim::platform_pg nlohmann_json::nlohmann_json)
install(TARGETS psim-pg-migrate RUNTIME DESTINATION bin COMPONENT psim-pg-migrate)
psim_package(psim-pg-migrate SUMMARY "PSIM PostgreSQL migration runner")
add_test(NAME psim-pg-migrate.usage COMMAND psim-pg-migrate)
set_tests_properties(psim-pg-migrate.usage PROPERTIES LABELS "smoke;psim-pg-migrate" WILL_FAIL TRUE)
