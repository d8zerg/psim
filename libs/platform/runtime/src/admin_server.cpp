#include "admin_server.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/system/error_code.hpp>

#include <charconv>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::runtime::detail {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
using asio::ip::tcp;

namespace {

constexpr auto kReadTimeout = std::chrono::seconds(30);
constexpr auto kAwait = asio::as_tuple(asio::use_awaitable);

asio::awaitable<void> session(tcp::socket socket, AdminHandler handler) {
  beast::tcp_stream stream(std::move(socket));
  beast::flat_buffer buffer;
  while (true) {
    http::request<http::empty_body> request;
    stream.expires_after(kReadTimeout);
    if (auto [ec, bytes] = co_await http::async_read(stream, buffer, request, kAwait); ec) {
      break;
    }
    http::response<http::string_body> response;
    response.version(request.version());
    response.keep_alive(request.keep_alive());
    response.set(http::field::server, "psim-runtime");
    if (request.method() != http::verb::get && request.method() != http::verb::head) {
      response.result(http::status::method_not_allowed);
      response.set(http::field::allow, "GET, HEAD");
    } else {
      const std::string_view target = request.target();
      auto reply = handler(target.substr(0, target.find('?')));
      response.result(reply.status);
      response.set(http::field::content_type, reply.content_type);
      if (request.method() == http::verb::get) {
        response.body() = std::move(reply.body);
      }
    }
    response.prepare_payload();
    if (auto [ec, bytes] = co_await http::async_write(stream, response, kAwait); ec || !response.keep_alive()) {
      break;
    }
  }
  boost::system::error_code ignored;
  (void)stream.socket().shutdown(tcp::socket::shutdown_send, ignored);
}

// The acceptor outlives the loop: AdminServer owns it and closes it on shutdown.
asio::awaitable<void> accept_loop(tcp::acceptor* acceptor, AdminHandler handler) {
  while (acceptor->is_open()) {
    auto [ec, socket] = co_await acceptor->async_accept(kAwait);
    if (ec) {
      co_return;  // closed on shutdown
    }
    asio::co_spawn(acceptor->get_executor(), session(std::move(socket), handler), asio::detached);
  }
}

}  // namespace

AdminServer::AdminServer(asio::any_io_executor executor, AdminHandler handler)
    : executor_(std::move(executor)), handler_(std::move(handler)), acceptor_(executor_) {}

Result<unsigned short> AdminServer::listen(std::string_view address) {
  const auto colon = address.rfind(':');
  unsigned short port = 0;
  const auto port_text = address.substr(colon + 1);
  if (colon == std::string_view::npos
      || std::from_chars(std::to_address(port_text.begin()), std::to_address(port_text.end()), port).ec
             != std::errc{}) {
    return fail(ErrorCode::kCommonInvalidArgument, "admin.listen: expected host:port, got " + std::string(address));
  }
  boost::system::error_code ec;
  const auto host = asio::ip::make_address(std::string(address.substr(0, colon)), ec);
  if (ec) {
    return fail(ErrorCode::kCommonInvalidArgument, "admin.listen: bad address " + std::string(address));
  }
  const tcp::endpoint endpoint(host, port);
  if ((void)acceptor_.open(endpoint.protocol(), ec); !ec) {
    (void)acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
    if ((void)acceptor_.bind(endpoint, ec); !ec) {
      (void)acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    }
  }
  if (ec) {
    return fail(ErrorCode::kCommonUnavailable, "admin.listen " + std::string(address) + ": " + ec.message());
  }
  asio::co_spawn(executor_, accept_loop(&acceptor_, handler_), asio::detached);
  return acceptor_.local_endpoint().port();
}

void AdminServer::close() {
  boost::system::error_code ignored;
  (void)acceptor_.close(ignored);
}

}  // namespace psim::platform::runtime::detail
