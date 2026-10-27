#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "psim/platform/pg/connection.hpp"

int migrate_release(psim::platform::pg::ConnectionOptions options, std::filesystem::path directory, std::string phase,
                    std::int64_t minimum_epoch);
