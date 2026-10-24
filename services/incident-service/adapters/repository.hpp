#pragma once
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "psim/incident/v1/events.pb.h"
#include "psim/platform/async/credits.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/processing/v1/signal.pb.h"
#include "psim/response/v1/events.pb.h"

namespace psim::incident_service {
namespace p = platform;
namespace asio = boost::asio;
using Json = nlohmann::json;
using Encode = std::function<p::kafka::Record(std::string, std::string, const google::protobuf::Message&)>;
using Lookup = std::function<asio::awaitable<p::Result<p::security::Principal>>(std::string)>;

struct AssistantOptions {
  std::string endpoint;
  std::string model;
  p::security::HttpOptions http;
};

class Repository {
 public:
  Repository(p::pg::Pool* pool, Encode encode, Lookup lookup = {}, bool legacy_types = false,
             AssistantOptions assistant = {})
      : pool_(pool),
        encode_(std::move(encode)),
        lookup_(std::move(lookup)),
        legacy_types_(legacy_types),
        assistant_(std::move(assistant)) {}

  asio::awaitable<p::Result<void>> initialize();
  asio::awaitable<p::Result<void>> apply(processing::v1::SignalRecord signal, Encode outgoing = {});
  asio::awaitable<p::Result<Json>> request(p::security::Principal principal, std::string operation, std::string id,
                                           Json body, std::string key = {}, std::optional<std::uint64_t> expected = {});
  asio::awaitable<p::Result<void>> tick();
  asio::awaitable<p::Result<void>> apply_response(response::v1::ResponseEventRecord input, Encode outgoing = {});

  asio::awaitable<p::Result<void>> denied(p::security::Principal principal, std::string action, std::string id);

  p::pg::MessageStore messages() const { return messages_; }

 private:
  asio::awaitable<p::Result<Json>> operational(p::pg::Transaction* tx, p::security::Principal principal,
                                               std::string operation, incident::v1::Incident value, Json body,
                                               std::optional<std::uint64_t> expected, Json prepared = {});
  asio::awaitable<p::Result<Json>> prepare_assistant(p::security::Principal principal, incident::v1::Incident value,
                                                     Json body);
  asio::awaitable<p::Result<void>> expire_operations(p::pg::Transaction* tx);
  asio::awaitable<p::Result<Json>> execute(p::pg::Transaction* tx, p::security::Principal principal,
                                           std::string operation, std::string id, Json body,
                                           std::optional<std::uint64_t> expected, Json prepared = {});
  asio::awaitable<p::Result<void>> save(p::pg::Transaction* tx, p::security::Principal principal,
                                        incident::v1::Incident value, std::string action, std::string ref = {},
                                        std::string text = {}, std::string previous = {},
                                        const Encode* outgoing = nullptr, const common::v1::Envelope* cause = nullptr);
  asio::awaitable<p::Result<void>> audit(p::pg::Transaction* tx, p::security::Principal principal, std::string action,
                                         std::string id, std::string site = {}, bool refused = false,
                                         std::string attempted = {}, Json details = Json::object());
  p::pg::Pool* pool_;
  Encode encode_;
  Lookup lookup_;
  bool legacy_types_;
  AssistantOptions assistant_;
  p::async::Semaphore assistant_requests_{2};
  p::pg::MessageStore messages_{"incident"};
};
}  // namespace psim::incident_service
