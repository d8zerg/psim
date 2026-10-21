#pragma once
#include <boost/beast/http.hpp>

#include "search.hpp"

namespace psim::event_history {
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

class Api {
 public:
  explicit Api(Search* search) : search_(search) {}

  boost::asio::awaitable<p::Result<Response>> handle(Request request, p::security::Principal principal);
  static Response problem(const p::Error& error);

 private:
  Search* search_;
};
}  // namespace psim::event_history
