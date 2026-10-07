#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <functional>
#include <string>
#include <string_view>

#include "psim/platform/error.hpp"

namespace psim::platform::runtime::detail {

struct AdminResponse {
  unsigned status = 200;
  std::string content_type = "application/json";
  std::string body;
};

/// Answers GET and HEAD requests by target path (no query); runs on the server's executor.
using AdminHandler = std::function<AdminResponse(std::string_view target)>;

/// HTTP/1.1 server of the admin endpoints: keep-alive, read timeout, GET and HEAD only.
class AdminServer {
 public:
  AdminServer(boost::asio::any_io_executor executor, AdminHandler handler);

  /// Bind "host:port" (port 0 picks a free one) and start accepting; returns the bound port.
  Result<unsigned short> listen(std::string_view address);

  void close();

 private:
  boost::asio::any_io_executor executor_;
  AdminHandler handler_;
  boost::asio::ip::tcp::acceptor acceptor_;
};

}  // namespace psim::platform::runtime::detail
