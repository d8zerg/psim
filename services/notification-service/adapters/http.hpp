#pragma once
#include <boost/beast/http.hpp>

#include "repository.hpp"

namespace psim::notification_service {
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

class Api {
 public:
  explicit Api(Repository* repository) : repository_(repository) {}

  asio::awaitable<p::Result<Response>> handle(Request request, p::security::Principal principal);
  static Response problem(const p::Error& error);

 private:
  Repository* repository_;
};
}  // namespace psim::notification_service
