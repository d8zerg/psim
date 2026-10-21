#pragma once
#include <nlohmann/json.hpp>

#include <functional>

#include "psim/platform/authz/policy.hpp"
#include "psim/platform/clickhouse/client.hpp"

namespace psim::event_history {
namespace p = platform;
using Json = nlohmann::json;
using Denial = std::function<boost::asio::awaitable<p::Result<void>>(p::security::Principal, std::string)>;

class Search {
 public:
  Search(p::clickhouse::Pool* pool, Denial denial) : pool_(pool), denial_(std::move(denial)) {}

  boost::asio::awaitable<p::Result<Json>> read(p::security::Principal principal, std::string operation, std::string id,
                                               Json input);

 private:
  p::clickhouse::Pool* pool_;
  Denial denial_;
};

// Only normalized request fields and verified identity enter the query builder.
p::Result<p::clickhouse::Read> search_query(const p::security::Principal& principal, std::string_view operation,
                                            std::string_view id, const Json& input);
}  // namespace psim::event_history
