add_subdirectory(platform/core)
add_subdirectory(platform/errors)
add_subdirectory(platform/log)
add_subdirectory(platform/async)
add_subdirectory(platform/security)
add_subdirectory(platform/authz)
add_subdirectory(platform/config)
add_subdirectory(platform/observability)
add_subdirectory(platform/runtime)
add_subdirectory(platform/kafka)

add_subdirectory(platform/pg)
add_subdirectory(platform/timers)
add_subdirectory(platform/redis)
add_subdirectory(sim EXCLUDE_FROM_ALL)

add_subdirectory(platform/clickhouse)

add_subdirectory(platform/s3)

add_subdirectory(platform/connector)
add_subdirectory(platform/stream)
add_subdirectory(domain/processing)

add_subdirectory(domain/incident)

add_subdirectory(domain/response)
