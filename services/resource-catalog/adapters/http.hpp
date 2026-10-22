#pragma once

#include <boost/beast/http.hpp>

#include <set>
#include <string>

#include "connector_admin.hpp"
#include "file_store.hpp"
#include "model.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "repository.hpp"

namespace psim::resource_catalog {
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

class Api {
 public:
  Api(Repository* repository, FileStore* files, ConnectorAdmin* admin, platform::async::BlockingPool* blocking,
      std::set<std::string> taxonomy = {})
      : taxonomy_(std::move(taxonomy)), repository_(repository), files_(files), admin_(admin), blocking_(blocking) {}

  boost::asio::awaitable<platform::Result<Response>> handle(Request request, platform::security::Principal principal,
                                                            std::string token);
  static Response problem(const platform::Error& error);

 private:
  boost::asio::awaitable<platform::Result<Response>> route(Request request, platform::security::Principal principal,
                                                           std::string token);
  std::set<std::string> taxonomy_;
  Repository* repository_;
  FileStore* files_;
  ConnectorAdmin* admin_;
  platform::async::BlockingPool* blocking_;
};
}  // namespace psim::resource_catalog
