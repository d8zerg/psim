#pragma once
#include <functional>
#include <optional>
#include <set>

#include "model.hpp"
#include "psim/catalog/v1/catalog.pb.h"
#include "psim/incident/v1/events.pb.h"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/timers/store.hpp"
#include "psim/response/v1/events.pb.h"

namespace psim::response_engine {
namespace asio = boost::asio;
using Encode = std::function<p::kafka::Record(std::string, std::string, const google::protobuf::Message&)>;
using Handler = std::function<asio::awaitable<p::Result<bool>>(std::string, Json, int)>;

struct Value {
  std::string tenant, site, zone;
  response::v1::ResponsePlan plan;
  response::v1::ResponseRun run;
};

class Repository {
 public:
  Repository(p::pg::Pool* pool, Encode encode, std::set<std::string> handlers = {}, Handler handler = {})
      : pool_(pool), encode_(std::move(encode)), handlers_(std::move(handlers)), handler_(std::move(handler)) {}

  asio::awaitable<p::Result<void>> initialize();
  asio::awaitable<p::Result<void>> catalog(catalog::v1::CatalogRecord input);
  asio::awaitable<p::Result<void>> incident(incident::v1::IncidentEventRecord input);
  asio::awaitable<p::Result<void>> response(response::v1::ResponseEventRecord input);
  asio::awaitable<p::Result<Json>> request(p::security::Principal principal, std::string operation, Json body,
                                           std::string key = {}, std::optional<std::uint64_t> expected = {});
  asio::awaitable<p::Result<void>> tick();
  asio::awaitable<p::Result<void>> effects();

  std::uint64_t missing_plans() const { return missing_plans_; }

  p::pg::MessageStore messages() const { return messages_; }

 private:
  static asio::awaitable<p::Result<Value>> load(p::pg::Transaction* tx, std::string tenant, std::string id);
  asio::awaitable<p::Result<void>> save(p::pg::Transaction* tx, Value* value, std::vector<d::Change> changes,
                                        std::string actor);
  asio::awaitable<p::Result<void>> drive(p::pg::Transaction* tx, Value* value,
                                         std::optional<p::security::Principal> manual = {});
  asio::awaitable<p::Result<void>> audit(p::pg::Transaction* tx, std::string tenant, std::string actor,
                                         std::string action, std::string id, std::string site, bool denied = false);
  asio::awaitable<p::Result<void>> validate_catalog(p::pg::Transaction* tx, std::string tenant,
                                                    response::v1::ResponsePlan plan);
  asio::awaitable<p::Result<void>> emit(p::pg::Transaction* tx, response::v1::ResponseEventRecord event);
  asio::awaitable<p::Result<void>> finish(p::pg::Transaction* tx, Value* value, d::Operation operation,
                                          std::string note = {});
  std::uint64_t missing_plans_ = 0;
  p::pg::Pool* pool_;
  Encode encode_;
  std::set<std::string> handlers_;
  Handler handler_;
  p::pg::MessageStore messages_{"response"};
  p::timers::Store timers_{"response"};
};
}  // namespace psim::response_engine
