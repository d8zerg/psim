#pragma once
#include <boost/beast/http.hpp>

#include "escalation.hpp"
#include "repository.hpp"

namespace psim::response_engine {
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

class Api {
 public:
  explicit Api(Repository* repository, Escalation* escalation = nullptr)
      : repository_(repository), escalation_(escalation) {}

  asio::awaitable<p::Result<Response>> handle(Request request, p::security::Principal principal);
  static Response problem(const p::Error& error);

 private:
  Repository* repository_;
  Escalation* escalation_;
};
}  // namespace psim::response_engine
