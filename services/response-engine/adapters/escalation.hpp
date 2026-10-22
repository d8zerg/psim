#pragma once
#include "psim/platform/identity/directory.hpp"
#include "repository.hpp"

namespace psim::response_engine {
using Lookup = std::function<asio::awaitable<p::Result<p::security::Principal>>(std::string)>;
[[nodiscard]] p::Result<void> validate_policy(const response::v1::EscalationPolicy& policy);

class Escalation {
 public:
  Escalation(p::pg::Pool* pool, Encode encode, Lookup lookup = {})
      : pool_(pool), encode_(std::move(encode)), lookup_(std::move(lookup)) {}

  asio::awaitable<p::Result<void>> initialize();
  asio::awaitable<p::Result<void>> incident(incident::v1::IncidentEventRecord input);
  asio::awaitable<p::Result<void>> response(response::v1::ResponseEventRecord input);
  asio::awaitable<p::Result<void>> tick();
  asio::awaitable<p::Result<Json>> request(p::security::Principal principal, std::string operation, Json body,
                                           std::string key, std::optional<std::uint64_t> expected);

 private:
  asio::awaitable<p::Result<void>> trigger(p::pg::Transaction* tx, std::string tenant, incident::v1::Incident value,
                                           response::v1::EscalationPolicy policy, response::v1::EscalationLevel level,
                                           std::string kind);
  asio::awaitable<p::Result<void>> publish(p::pg::Transaction* tx, response::v1::ResponseEventRecord event,
                                           std::string id);
  asio::awaitable<p::Result<void>> audit(p::pg::Transaction* tx, std::string tenant, std::string actor,
                                         std::string action, std::string id, std::string site, bool denied = false);
  p::pg::Pool* pool_;
  Encode encode_;
  Lookup lookup_;
  p::pg::MessageStore messages_{"response"};
  p::timers::Store timers_{"escalation"};
};
}  // namespace psim::response_engine
