#include <google/protobuf/util/json_util.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <psim/connector/v1/administration.grpc.pb.h>
#include <psim/connector/v1/administration.pb.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <print>
#include <span>
#include <stdexcept>
#include <string>

#include "psim/platform/connector/grpc_process.hpp"
#include "psim/platform/security/secret.hpp"

namespace {
std::string file(const std::string& path) {
  std::ifstream input(path);
  std::string value((std::istreambuf_iterator<char>(input)), {});
  if (!input || value.empty() || value.size() > 65536) {
    throw std::runtime_error("invalid public input file");
  }
  return value;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const psim::platform::connector::GrpcProcess grpc;
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (argc != 5) {
      std::println(stderr, "usage: psim-gateway-admin <host:port> <tls-directory> <request.json> <token-file>");
      return 2;
    }
    const std::string directory(args[2]);
    auto key = psim::platform::security::read_secret(directory + "/admin.key");
    auto token = psim::platform::security::read_secret(args[4]);
    if (!key || !token) {
      throw std::runtime_error("protected client key and operator token required");
    }
    grpc::SslCredentialsOptions options;
    options.pem_root_certs = file(directory + "/ca.pem");
    options.pem_cert_chain = file(directory + "/admin.pem");
    options.pem_private_key = std::string(key->view());
    auto channel = grpc::CreateChannel(args[1], grpc::SslCredentials(options));
    auto stub = psim::connector::v1::ConnectorAdministrationService::NewStub(channel);
    psim::connector::v1::ManageRequest request;
    if (!google::protobuf::util::JsonStringToMessage(file(args[3]), &request).ok()) {
      throw std::runtime_error("invalid management request JSON");
    }
    grpc::ClientContext context;
    context.set_wait_for_ready(true);
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(25));
    context.AddMetadata("authorization", "Bearer " + std::string(token->view()));
    psim::connector::v1::ManageResponse response;
    auto result = stub->Manage(&context, request, &response);
    if (!result.ok()) {
      std::println(stderr, "gateway management failed: {}", result.error_message());
      return 1;
    }
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(response, &json).ok()) {
      throw std::runtime_error("invalid management response");
    }
    std::println("{}", json);
    return 0;
  } catch (const std::exception& error) {
    (void)std::fputs("gateway management failed: ", stderr);
    (void)std::fputs(error.what(), stderr);
    (void)std::fputs("\n", stderr);
    return 1;
  }
}
