#pragma once
#include <nlohmann/json.hpp>

#include "psim/domain/response/plan.hpp"
#include "psim/platform/error.hpp"
#include "psim/response/v1/plan.pb.h"
#include "psim/response/v1/run.pb.h"

namespace psim::response_engine {
namespace d = domain::response;
namespace p = platform;
using Json = nlohmann::json;

[[nodiscard]] p::Result<d::Plan> model(const response::v1::ResponsePlan& plan);
[[nodiscard]] d::Run model(const response::v1::ResponseRun& run);
[[nodiscard]] response::v1::ResponseRun snapshot(const d::Run& run, const response::v1::ResponseRun& identity,
                                                 const response::v1::ResponsePlan& plan, std::string actor);
[[nodiscard]] int specificity(const response::v1::PlanBinding& binding);
[[nodiscard]] bool overlaps(const response::v1::PlanBinding& left, const response::v1::PlanBinding& right);
[[nodiscard]] bool matches(const response::v1::PlanBinding& binding, const std::string& type, const std::string& site,
                           const std::string& zone, int priority);
[[nodiscard]] std::string effect_id(const response::v1::ResponseRun& run, const std::string& step,
                                    std::uint32_t attempt, const std::string& target = {});
[[nodiscard]] Json replay(const response::v1::ResponsePlan& plan, const Json& history);
}  // namespace psim::response_engine
