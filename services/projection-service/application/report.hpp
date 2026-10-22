#pragma once
#include <boost/beast/http.hpp>

#include "psim/api/v1/reports.pb.h"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::projection {
namespace p = platform;
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;
using Audit = std::function<boost::asio::awaitable<p::Result<void>>(p::security::Principal, std::string, bool)>;
p::Result<api::v1::GetIncidentReportRequest> parse_request(std::string_view target);
p::Result<p::clickhouse::Read> report_query(const p::security::Principal& principal,
                                            const api::v1::GetIncidentReportRequest& request);
p::Result<api::v1::GetIncidentReportResponse> report(const p::clickhouse::ResultSet& result, bool grouped);
std::string csv(const api::v1::GetIncidentReportResponse& value);

class Api {
 public:
  Api(p::clickhouse::Pool* pool, Audit audit) : pool_(pool), audit_(std::move(audit)) {}

  boost::asio::awaitable<p::Result<Response>> handle(Request request, p::security::Principal principal);
  static Response problem(const p::Error& error);

 private:
  p::clickhouse::Pool* pool_;
  Audit audit_;
};
}  // namespace psim::projection
