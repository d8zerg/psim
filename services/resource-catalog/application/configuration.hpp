#pragma once
#include <set>
#include <string>

#include "model.hpp"
#include "psim/config/v1/config.pb.h"

namespace psim::resource_catalog {
p::Result<config::v1::ConfigRecord> configuration(std::string_view kind, std::string_view id, const Json& body,
                                                  const std::set<std::string>& taxonomy);
Json default_configuration(std::string_view kind);
}  // namespace psim::resource_catalog
