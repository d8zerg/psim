#pragma once
#include <functional>
#include <string>

#include "chain.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::audit_service {
namespace p = platform;
namespace asio = boost::asio;
using Encode = std::function<p::kafka::Record(std::string, std::string, const google::protobuf::Message&)>;

asio::awaitable<p::Result<void>> migrate(p::pg::Connection* connection, std::string role);

class Repository {
 public:
  Repository(p::pg::Pool* pool, p::async::BlockingPool* blocking, Encode encode, Signer signer, Archive archive,
             std::int64_t interval_ms = 300000, std::int64_t checkpoint_records = 10000)
      : pool_(pool),
        blocking_(blocking),
        encode_(std::move(encode)),
        signer_(std::move(signer)),
        archive_(std::move(archive)),
        interval_ms_(interval_ms),
        checkpoint_records_(checkpoint_records) {}

  asio::awaitable<p::Result<void>> initialize();
  asio::awaitable<p::Result<void>> apply(audit::v1::AuditEventRecord input, std::string key);
  asio::awaitable<p::Result<void>> tick();
  asio::awaitable<p::Result<Json>> request(p::security::Principal principal, std::string operation, Json body);

  p::pg::MessageStore messages() const { return messages_; }

 private:
  asio::awaitable<p::Result<void>> checkpoint(p::pg::Transaction* tx, Checkpoint point);
  static asio::awaitable<p::Result<void>> append(p::pg::Transaction* tx, audit::v1::AuditEventRecord input);
  asio::awaitable<p::Result<void>> record_access(p::security::Principal principal, std::string action, bool denied,
                                                 Json details = Json::object());
  asio::awaitable<p::Result<Json>> inspect(p::pg::Transaction* tx, std::string tenant, Json body, bool exporting);
  p::pg::Pool* pool_;
  p::async::BlockingPool* blocking_;
  Encode encode_;
  Signer signer_;
  Archive archive_;
  std::int64_t interval_ms_;
  std::int64_t checkpoint_records_;
  std::map<std::string, std::int64_t> next_checks_;
  p::pg::MessageStore messages_{"audit"};
};
}  // namespace psim::audit_service
