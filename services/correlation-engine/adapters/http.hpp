#pragma once

#include <boost/beast/http.hpp>

#include "repository.hpp"

namespace psim::correlation::management {
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

class Api {
 public:
  explicit Api(Repository* repository) : repository_(repository) {}

  asio::awaitable<p::Result<Response>> handle(Request request, p::security::Principal principal);
  static Response problem(const p::Error& error);

 private:
  asio::awaitable<p::Result<Response>> route(Request request, p::security::Principal principal);
  Repository* repository_;
};
}  // namespace psim::correlation::management
