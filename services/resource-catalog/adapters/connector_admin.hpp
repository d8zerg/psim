#pragma once

#include <boost/asio/awaitable.hpp>
#include <psim/connector/v1/administration.grpc.pb.h>

#include <memory>
#include <string>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"

namespace psim::resource_catalog {
class ConnectorAdmin {
 public:
  static platform::Result<std::unique_ptr<ConnectorAdmin>> create(platform::async::BlockingPool* blocking,
                                                                  const std::string& target, const std::string& ca,
                                                                  const std::string& certificate,
                                                                  const std::string& key);
  boost::asio::awaitable<platform::Result<connector::v1::ManageResponse>> manage(connector::v1::ManageRequest request,
                                                                                 std::string token);

 private:
  ConnectorAdmin(platform::async::BlockingPool* blocking,
                 std::unique_ptr<connector::v1::ConnectorAdministrationService::Stub> stub)
      : blocking_(blocking), stub_(std::move(stub)) {}

  platform::async::BlockingPool* blocking_;
  std::unique_ptr<connector::v1::ConnectorAdministrationService::Stub> stub_;
};
}  // namespace psim::resource_catalog
