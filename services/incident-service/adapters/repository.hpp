#pragma once
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "psim/incident/v1/events.pb.h"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/processing/v1/signal.pb.h"
#include "psim/response/v1/events.pb.h"

namespace psim::incident_service {
namespace p = platform;
namespace asio = boost::asio;
using Json = nlohmann::json;
using Encode = std::function<p::kafka::Record(std::string, std::string, const google::protobuf::Message&)>;
using Lookup = std::function<asio::awaitable<p::Result<p::security::Principal>>(std::string)>;

class Repository {
 public:
  Repository(p::pg::Pool* pool, Encode encode, Lookup lookup = {}, bool legacy_types = false)
      : pool_(pool), encode_(std::move(encode)), lookup_(std::move(lookup)), legacy_types_(legacy_types) {}

  asio::awaitable<p::Result<void>> initialize();
  asio::awaitable<p::Result<void>> apply(processing::v1::SignalRecord signal, Encode outgoing = {});
  asio::awaitable<p::Result<Json>> request(p::security::Principal principal, std::string operation, std::string id,
                                           Json body, std::string key = {}, std::optional<std::uint64_t> expected = {});
  asio::awaitable<p::Result<void>> tick();
  asio::awaitable<p::Result<void>> apply_response(response::v1::ResponseEventRecord input, Encode outgoing = {});

  p::pg::MessageStore messages() const { return messages_; }

 private:
  asio::awaitable<p::Result<Json>> execute(p::pg::Transaction* tx, p::security::Principal principal,
                                           std::string operation, std::string id, Json body,
                                           std::optional<std::uint64_t> expected);
  asio::awaitable<p::Result<void>> save(p::pg::Transaction* tx, p::security::Principal principal,
                                        incident::v1::Incident value, std::string action, std::string ref = {},
                                        std::string text = {}, std::string previous = {},
                                        const Encode* outgoing = nullptr, const common::v1::Envelope* cause = nullptr);
  asio::awaitable<p::Result<void>> audit(p::pg::Transaction* tx, p::security::Principal principal, std::string action,
                                         std::string id, std::string site = {}, bool refused = false,
                                         std::string attempted = {});
  asio::awaitable<p::Result<void>> denied(p::security::Principal principal, std::string action, std::string id);
  p::pg::Pool* pool_;
  Encode encode_;
  Lookup lookup_;
  bool legacy_types_;
  p::pg::MessageStore messages_{"incident"};
};
}  // namespace psim::incident_service
