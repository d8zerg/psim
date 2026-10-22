#pragma once
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>

#include "psim/catalog/v1/catalog.pb.h"
#include "psim/incident/v1/events.pb.h"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/response/v1/command.pb.h"
#include "psim/response/v1/events.pb.h"

namespace psim::command_service {
namespace p = platform;
namespace asio = boost::asio;
using Json = nlohmann::json;
using Encode = std::function<p::kafka::Record(std::string, std::string, const google::protobuf::Message&)>;
using SessionLookup = std::function<asio::awaitable<p::Result<std::optional<std::string>>>(std::string, std::string)>;

struct Value {
  response::v1::Command command;
  std::string tenant, site, zone, incident_site, incident_zone, session, external, fingerprint;
};

class Repository {
 public:
  Repository(p::pg::Pool* pool, Encode encode, SessionLookup sessions, int timeout = 30)
      : pool_(pool), encode_(std::move(encode)), sessions_(std::move(sessions)), timeout_(timeout) {}

  asio::awaitable<p::Result<void>> initialize();
  asio::awaitable<p::Result<void>> catalog(catalog::v1::CatalogRecord input);
  asio::awaitable<p::Result<void>> incident(incident::v1::IncidentEventRecord input);
  asio::awaitable<p::Result<void>> requested(response::v1::ResponseEventRecord input);
  asio::awaitable<p::Result<bool>> result(response::v1::CommandResultRecord input);
  asio::awaitable<p::Result<Json>> request(p::security::Principal principal, std::string operation, Json body,
                                           std::string key = {}, std::optional<std::uint64_t> expected = {});
  asio::awaitable<p::Result<void>> tick();

  p::pg::MessageStore messages() const { return messages_; }

 private:
  asio::awaitable<p::Result<void>> project(std::string tenant, std::string kind, std::string id, std::uint64_t version,
                                           Json body);
  asio::awaitable<p::Result<void>> save(p::pg::Transaction* tx, Value value, p::security::Principal actor,
                                        std::string action);
  asio::awaitable<p::Result<void>> dispatch(p::pg::Transaction* tx, Value* value);
  asio::awaitable<p::Result<void>> audit(p::pg::Transaction* tx, p::security::Principal actor, std::string action,
                                         std::string id, std::string site, bool denied = false);
  p::pg::Pool* pool_;
  Encode encode_;
  SessionLookup sessions_;
  int timeout_;
  p::pg::MessageStore messages_{"command"};
};
}  // namespace psim::command_service
