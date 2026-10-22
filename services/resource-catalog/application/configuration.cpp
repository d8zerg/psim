#include "configuration.hpp"

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>

#include <set>
#include <string>
#include <string_view>

#include "model.hpp"
#include "psim/common/v1/types.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/domain/processing/mapping.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::resource_catalog {
Json default_configuration(std::string_view kind) {
  if (kind == "retention") {
    return {{"eventHistoryAll", "604800s"},
            {"eventHistorySignificant", "7776000s"},
            {"incidents", "34560000s"},
            {"audit", "31536000s"},
            {"version", "0"}};
  }
  return {{"significanceThreshold", "SEVERITY_MEDIUM"},
          {"maxClockSkew", "30s"},
          {"maxBufferReplayAge", "86400s"},
          {"sourceIdleTimeout", "60s"},
          {"version", "0"}};
}

p::Result<config::v1::ConfigRecord> configuration(std::string_view kind, std::string_view id, const Json& body,
                                                  const std::set<std::string>& taxonomy) {
  config::v1::ConfigRecord record;
  google::protobuf::Message* value = nullptr;
  if (kind == "mapping") {
    value = record.mutable_mapping_rule_set();
  } else if (kind == "retention") {
    value = record.mutable_retention();
  } else if (kind == "processing") {
    value = record.mutable_processing();
  } else {
    return p::fail(p::ErrorCode::kCommonNotFound, "unknown configuration");
  }
  if (!body.is_object() || !google::protobuf::util::JsonStringToMessage(body.dump(), value).ok()) {
    return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid configuration fields");
  }
  if (record.has_mapping_rule_set()) {
    auto* rules = record.mutable_mapping_rule_set();
    if (id.empty() || id.size() > 128
        || id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")
               != std::string::npos
        || rules->connector_type() != id || rules->taxonomy_version() != "1" || rules->rules_size() == 0
        || rules->rules_size() > 10000
        || (!rules->mapping_rule_set_id().empty() && !canonical_uuid(rules->mapping_rule_set_id()))) {
      return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid mapping identity or taxonomy");
    }
    for (const auto& rule : rules->rules()) {
      if (rule.raw_code().empty() || rule.raw_code().size() > 128
          || (rule.raw_code().find('*') != std::string::npos && rule.raw_code().find('*') != rule.raw_code().size() - 1)
          || !taxonomy.contains(rule.type()) || !common::v1::Severity_IsValid(rule.severity())
          || !domain::processing::mapping_condition(rule.condition()) || rule.attribute_map_size() > 64) {
        return p::fail(p::ErrorCode::kCommonInvalidArgument, "unsupported mapping rule");
      }
      for (const auto& [from, to] : rule.attribute_map()) {
        if (from.empty() || to.empty() || from.size() > 128 || to.size() > 128) {
          return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid attribute mapping");
        }
      }
    }
  } else {
    const auto* descriptor = value->GetDescriptor();
    const auto* reflection = value->GetReflection();
    for (int index = 0; index < descriptor->field_count(); ++index) {
      const auto* field = descriptor->field(index);
      if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
        google::protobuf::Duration duration;
        duration.CopyFrom(reflection->GetMessage(*value, field));
        if (!google::protobuf::util::TimeUtil::IsDurationValid(duration) || duration.seconds() <= 0
            || duration.nanos() != 0 || duration.seconds() > 10LL * 366 * 86400) {
          return p::fail(p::ErrorCode::kCommonInvalidArgument, "positive bounded whole-second duration required");
        }
      }
    }
    if (record.has_retention()
        && record.retention().event_history_significant().seconds()
               < record.retention().event_history_all().seconds()) {
      return p::fail(p::ErrorCode::kCommonInvalidArgument, "significant retention cannot be shorter than all events");
    }
    if (record.has_processing()
        && (!common::v1::Severity_IsValid(record.processing().significance_threshold())
            || record.processing().significance_threshold() == 0)) {
      return p::fail(p::ErrorCode::kCommonInvalidArgument, "significance threshold required");
    }
  }
  return record;
}
}  // namespace psim::resource_catalog
