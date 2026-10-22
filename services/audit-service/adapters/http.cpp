#include "http.hpp"

#include <google/protobuf/util/json_util.h>
#include <psim/api/v1/audit.pb.h>

#include <charconv>
#include <cstddef>
#include <exception>
#include <expected>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::audit_service {
namespace {
std::string decode(std::string_view value) {
  std::string result;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%') {
      if (i + 2 >= value.size()) {
        throw std::invalid_argument("query escape");
      }
      unsigned int ch = 0;
      const auto digits = value.substr(i + 1, 2);
      const auto parsed = std::from_chars(digits.begin(), digits.end(), ch, 16);
      if (parsed.ec != std::errc{} || parsed.ptr != digits.end() || ch == 0) {
        throw std::invalid_argument("query escape");
      }
      result += static_cast<char>(ch);
      i += 2;
    } else {
      result += value[i] == '+' ? ' ' : value[i];
    }
  }
  return result;
}
}  // namespace

Response Api::problem(const p::Error& error) {
  const auto& info = p::info(error.code());
  Response result(static_cast<http::status>(info.http_status), 11);
  result.set(http::field::content_type, "application/problem+json");
  result.body() =
      Json{{"type", "about:blank"}, {"title", info.title}, {"status", info.http_status}, {"code", error.name()}}.dump();
  return result;
}

asio::awaitable<p::Result<Response>> Api::handle(Request request, p::security::Principal principal) {
  try {
    const std::string target(request.target());
    if (target.size() > 8192) {
      throw std::invalid_argument("target limit");
    }
    const auto question = target.find('?');
    const auto path = target.substr(0, question);
    std::string operation;
    std::string type;
    if (path == "/v1/audit/records" && request.method() == http::verb::get) {
      operation = "search";
      type = "psim.api.v1.SearchAuditRecordsRequest";
    } else if (path == "/v1/audit/records:export" && request.method() == http::verb::get) {
      operation = "export";
      type = "psim.api.v1.ExportAuditRecordsRequest";
    } else if (path == "/v1/audit:verify" && request.method() == http::verb::post) {
      operation = "verify";
      type = "psim.api.v1.VerifyAuditIntegrityRequest";
    } else {
      co_return p::fail(p::ErrorCode::kCommonNotFound, "unknown audit endpoint");
    }
    Json body = Json::object();
    if (request.method() == http::verb::post) {
      if (question != std::string::npos || request[http::field::content_type] != "application/json") {
        throw std::invalid_argument("JSON required");
      }
      body = Json::parse(request.body().empty() ? "{}" : request.body());
    } else {
      auto rest = question == std::string::npos ? std::string_view{} : std::string_view(target).substr(question + 1);
      std::set<std::string> names;
      while (!rest.empty()) {
        const auto amp = rest.find('&');
        const auto pair = rest.substr(0, amp);
        const auto equal = pair.find('=');
        if (equal == std::string_view::npos) {
          throw std::invalid_argument("query");
        }
        const auto name = decode(pair.substr(0, equal));
        const auto value = decode(pair.substr(equal + 1));
        if (!names.insert(name).second) {
          throw std::invalid_argument("duplicate query field");
        }
        if (name.starts_with("query.")) {
          body["query"][name.substr(6)] = value;
        } else if (name == "pageSize") {
          int size = 0;
          const auto parsed = std::from_chars(std::string_view(value).begin(), std::string_view(value).end(), size);
          if (parsed.ec != std::errc{} || parsed.ptr != std::string_view(value).end()) {
            throw std::invalid_argument("page size");
          }
          body[name] = size;
        } else {
          body[name] = value;
        }
        rest = amp == std::string_view::npos ? std::string_view{} : rest.substr(amp + 1);
      }
    }
    (void)api::v1::SearchAuditRecordsRequest::descriptor();
    const auto* descriptor = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type);
    const std::unique_ptr<google::protobuf::Message> parsed(
        google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor)->New());
    if (!google::protobuf::util::JsonStringToMessage(body.dump(), parsed.get()).ok()) {
      throw std::invalid_argument("request fields");
    }
    std::string normalized;
    if (!google::protobuf::util::MessageToJsonString(*parsed, &normalized).ok()) {
      throw std::invalid_argument("request encoding");
    }
    body = Json::parse(normalized);
    auto result = co_await repository_->request(std::move(principal), operation, body);
    if (!result) {
      co_return std::unexpected(result.error());
    }
    Response response(http::status::ok, 11);
    if (operation == "export") {
      response.set(http::field::content_type, "application/x-ndjson");
      response.set(http::field::content_disposition, "attachment; filename=psim-audit.jsonl");
      response.body() = result->at("jsonl").get<std::string>();
    } else {
      response.set(http::field::content_type, "application/json");
      response.body() = result->dump();
    }
    co_return response;
  } catch (const std::exception&) {
    co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid audit request or stored record");
  }
}
}  // namespace psim::audit_service
