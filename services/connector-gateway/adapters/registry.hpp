#pragma once

#include <psim/connector/v1/administration.pb.h>
#include <psim/ingest/v1/state.pb.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "protocol.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::connector_gateway {
struct Snapshot {
  std::uint64_t revision = 0;
  std::map<std::string, Registration> registrations;
};

class Registry {
 public:
  using Encode = std::function<platform::kafka::Record(std::string, std::string, const google::protobuf::Message&)>;

  Registry(platform::pg::Pool* pool, platform::pg::Pool* reader, Encode encode)
      : pool_(pool), reader_(reader), encode_(std::move(encode)), messages_("ingest") {}

  boost::asio::awaitable<platform::Result<void>> initialize();
  boost::asio::awaitable<platform::Result<Snapshot>> snapshot();
  boost::asio::awaitable<platform::Result<connector::v1::ManageResponse>> manage(
      connector::v1::ManageRequest request, platform::security::Principal principal,
      std::optional<Certificate> certificate);
  boost::asio::awaitable<platform::Result<void>> denied(std::string tenant, std::string actor, std::string object,
                                                        std::string action);
  boost::asio::awaitable<platform::Result<void>> acknowledge(std::string boot, std::uint64_t revision);
  boost::asio::awaitable<platform::Result<bool>> enforced(std::uint64_t revision);
  boost::asio::awaitable<platform::Result<void>> connected(Registration registration, Certificate peer,
                                                           std::string session, int ttl);
  boost::asio::awaitable<platform::Result<bool>> owns(std::string session);
  boost::asio::awaitable<platform::Result<bool>> renew(std::string session, int ttl);
  boost::asio::awaitable<platform::Result<void>> disconnected(std::string session, std::string reason);
  boost::asio::awaitable<platform::Result<void>> expire();

  [[nodiscard]] platform::pg::MessageStore messages() const { return messages_; }

 private:
  boost::asio::awaitable<platform::Result<void>> publish_catalog(platform::pg::Transaction* tx, std::string tenant,
                                                                 catalog::v1::Connector connector, std::string actor);
  boost::asio::awaitable<platform::Result<void>> append(platform::pg::Transaction* tx,
                                                        const ingest::v1::GatewayStateRecord* state);
  platform::pg::Pool* pool_;
  platform::pg::Pool* reader_;
  Encode encode_;
  platform::pg::MessageStore messages_;
};

void envelope(common::v1::Envelope* output, std::string tenant, std::string connector, std::string id = {});
}  // namespace psim::connector_gateway
