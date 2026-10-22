#pragma once
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/cancel_after.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "psim/platform/security/tls.hpp"
#include "routes.hpp"

namespace psim::api_gateway {
namespace asio = boost::asio;
namespace beast = boost::beast;

struct Endpoint {
  std::string host;
  std::string port;
};

/// Only end-to-end application headers cross the public boundary.
void request_fields(const http::fields& input, http::fields& output);
void response_fields(const http::fields& input, http::fields& output);

// Both parsers and serializers use a fixed chunk. Backpressure is the awaited socket write.
template <bool Request, typename Input, typename Output>
asio::awaitable<bool> transfer(Input* input_stream, beast::flat_buffer* pending_buffer,
                               http::parser<Request, http::buffer_body>* input_parser, Output* output_stream,
                               http::serializer<Request, http::buffer_body>* output_serializer,
                               http::message<Request, http::buffer_body>* output_message, bool* invalid = nullptr,
                               bool* oversized = nullptr) {
  auto& input = *input_stream;
  auto& pending = *pending_buffer;
  auto& parser = *input_parser;
  auto& output = *output_stream;
  auto& serializer = *output_serializer;
  auto& message = *output_message;
  std::array<char, 65536> chunk{};
  while (!parser.is_done()) {
    parser.get().body().data = chunk.data();
    parser.get().body().size = chunk.size();
    auto [error, count] = co_await http::async_read_some(input, pending, parser, asio::as_tuple(asio::use_awaitable));
    (void)count;
    if (error && error != http::error::need_buffer) {
      if (invalid) {
        *invalid = true;
      }
      if (oversized) {
        *oversized = error == http::error::body_limit;
      }
      co_return false;
    }
    const auto filled = chunk.size() - parser.get().body().size;
    if (filled == 0 && !parser.is_done()) {
      continue;
    }
    message.body().data = chunk.data();
    message.body().size = filled;
    message.body().more = !parser.is_done();
    auto [written, bytes] = co_await http::async_write(output, serializer, asio::as_tuple(asio::use_awaitable));
    (void)bytes;
    if (written && written != http::error::need_buffer) {
      co_return false;
    }
  }
  // Empty bodies still finish the serializer after the header.
  if (!serializer.is_done()) {
    message.body().data = nullptr;
    message.body().size = 0;
    message.body().more = false;
    auto [error, count] = co_await http::async_write(output, serializer, asio::as_tuple(asio::use_awaitable));
    (void)count;
    if (error) {
      co_return false;
    }
  }
  co_return true;
}

template <typename Client>
asio::awaitable<bool> proxy(Client* connection, beast::flat_buffer* input_buffer,
                            http::request_parser<http::buffer_body>* request_parser, Endpoint endpoint,
                            asio::ssl::context* context, std::string origin, bool floor_plan, bool* started,
                            bool* invalid, bool* oversized) {
  auto& client = *connection;
  auto& incoming = *input_buffer;
  auto& parser = *request_parser;
  auto& tls = *context;
  const auto executor = co_await asio::this_coro::executor;
  asio::ip::tcp::resolver resolver(executor);
  // Endpoints are deployment configuration; no client-controlled URL or redirect is followed.
  auto [resolved, addresses] = co_await resolver.async_resolve(
      endpoint.host, endpoint.port, asio::cancel_after(std::chrono::seconds(5), asio::as_tuple(asio::use_awaitable)));
  if (resolved) {
    co_return false;
  }
  beast::ssl_stream<beast::tcp_stream> upstream(executor, tls);
  beast::get_lowest_layer(upstream).expires_after(std::chrono::seconds(60));
  auto [connected, address] =
      co_await beast::get_lowest_layer(upstream).async_connect(addresses, asio::as_tuple(asio::use_awaitable));
  (void)address;
  if (connected || !platform::security::configure_hostname(upstream.native_handle(), endpoint.host)) {
    co_return false;
  }
  auto [handshake] =
      co_await upstream.async_handshake(asio::ssl::stream_base::client, asio::as_tuple(asio::use_awaitable));
  if (handshake) {
    co_return false;
  }
  http::request<http::buffer_body> request(parser.get().method(), parser.get().target(), 11);
  request_fields(parser.get().base(), request.base());
  request.set(http::field::host, endpoint.host + ":" + endpoint.port);
  request.keep_alive(false);
  if (const auto length = parser.content_length()) {
    request.content_length(length);
  } else if (!parser.is_done()) {
    request.chunked(true);
  }
  request.body().more = !parser.is_done();
  http::request_serializer<http::buffer_body> send(request);
  auto [header, bytes] = co_await http::async_write_header(upstream, send, asio::as_tuple(asio::use_awaitable));
  (void)bytes;
  if (header || !co_await transfer(&client, &incoming, &parser, &upstream, &send, &request, invalid, oversized)) {
    co_return false;
  }
  beast::flat_buffer pending;
  http::response_parser<http::buffer_body> response;
  response.body_limit(floor_plan ? 20ULL * 1024 * 1024 : 64ULL * 1024 * 1024);
  response.header_limit(16384);
  auto [read, count] =
      co_await http::async_read_header(upstream, pending, response, asio::as_tuple(asio::use_awaitable));
  (void)count;
  if (read || response.get().result_int() < 200
      || (response.get().result_int() >= 300 && response.get().result_int() < 400
          && response.get().result_int() != 304)) {
    co_return false;
  }
  http::response<http::buffer_body> outgoing(response.get().result(), 11);
  response_fields(response.get().base(), outgoing.base());
  security_headers(outgoing.base(), origin);
  outgoing.keep_alive(false);
  if (auto length = response.content_length()) {
    outgoing.content_length(length);
  } else if (!response.is_done()) {
    outgoing.chunked(true);
  }
  if (floor_plan && parser.get().method() == http::verb::get && outgoing.result() == http::status::ok) {
    const auto type = outgoing[http::field::content_type];
    if (type != "image/png" && type != "image/jpeg") {
      co_return false;
    }
    outgoing.set(http::field::content_disposition,
                 "attachment; filename=\"floor-plan" + std::string(type == "image/png" ? ".png" : ".jpg") + "\"");
  }
  outgoing.body().more = !response.is_done();
  http::response_serializer<http::buffer_body> receive(outgoing);
  *started = true;
  auto [sent, length] = co_await http::async_write_header(client, receive, asio::as_tuple(asio::use_awaitable));
  (void)length;
  if (sent) {
    co_return false;
  }
  co_return co_await transfer(&upstream, &pending, &response, &client, &receive, &outgoing);
}
}  // namespace psim::api_gateway
