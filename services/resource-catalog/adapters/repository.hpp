#pragma once

#include <boost/asio/awaitable.hpp>
#include <google/protobuf/message.h>
#include <psim/catalog/v1/catalog.pb.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "model.hpp"
#include "psim/config/v1/config.pb.h"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/pg/transaction.hpp"

namespace psim::resource_catalog {
namespace asio = boost::asio;
using Encode = std::function<p::kafka::Record(std::string, std::string, const google::protobuf::Message&)>;

struct Mutation {
  p::security::Principal principal;
  std::string key{};
  std::string fingerprint{};
  std::uint64_t expected = 0;
  std::string action = "save";
};

class Repository {
 public:
  Repository(p::pg::Pool* pool, Encode encode) : pool_(pool), encode_(std::move(encode)) {}

  asio::awaitable<p::Result<void>> initialize();
  asio::awaitable<p::Result<Json>> get(p::security::Principal principal, std::string kind, std::string id);
  asio::awaitable<p::Result<Json>> device_view(p::security::Principal principal, std::string id);
  asio::awaitable<p::Result<Page>> list(p::security::Principal principal, std::string kind, Json filters,
                                        std::size_t size, std::string after);
  asio::awaitable<p::Result<Json>> change(Resource resource, Mutation mutation);
  asio::awaitable<p::Result<void>> project(catalog::v1::CatalogRecord record);
  asio::awaitable<p::Result<void>> tick(Time now);
  asio::awaitable<p::Result<Json>> start_import(Mutation mutation, std::vector<Json> rows, bool dry_run);
  asio::awaitable<p::Result<Json>> get_import(p::security::Principal principal, std::string id);
  asio::awaitable<p::Result<void>> import_once();
  asio::awaitable<p::Result<void>> reserve_file(p::security::Principal principal, Json file);
  asio::awaitable<p::Result<Json>> attach_file(Json file, double width, double height, Mutation mutation);
  asio::awaitable<p::Result<Json>> file(p::security::Principal principal, std::string floor, std::uint64_t version);
  asio::awaitable<p::Result<Page>> files(p::security::Principal principal, std::string floor, std::size_t size,
                                         std::string after);
  asio::awaitable<p::Result<void>> cleanup_files(std::function<asio::awaitable<p::Result<void>>(std::string)> remove);
  asio::awaitable<p::Result<void>> denied(p::security::Principal principal, std::string object);

  asio::awaitable<p::Result<Json>> configurations(p::security::Principal principal, std::string kind,
                                                  std::string id = {});
  asio::awaitable<p::Result<Json>> configure(config::v1::ConfigRecord record, std::string kind, std::string id,
                                             Mutation mutation);
  asio::awaitable<p::Result<void>> project_configuration(config::v1::ConfigRecord record);

  [[nodiscard]] p::pg::MessageStore messages() const { return messages_; }

 private:
  static asio::awaitable<p::Result<Json>> load(p::pg::Transaction* tx, std::string tenant, std::string kind,
                                               std::string id);
  static asio::awaitable<p::Result<std::string>> scope_zone(p::pg::Transaction* tx, std::string tenant, Json body);
  static asio::awaitable<p::Result<void>> lock(p::pg::Transaction* tx, std::string tenant);
  asio::awaitable<p::Result<Json>> apply(p::pg::Transaction* tx, Resource resource, Mutation mutation, Time now);
  asio::awaitable<p::Result<void>> publish(p::pg::Transaction* tx, Resource resource, Mutation mutation, Time now);
  asio::awaitable<p::Result<void>> audit(p::pg::Transaction* tx, Mutation mutation, std::string kind, std::string id,
                                         std::string site, Time now, bool denied = false, Json snapshot = {},
                                         std::string cause = {});
  static asio::awaitable<p::Result<std::optional<Json>>> previous(p::pg::Transaction* tx, Mutation mutation);
  static asio::awaitable<p::Result<void>> remember(p::pg::Transaction* tx, Mutation mutation, Json response);
  p::pg::Pool* pool_;
  Encode encode_;
  p::pg::MessageStore messages_{"catalog"};
};
}  // namespace psim::resource_catalog
