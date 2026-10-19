#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <nlohmann/json.hpp>
#include <openssl/prov_ssl.h>
#include <openssl/ssl.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../common/settings.hpp"
#include "handler.hpp"
#include "psim/platform/connector/client.hpp"
#include "psim/platform/connector/grpc_process.hpp"
#include "psim/platform/security/secret.hpp"

namespace {
namespace net = boost::asio;
namespace ssl = net::ssl;
namespace beast = boost::beast;
namespace http = beast::http;
using Tcp = net::ip::tcp;
using Stream = beast::ssl_stream<beast::tcp_stream>;

net::awaitable<void> serve(Tcp::socket socket, ssl::context* tls, const psim::connectors::webhook::Handler* handler) {
  Stream stream(std::move(socket), *tls);
  try {
    beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(5));
    co_await stream.async_handshake(ssl::stream_base::server, net::use_awaitable);
    http::request_parser<http::string_body> parser;
    parser.body_limit(65536);
    parser.header_limit(8192);
    beast::flat_buffer buffer(8192);
    beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(5));
    co_await http::async_read(stream, buffer, parser, net::use_awaitable);
    const auto result = handler->handle(parser.get());
    beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(5));
    co_await http::async_write(stream, result, net::use_awaitable);
  } catch (const std::exception&) {
    co_return;
  }
}

net::awaitable<void> accept(Tcp::acceptor* acceptor, ssl::context* tls,
                            const psim::connectors::webhook::Handler* handler, std::atomic_size_t* active) {
  for (;;) {
    auto socket = co_await acceptor->async_accept(net::make_strand(acceptor->get_executor()), net::use_awaitable);
    if (active->load() >= 64) {
      boost::system::error_code ignored;
      (void)socket.close(ignored);
      continue;
    }
    ++*active;
    auto executor = socket.get_executor();
    net::co_spawn(executor, serve(std::move(socket), tls, handler),
                  [active](const std::exception_ptr& /*error*/) { --*active; });
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): argc guards argv access
    if (argc == 2 && std::string(argv[1]) == "--version") {
      std::cout << "psim-webhook " << PSIM_CONNECTOR_VERSION << '\n';
      return 0;
    }
    if (argc != 1) {
      throw std::invalid_argument("webhook accepts only --version; use environment configuration");
    }
    using psim::connectors::require;
    namespace sdk = psim::platform::connector;
    const sdk::GrpcProcess grpc;
    auto options = psim::connectors::options("webhook-1");
    require(!options.buffer_directory.empty(), "webhook requires a persistent buffer directory");
    sdk::Client client(
        options, {.rejection = [](const auto& rejection) {
          std::cerr << nlohmann::json{{"rejected_event", rejection.event_id()}, {"code", rejection.code()}}.dump()
                    << '\n';
        }});
    auto token = psim::platform::security::read_secret(psim::connectors::setting("PSIM_WEBHOOK_TOKEN_FILE"));
    require(token.has_value(), "webhook token unavailable");
    const psim::connectors::webhook::Handler handler(client, std::string(token->view()));
    net::io_context io;
    ssl::context tls(ssl::context::tls_server);
    require(SSL_CTX_set_min_proto_version(tls.native_handle(), TLS1_3_VERSION) == 1, "webhook TLS setup failed");
    tls.use_certificate_chain_file(psim::connectors::setting("PSIM_WEBHOOK_CERTIFICATE_FILE"));
    auto key = psim::platform::security::read_secret(psim::connectors::setting("PSIM_WEBHOOK_PRIVATE_KEY_FILE"));
    require(key.has_value(), "webhook TLS key unavailable");
    tls.use_private_key(net::buffer(key->view()), ssl::context::pem);
    require(SSL_CTX_check_private_key(tls.native_handle()) == 1, "webhook TLS key mismatch");
    const auto port = std::stoul(psim::connectors::setting("PSIM_WEBHOOK_PORT", "8443"));
    require(port <= 65535, "invalid webhook port");
    Tcp::acceptor acceptor(io, {net::ip::make_address(psim::connectors::setting("PSIM_WEBHOOK_ADDRESS", "127.0.0.1")),
                                static_cast<unsigned short>(port)});
    std::atomic_size_t active{0};
    net::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](const auto& /*error*/, int /*signal*/) { io.stop(); });
    net::co_spawn(io, accept(&acceptor, &tls, &handler, &active), [&](const std::exception_ptr& error) {
      if (error) {
        io.stop();
      }
    });
    require(client.start().has_value(), "webhook SDK startup failed");
    std::cout << nlohmann::json{{"port", acceptor.local_endpoint().port()}}.dump() << '\n';
    std::cout.flush();
    {
      std::vector<std::jthread> workers;
      workers.reserve(4);
      for (int index = 0; index < 4; ++index) {
        workers.emplace_back([&] { io.run(); });
      }
    }
    const bool drained = client.flush(std::chrono::seconds(5));
    client.stop();
    return drained ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
