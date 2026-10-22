#pragma once
#include <functional>
#include <optional>

#include "psim/platform/pg/store.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/response/v1/events.pb.h"
#include "transport.hpp"

namespace psim::notification_service {
using Encode = std::function<p::kafka::Record(std::string, std::string, const google::protobuf::Message&)>;
using Lookup = std::function<asio::awaitable<p::Result<p::security::Principal>>(std::string)>;

struct DeliveryPolicy {
  int attempts = 6;
  int rate_limit = 20;
  int window_ms = 10000;
  int retry_ms = 1000;
};

class Repository {
 public:
  Repository(p::pg::Pool* pool, Encode encode, Lookup lookup, Transport* transport, DeliveryPolicy policy = {})
      : pool_(pool), encode_(std::move(encode)), lookup_(std::move(lookup)), transport_(transport), policy_(policy) {}

  asio::awaitable<p::Result<void>> initialize();
  asio::awaitable<p::Result<void>> apply(response::v1::ResponseEventRecord input);
  asio::awaitable<p::Result<void>> tick();
  asio::awaitable<p::Result<Json>> request(p::security::Principal principal, std::string operation, Json body,
                                           std::string key = {}, std::optional<std::uint64_t> expected = {});

  p::pg::MessageStore messages() const { return messages_; }

 private:
  asio::awaitable<p::Result<void>> save(p::pg::Transaction* tx, std::string tenant, std::string site,
                                        response::v1::Notification value, std::string status, std::string destination,
                                        std::int64_t due);
  asio::awaitable<p::Result<void>> audit(p::pg::Transaction* tx, std::string tenant, std::string actor,
                                         std::string action, std::string id, std::string site, bool denied = false,
                                         Json details = Json::object(), std::string cause = {});
  p::pg::Pool* pool_;
  Encode encode_;
  Lookup lookup_;
  Transport* transport_;
  DeliveryPolicy policy_;
  p::pg::MessageStore messages_{"notification"};
};
}  // namespace psim::notification_service
