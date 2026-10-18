#pragma once

#include <boost/asio/steady_timer.hpp>
#include <grpcpp/grpcpp.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <psim/connector/v1/administration.grpc.pb.h>
#include <psim/connector/v1/connector.grpc.pb.h>

#include <atomic>
#include <map>
#include <memory>
#include <string>

#include "psim/platform/pg/store.hpp"
#include "psim/platform/redis/patterns.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "registry.hpp"

namespace psim::connector_gateway {
struct SessionState;
struct DeliveryBatch;

class Gateway final : public platform::stream::Worker,
                      public connector::v1::ConnectorGatewayService::CallbackService,
                      public connector::v1::ConnectorAdministrationService::Service {
 public:
  explicit Gateway(const platform::runtime::Context& context);

  [[nodiscard]] bool ready() const override { return platform::stream::Worker::ready() && healthy_.load(); }

  grpc::ServerBidiReactor<connector::v1::SessionRequest, connector::v1::SessionResponse>* Session(
      grpc::CallbackServerContext* context) override;
  grpc::Status Manage(grpc::ServerContext* call, const connector::v1::ManageRequest* request,
                      connector::v1::ManageResponse* response) override;
  boost::asio::awaitable<platform::Result<void>> handle(const platform::kafka::Message* /*message*/,
                                                        platform::kafka::Output* /*output*/) override;

  [[nodiscard]] const boost::asio::any_io_executor& shard() const { return executor(); }

  void add(std::shared_ptr<SessionState> session);
  void removed(std::shared_ptr<SessionState> session);
  boost::asio::awaitable<void> dispatch(std::shared_ptr<SessionState> session);

 private:
  boost::asio::awaitable<platform::Result<void>> initialize() override;
  boost::asio::awaitable<void> shutdown() override;
  boost::asio::awaitable<platform::Result<void>> open_listener();
  boost::asio::awaitable<void> refresh();
  boost::asio::awaitable<void> publish();
  boost::asio::awaitable<void> commands();
  boost::asio::awaitable<void> renew(std::shared_ptr<SessionState> session);
  boost::asio::awaitable<void> release(std::shared_ptr<SessionState> session);
  boost::asio::awaitable<platform::Result<void>> register_session(std::shared_ptr<SessionState> session);
  boost::asio::awaitable<platform::Result<void>> events(std::shared_ptr<SessionState> session);
  boost::asio::awaitable<platform::Result<void>> state(std::shared_ptr<SessionState> session);
  boost::asio::awaitable<platform::Result<void>> result(std::shared_ptr<SessionState> session);
  boost::asio::awaitable<platform::Result<void>> deliver(std::vector<platform::kafka::Record> records);
  boost::asio::awaitable<void> send_one(std::shared_ptr<DeliveryBatch> batch, platform::kafka::Record record);
  boost::asio::awaitable<platform::Result<void>> quota(std::shared_ptr<SessionState> session, std::uint32_t count);
  boost::asio::awaitable<platform::Result<connector::v1::ManageResponse>> manage(connector::v1::ManageRequest request,
                                                                                 std::string token);
  [[nodiscard]] bool allowed(const SessionState& session) const;
  void timer();
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<platform::pg::Pool> database_;
  std::unique_ptr<Registry> registry_;
  std::unique_ptr<platform::pg::OutboxRelay> relay_;
  std::unique_ptr<platform::redis::Pool> redis_;
  std::unique_ptr<platform::security::JwksCache> verifier_;
  std::unique_ptr<platform::kafka::Consumer> command_consumer_;
  std::map<std::string, std::shared_ptr<SessionState>> sessions_;
  Snapshot snapshot_;
  boost::asio::steady_timer tick_, refreshed_, published_, idle_;
  std::chrono::steady_clock::time_point verified_{};
  std::string boot_, ca_file_;
  std::atomic_bool healthy_{false};
  bool stopping_ = false, command_healthy_ = true;
  int background_ = 0, releasing_ = 0;
  prometheus::Counter *accepted_, *rejected_, *commands_sent_, *expired_, *denied_;
  prometheus::Gauge* connected_;
};

[[nodiscard]] std::string settings_schema();
}  // namespace psim::connector_gateway
