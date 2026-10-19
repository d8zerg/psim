#include "connector_admin.hpp"

#include <google/rpc/error_details.pb.h>
#include <google/rpc/status.pb.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <psim/connector/v1/administration.grpc.pb.h>
#include <psim/connector/v1/administration.pb.h>

#include <chrono>
#include <expected>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/secret.hpp"

namespace psim::resource_catalog {
namespace p = platform;

namespace {
p::Result<std::string> public_file(const std::string& path) {
  std::ifstream input(path);
  std::string bytes((std::istreambuf_iterator<char>(input)), {});
  if (input.bad() || bytes.empty() || bytes.size() > 65536) {
    return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid management trust file");
  }
  return bytes;
}
}  // namespace

p::Result<std::unique_ptr<ConnectorAdmin>> ConnectorAdmin::create(p::async::BlockingPool* blocking,
                                                                  const std::string& target, const std::string& ca,
                                                                  const std::string& certificate,
                                                                  const std::string& key) {
  auto authority = public_file(ca);
  auto cert = public_file(certificate);
  auto secret = p::security::read_secret(key);
  if (!authority || !cert || !secret) {
    return p::fail(p::ErrorCode::kCommonInvalidArgument, "management TLS credentials required");
  }
  grpc::SslCredentialsOptions options;
  options.pem_root_certs = *authority;
  options.pem_cert_chain = *cert;
  options.pem_private_key = std::string(secret->view());
  auto channel = grpc::CreateChannel(target, grpc::SslCredentials(options));
  return std::unique_ptr<ConnectorAdmin>(
      new ConnectorAdmin(blocking, connector::v1::ConnectorAdministrationService::NewStub(channel)));
}

boost::asio::awaitable<p::Result<connector::v1::ManageResponse>> ConnectorAdmin::manage(
    connector::v1::ManageRequest request, std::string token) {
  co_return co_await blocking_->run([this, &request, &token]() -> p::Result<connector::v1::ManageResponse> {
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
    context.AddMetadata("authorization", "Bearer " + token);
    connector::v1::ManageResponse response;
    auto status = stub_->Manage(&context, request, &response);
    if (status.ok()) {
      return response;
    }
    google::rpc::Status details;
    if (details.ParseFromString(status.error_details())) {
      for (const auto& any : details.details()) {
        google::rpc::ErrorInfo info;
        if (any.UnpackTo(&info)) {
          if (const auto code = p::parse_error_code(info.reason())) {
            return p::fail(*code, "connector management rejected");
          }
        }
      }
    }
    return p::fail(p::ErrorCode::kCommonUnavailable, "connector management unavailable");
  });
}
}  // namespace psim::resource_catalog
