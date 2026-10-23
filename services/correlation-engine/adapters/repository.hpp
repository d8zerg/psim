#pragma once

#include <boost/asio/awaitable.hpp>
#include <google/protobuf/message.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/security/jwt.hpp"
#include "rules.hpp"

namespace psim::correlation::management {
namespace p = platform;
namespace asio = boost::asio;
using Json = nlohmann::json;
using Encode = std::function<p::kafka::Record(std::string, std::string, const google::protobuf::Message&)>;

struct Job {
  std::string tenant;
  std::string id;
  std::string lease;
  Json principal;
  Json request;
  Json rule;
  Json input;
};

bool can_read_rule(const rules::Rule& rule, const p::security::Principal& principal);
bool can_read(const p::security::Principal& principal);
bool can_write(const p::security::Principal& principal);
bool can_read_site(const p::security::Principal& principal, std::string_view site);

class Repository {
 public:
  Repository(p::pg::Pool* pool, Encode encode, std::set<std::string> taxonomy)
      : pool_(pool), encode_(std::move(encode)), taxonomy_(std::move(taxonomy)) {}

  asio::awaitable<p::Result<void>> initialize();
  asio::awaitable<p::Result<Json>> request(p::security::Principal principal, std::string operation, Json body,
                                           std::string key = {}, std::optional<std::uint64_t> expected = {});
  asio::awaitable<p::Result<std::optional<Job>>> take_job();
  asio::awaitable<p::Result<void>> renew(Job job);
  asio::awaitable<p::Result<void>> save_input(Job job, Json input);
  asio::awaitable<p::Result<void>> finish(Job job, p::Result<rules::ReplayResult> result);

  p::pg::MessageStore messages() const { return messages_; }

 private:
  asio::awaitable<p::Result<Json>> apply(p::pg::Transaction* tx, p::security::Principal principal,
                                         std::string operation, Json body, std::optional<std::uint64_t> expected);
  asio::awaitable<p::Result<Json>> jobs(p::pg::Transaction* tx, p::security::Principal principal, std::string operation,
                                        Json body);
  asio::awaitable<p::Result<void>> publish(p::pg::Transaction* tx, p::security::Principal principal,
                                           processing::v1::RuleSet* output);
  asio::awaitable<p::Result<void>> denied(p::security::Principal principal, std::string operation);
  asio::awaitable<p::Result<void>> audit(p::pg::Transaction* tx, p::security::Principal principal, std::string action,
                                         std::string id, Json details);
  p::pg::Pool* pool_;
  Encode encode_;
  std::set<std::string> taxonomy_;
  p::pg::MessageStore messages_{"rules"};
};
}  // namespace psim::correlation::management
