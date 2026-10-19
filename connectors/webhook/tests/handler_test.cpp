#include "../handler.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <unistd.h>

#include <filesystem>
#include <memory>
#include <string>

#include "psim/platform/connector/client.hpp"

namespace psim::connectors::webhook {
namespace {
constexpr auto kToken = "0123456789abcdef0123456789abcdef";
constexpr auto kBody = R"({"source_ref":"door-1","raw_code":"ALARM","occurred_at":"2020-01-01T00:00:00Z"})";

class Webhook : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() / ("psim-webhook-handler-" + std::to_string(::getpid()));
    std::filesystem::remove_all(path_);
    options_.buffer_directory = path_.string();
    client_ = std::make_unique<platform::connector::Client>(options_);
  }

  void TearDown() override {
    client_.reset();
    std::filesystem::remove_all(path_);
  }

  static Request request(const std::string& body = kBody) {
    Request result{http::verb::post, "/v1/events", 11};
    result.set(http::field::authorization, std::string("Bearer ") + kToken);
    result.set(http::field::content_type, "application/json");
    result.set("Idempotency-Key", "upstream-1");
    result.body() = body;
    return result;
  }

  platform::connector::Options& options() { return options_; }

  std::unique_ptr<platform::connector::Client>& client() { return client_; }

 private:
  platform::connector::Options options_;
  std::unique_ptr<platform::connector::Client> client_;
  std::filesystem::path path_;
};

TEST_F(Webhook, DuplicateRetrySurvivesRestartWithIdenticalReceipt) {
  const auto first = Handler(*client(), kToken).handle(request());
  ASSERT_EQ(first.result(), http::status::accepted);
  EXPECT_EQ(client()->diagnostics().pending_events, 1);
  EXPECT_EQ(Handler(*client(), kToken).handle(request()).body(), first.body());
  client().reset();
  client() = std::make_unique<platform::connector::Client>(options());
  EXPECT_EQ(Handler(*client(), kToken).handle(request()).body(), first.body());
  EXPECT_EQ(client()->diagnostics().pending_events, 1);
  auto changed = request();
  changed.body() = R"({"source_ref":"door-1","raw_code":"FIRE","occurred_at":"2020-01-01T00:00:00Z"})";
  EXPECT_EQ(Handler(*client(), kToken).handle(changed).result(), http::status::conflict);
  EXPECT_EQ(client()->diagnostics().pending_events, 1);
}

TEST_F(Webhook, AuthValidationAndUnknownFieldsCannotAdmitEvents) {
  const Handler handler(*client(), kToken);
  auto input = request();
  input.erase(http::field::authorization);
  EXPECT_EQ(handler.handle(input).result(), http::status::unauthorized);
  input = request();
  input.insert(http::field::authorization, std::string("Bearer ") + kToken);
  EXPECT_EQ(handler.handle(input).result(), http::status::unauthorized);
  input = request();
  input.erase("Idempotency-Key");
  EXPECT_EQ(handler.handle(input).result(), http::status::bad_request);
  EXPECT_EQ(handler.handle(request("{}")).result(), http::status::bad_request);
  EXPECT_EQ(handler.handle(request("[]")).result(), http::status::bad_request);
  EXPECT_EQ(handler.handle(request("broken json")).result(), http::status::bad_request);
  auto json = nlohmann::json::parse(kBody);
  json["event_id"] = "forged";
  EXPECT_EQ(handler.handle(request(json.dump())).result(), http::status::bad_request);
  json.erase("event_id");
  json["occurred_at"] = "invalid";
  EXPECT_EQ(handler.handle(request(json.dump())).result(), http::status::bad_request);
  json["occurred_at"] = 42;
  EXPECT_EQ(handler.handle(request(json.dump())).result(), http::status::bad_request);
  EXPECT_EQ(handler
                .handle(request(
                    R"({"source_ref":"a","source_ref":"b","raw_code":"ALARM","occurred_at":"2020-01-01T00:00:00Z"})"))
                .result(),
            http::status::bad_request);
  EXPECT_EQ(client()->diagnostics().pending_events, 0);
}

TEST_F(Webhook, BoundedLedgerBackpressuresAndDiagnosticsRequireAuthentication) {
  client().reset();
  options().max_idempotency_keys = 1;
  client() = std::make_unique<platform::connector::Client>(options());
  const Handler handler(*client(), kToken);
  EXPECT_EQ(handler.handle(request()).result(), http::status::accepted);
  auto second = request();
  second.set("Idempotency-Key", "upstream-2");
  EXPECT_EQ(handler.handle(second).result(), http::status::service_unavailable);
  EXPECT_EQ(handler.handle(request()).result(), http::status::accepted);
  auto input = request();
  input.method(http::verb::get);
  input.target("/healthz");
  EXPECT_EQ(handler.handle(input).result(), http::status::ok);
  input.target("/metrics");
  EXPECT_NE(handler.handle(input).body().find("psim_connector_pending_events 1"), std::string::npos);
  input.target("/missing");
  EXPECT_EQ(handler.handle(input).result(), http::status::not_found);
  input.target("/v1/events");
  EXPECT_EQ(handler.handle(input).result(), http::status::method_not_allowed);
  input = request();
  input.set(http::field::content_type, "text/plain");
  EXPECT_EQ(handler.handle(input).result(), http::status::unsupported_media_type);
}
}  // namespace
}  // namespace psim::connectors::webhook
