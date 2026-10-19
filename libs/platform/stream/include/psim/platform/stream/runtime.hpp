#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <opentelemetry/trace/span.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/runtime.hpp"

namespace psim::platform::stream {
namespace asio = boost::asio;
using Span = opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>;

/// Common infrastructure settings; each process still owns its handlers and repositories.
inline constexpr std::string_view kSettings = R"({"type":"object","additionalProperties":false,"properties":{
 "brokers":{"type":"string","default":"kafka-1:19092"},
 "registry":{"type":"string","default":"http://registry:8080/apis/ccompat/v7"},
 "registry_ca_file":{"type":"string","default":""},
 "registry_certificate_file":{"type":"string","default":""},
 "registry_private_key_file":{"type":"string","default":""},
 "allow_plaintext":{"type":"boolean","default":false},
 "kafka_ca_file":{"type":"string","default":""},
 "kafka_certificate_file":{"type":"string","default":""},
 "kafka_private_key_file":{"type":"string","default":""},
 "pg_ca_file":{"type":"string","default":""},
 "pg_certificate_file":{"type":"string","default":""},
 "pg_private_key_file":{"type":"string","default":""},
 "pg_host":{"type":"string","default":"postgres"},
 "pg_port":{"type":"string","default":"5432"},
 "pg_database":{"type":"string","default":"psim"},
 "pg_user":{"type":"string","default":"psim"},
 "pg_max_result_bytes":{"type":"integer","minimum":1048576,"maximum":134217728,"default":16777216},
 "pg_password_file":{"type":"string","x-psim-secret":true},
 "listen_port":{"type":"integer","minimum":1,"maximum":65535,"default":8080},
 "tls_directory":{"type":"string","default":"/repo/.cache/event-flow"},
 "issuer":{"type":"string","default":"http://localhost:8180/realms/psim"},
 "jwks_url":{"type":"string","default":"http://keycloak:8080/realms/psim/protocol/openid-connect/certs"}
}})";

[[nodiscard]] std::string uuid7();
[[nodiscard]] std::string uuid5(std::string_view space, const std::vector<std::string>& parts);
[[nodiscard]] bool uuid(std::string_view value);
[[nodiscard]] std::string sha256(std::string_view bytes);
[[nodiscard]] std::string json(const google::protobuf::Message& message);
[[nodiscard]] Span span(const runtime::Context& context, std::string_view name, const kafka::Headers& headers = {});
void stamp(kafka::Headers& headers, std::string_view stage);

/// One asynchronous Kafka stage on one data-path shard. Control-thread lifecycle is marshalled
/// explicitly; all native handles are destroyed on that shard after the processor drains.
class Worker : public runtime::Component, public kafka::Handler {
 public:
  Worker(const runtime::Context& context, std::string stage, std::string input,
         kafka::Semantics semantics = kafka::Semantics::kExactlyOnce, bool broadcast = false);

  [[nodiscard]] std::string_view name() const override { return stage_; }

  [[nodiscard]] bool ready() const override { return ready_.load(); }

  asio::awaitable<Result<void>> start() final;
  asio::awaitable<void> drain() final;

 protected:
  virtual asio::awaitable<Result<void>> initialize() { co_return Result<void>{}; }

  virtual asio::awaitable<void> shutdown() { co_return; }

  [[nodiscard]] const runtime::Context& context() const { return context_; }

  [[nodiscard]] const asio::any_io_executor& executor() const { return executor_; }

  [[nodiscard]] kafka::ClientOptions kafka_options(std::string client_id) const;
  asio::awaitable<Result<void>> schema(std::string topic);
  asio::awaitable<Result<std::unique_ptr<pg::Pool>>> database();
  [[nodiscard]] kafka::Record record(std::string topic, std::string key, const google::protobuf::Message& value,
                                     kafka::Headers headers) const;

  template <typename T>
  Result<T> decode(const kafka::Message& message) const {
    return kafka::ProtobufSerde<T>(1).deserialize(message.value());
  }

  kafka::Producer* producer() const { return producer_.get(); }

 private:
  const runtime::Context& context_;
  asio::any_io_executor executor_;

  asio::awaitable<Result<void>> open();
  asio::awaitable<void> run();
  asio::awaitable<void> stop();
  std::string stage_;
  std::string input_;
  kafka::Semantics semantics_;
  bool broadcast_;
  std::unique_ptr<kafka::Producer> producer_;
  std::unique_ptr<kafka::Consumer> consumer_;
  std::unique_ptr<kafka::Processor> processor_;
  std::map<std::string, std::int32_t> schemas_;
  std::atomic_bool ready_{false};
  bool running_ = false;
  asio::steady_timer stopped_;
};
}  // namespace psim::platform::stream
