#pragma once

#include <boost/beast/http.hpp>

#include <string>

#include "psim/platform/connector/client.hpp"

namespace psim::connectors::webhook {
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

class Handler final {
 public:
  Handler(platform::connector::Client& client, std::string token);
  Response handle(const Request& request) const;

 private:
  platform::connector::Client* client_;
  std::string token_;
};
}  // namespace psim::connectors::webhook
