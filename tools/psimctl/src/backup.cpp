#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/s3/client.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/secret.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::ctl {
namespace {
constexpr std::array<std::string_view, 6> kComponents{"plans",        "postgres",       "clickhouse",
                                                      "kafka-config", "product-config", "identity-pki"};
constexpr std::size_t kChunk = static_cast<const std::size_t>(8 * 1024 * 1024);

bool safe_name(std::string_view value) {
  return !value.empty() && value.size() <= 128 && std::ranges::all_of(value, [](unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.'
           || ch == '_';
  });
}

void validate(const Json& config) {
  const auto& backup = config.at("backup");
  const auto& target = backup.at("storage");
  require(target.at("bucket") != "psim-plans", "backup bucket must differ from working plans");
  const auto endpoint = target.at("endpoint").get<std::string>();
  const auto working = config.at("plans").at("endpoint").get<std::string>();
  const auto authority = [](const std::string& value) {
    const auto begin = value.find("://");
    require(begin != std::string::npos, "invalid storage endpoint");
    return value.substr(begin + 3, value.find('/', begin + 3) - begin - 3);
  };
  require(authority(endpoint) != authority(working), "backup object storage must be independent");
  const auto window = backup.at("pitr_window_seconds").get<std::int64_t>();
  const auto interval = backup.at("copy_interval_seconds").get<std::int64_t>();
  const auto grace = backup.at("orphan_grace_seconds").get<std::int64_t>();
  constexpr std::int64_t kProtection = 8LL * 86400;
  require(window > 0 && interval > 0 && window < kProtection && interval < kProtection && grace > window + interval
              && grace <= kProtection,
          "catalog object protection must exceed PITR window plus copy interval");
  require(safe_name(backup.at("wal_namespace").get<std::string>()), "invalid WAL namespace");
}

p::s3::Object object(const Json& entry) {
  return {.key = entry.at("key"), .size = entry.at("size"), .sha256 = entry.at("sha256")};
}

Json object(const p::s3::Object& entry) {
  return {{"key", entry.key}, {"size", entry.size}, {"sha256", entry.sha256}};
}

class Repository {
 public:
  Repository(Context* context, bool reader) : context_(context), reader_(reader) { validate(context_->config); }

  asio::awaitable<void> open() {
    const auto& backup = context_->config.at("backup");
    const auto& config = reader_ ? backup.at("read_storage") : backup.at("storage");
    require(config.at("endpoint") == backup.at("storage").at("endpoint")
                && config.at("bucket") == backup.at("storage").at("bucket"),
            "backup reader target differs");
    client_ = take(co_await p::s3::Client::create(&context_->blocking, storage(config)));
  }

  Await put_file(std::string prefix, fs::path source) {
    require(!fs::is_symlink(source) && fs::is_regular_file(source), "backup source is not a regular file");
    Json chunks = Json::array();
    Temporary const temporary;
    std::ifstream input(source, std::ios::binary);
    std::vector<char> bytes(kChunk);
    std::uint64_t total = 0;
    while (true) {
      input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      const auto size = input.gcount();
      require(size >= 0 && !input.bad(), "backup source read failed");
      if (size == 0) {
        break;
      }
      const auto clear = temporary.path("chunk");
      write_file(clear, std::string_view(bytes.data(), static_cast<std::size_t>(size)), true);
      const auto key = prefix + "/" + std::to_string(chunks.size());
      chunks.push_back(co_await put_one(key, clear, temporary.path("encrypted")));
      total += static_cast<std::uint64_t>(size);
      require(chunks.size() <= 100000, "backup artifact exceeds chunk limit");
      if (!input) {
        break;
      }
    }
    co_return Json{{"chunks", std::move(chunks)}, {"size", total}, {"sha256", file_hash(source)}};
  }

  asio::awaitable<void> get_file(Json entry, fs::path destination) {
    require(!fs::exists(destination), "restore destination exists");
    Temporary const temporary;
    const auto assembled = temporary.path("assembled");
    std::ofstream output(assembled, std::ios::binary);
    std::uint64_t size = 0;
    for (const auto& chunk : entry.at("chunks")) {
      const auto clear = temporary.path("clear");
      fs::remove(clear);
      co_await get_one(chunk, clear, temporary.path("encrypted"));
      std::ifstream const input(clear, std::ios::binary);
      output << input.rdbuf();
      require(!input.bad() && output.good(), "restored artifact write failed");
      size += fs::file_size(clear);
    }
    output.close();
    require(
        size == entry.at("size").get<std::uint64_t>() && file_hash(assembled) == entry.at("sha256").get<std::string>(),
        "restored artifact integrity mismatch");
    fs::copy_file(assembled, destination, fs::copy_options::none);
    sync_file(destination);
    sync_file(destination.parent_path());
  }

  Await manifest(std::string id) {
    require(p::stream::uuid(id), "invalid snapshot identifier");
    Temporary const temporary;
    const auto key = "snapshots/" + id + "/manifest.enc";
    const auto metadata = take(co_await client_->head(key));
    co_await get_one(object(metadata), temporary.path("manifest"), temporary.path("encrypted"));
    auto document = read_json(temporary.path("manifest"));
    require(document.at("format") == 1 && document.at("id") == id
                && document.at("deployment") == context_->config.at("deployment"),
            "snapshot identity differs");
    co_return document;
  }

  asio::awaitable<void> commit_manifest(Json manifest) {
    Temporary const temporary;
    const auto source = temporary.path("manifest");
    write_json(source, manifest);
    require(fs::file_size(source) <= kChunk, "snapshot manifest exceeds limit");
    (void)co_await put_one("snapshots/" + manifest.at("id").get<std::string>() + "/manifest.enc", source,
                           temporary.path("encrypted"));
  }

  Await put_one(std::string key, fs::path source, fs::path encrypted) {
    const auto& config = context_->config.at("backup");
    process({"/usr/bin/openssl", "cms", "-encrypt", "-binary", "-aes-256-gcm", "-stream", "-outform", "DER", "-recip",
             config.at("recipient_certificate_file"), "-keyopt", "rsa_padding_mode:oaep", "-keyopt",
             "rsa_oaep_md:sha256", "-in", source.string(), "-out", encrypted.string()});
    const auto stored = take(co_await client_->put(key, encrypted.string(), p::s3::WriteMode::kWriteOnly));
    co_return object(stored);
  }

  asio::awaitable<void> get_one(Json entry, fs::path clear, fs::path encrypted) {
    fs::remove(encrypted);
    take(co_await client_->get(object(entry), encrypted.string()));
    const auto& config = context_->config.at("backup");
    (void)take(p::security::read_secret(config.at("recipient_private_key_file").get<std::string>()));
    process({"/usr/bin/openssl", "cms", "-decrypt", "-binary", "-inform", "DER", "-recip",
             config.at("recipient_certificate_file"), "-inkey", config.at("recipient_private_key_file"), "-in",
             encrypted.string(), "-out", clear.string()});
  }

  p::s3::Client* client() { return client_.get(); }

 private:
  Context* context_;
  bool reader_;
  std::unique_ptr<p::s3::Client> client_;
};

Await copy_tree(Repository* repository, fs::path root, std::string prefix) {
  require(fs::is_directory(root) && !fs::is_symlink(root), "backup root must be a directory");
  Json entries = Json::array();
  for (const auto& item : fs::recursive_directory_iterator(root)) {
    require(!item.is_symlink() && (item.is_directory() || item.is_regular_file()),
            "backup contains a link or special file");
    Json entry;
    if (item.is_regular_file()) {
      entry = co_await repository->put_file(prefix + "/" + std::to_string(entries.size()), item.path());
    } else {
      entry = {{"directory", true}};
    }
    entry["path"] = fs::relative(item.path(), root).generic_string();
    struct stat metadata{};
    require(::stat(item.path().c_str(), &metadata) == 0, "cannot inspect backup file");
    entry["mode"] = metadata.st_mode & 0777U;
    entry["uid"] = metadata.st_uid;
    entry["gid"] = metadata.st_gid;
    entries.push_back(std::move(entry));
    require(entries.size() <= 100000, "backup file count exceeds limit");
  }
  co_return Json{{"files", std::move(entries)}};
}

asio::awaitable<void> restore_tree(Repository* repository, Json tree, fs::path root) {
  require(!fs::exists(root), "restore directory must not exist");
  std::set<std::string> names;
  for (const auto& entry : tree.at("files")) {
    const auto name = entry.at("path").get<std::string>();
    const fs::path relative(name);
    require(!relative.empty() && !relative.is_absolute() && relative.lexically_normal() == relative
                && std::ranges::none_of(relative, [](const auto& part) { return part == ".." || part == "."; })
                && names.insert(name).second,
            "unsafe backup path");
  }
  fs::create_directories(root);
  fs::permissions(root, fs::perms::owner_all);
  for (const auto& entry : tree.at("files")) {
    const auto destination = root / entry.at("path").get<std::string>();
    fs::create_directories(destination.parent_path());
    if (entry.value("directory", false)) {
      fs::create_directory(destination);
    } else {
      co_await repository->get_file(entry, destination);
    }
    fs::permissions(destination, static_cast<fs::perms>(entry.at("mode").get<unsigned int>() & 0700U));
    if (::getuid() == 0) {
      require(::chown(destination.c_str(), entry.at("uid").get<uid_t>(), entry.at("gid").get<gid_t>()) == 0,
              "restored owner assignment failed");
    }
  }
}

Await copy_plans(Context* context, Repository* repository, std::string prefix) {
  auto client = take(co_await p::s3::Client::create(&context->blocking, storage(context->config.at("plans"))));
  const auto covered = seconds();
  Json entries = Json::array();
  std::string token;
  while (true) {
    auto page = take(co_await client->list(token));
    token = std::move(page.continuation);
    for (const auto& key : page.keys) {
      const auto metadata = take(co_await client->head(key));
      Temporary const temporary;
      const auto destination = temporary.path("object");
      take(co_await client->get(metadata, destination.string()));
      auto entry = co_await repository->put_file(prefix + "/plans/" + p::stream::sha256(key), destination);
      entry["object_key"] = key;
      entries.push_back(std::move(entry));
      require(entries.size() <= 100000, "plan object count exceeds limit");
    }
    if (token.empty()) {
      break;
    }
  }
  co_return Json{{"objects", std::move(entries)}, {"covered_until_seconds", covered}};
}

p::clickhouse::PoolOptions clickhouse(const Json& config) {
  return {.endpoints = {{config.at("host"), config.value("port", std::uint16_t{9440})}},
          .user = config.at("user"),
          .password_file = config.at("password_file"),
          .tls = tls(config),
          .allow_plaintext = config.value("allow_plaintext", false),
          .query_timeout = std::chrono::hours(1)};
}

Await snapshot(Context* context, Repository* repository, Arguments args) {
  const auto id = p::stream::uuid7();
  const auto prefix = "snapshots/" + id;
  Json manifest{{"format", 1},
                {"id", id},
                {"deployment", context->config.at("deployment")},
                {"started_at_seconds", seconds()},
                {"components", Json::object()}};
  const auto component = args.one("--component");
  require(component.empty() || std::ranges::find(kComponents, component) != kComponents.end(),
          "unknown backup component");
  for (const auto name : kComponents) {
    if (!component.empty() && component != name) {
      continue;
    }
    Temporary const temporary;
    Json entry;
    if (name == "plans") {
      entry = co_await copy_plans(context, repository, prefix);
    } else if (name == "postgres") {
      auto connection = take(co_await p::pg::Connection::connect(postgres(context->config.at("postgres"))));
      auto enabled = take(co_await connection->query(
          {.sql = "SELECT current_setting('archive_mode')='on' AND NOT pg_is_in_recovery()"}));
      require(enabled.value(0, 0) == "t", "PostgreSQL WAL archiving must be enabled on the primary");
      const auto destination = temporary.path("postgres");
      process({"/usr/lib/postgresql/18/bin/pg_basebackup", "--format=plain", "--wal-method=stream", "--checkpoint=fast",
               "--no-password", "--pgdata=" + destination.string()},
              pg_environment(context->config.at("postgres")));
      process({"/usr/lib/postgresql/18/bin/pg_verifybackup", destination.string()});
      const auto completed = seconds();
      entry = co_await copy_tree(repository, destination, prefix + "/postgres");
      entry["base_backup_completed_seconds"] = completed;
    } else if (name == "identity-pki") {
      entry = co_await copy_tree(repository, context->config.at("backup").at("protected_directory").get<std::string>(),
                                 prefix + "/identity-pki");
    } else if (name == "kafka-config") {
      const auto destination = temporary.path("topics");
      co_await export_topics(context, destination);
      entry = co_await repository->put_file(prefix + "/kafka-config", destination);
    } else if (name == "product-config") {
      const auto destination = temporary.path("config");
      const std::set<std::string> retained{
          "catalog.resources",     "catalog.sources",        "catalog.files",
          "catalog.configuration", "rules.versions",         "rules.active_sets",
          "incident.types",        "incident.type_versions", "incident.notification_versions",
          "response.plans",        "escalation.policies",    "notification.contacts",
          "ingest.control",        "ingest.revoked"};
      std::vector<std::string> arguments{"/usr/lib/postgresql/18/bin/pg_dump",
                                         "--format=custom",
                                         "--no-acl",
                                         "--schema=catalog",
                                         "--schema=rules",
                                         "--schema=incident",
                                         "--schema=response",
                                         "--schema=escalation",
                                         "--schema=notification",
                                         "--schema=ingest"};
      auto connection = take(co_await p::pg::Connection::connect(postgres(context->config.at("postgres"))));
      const auto tables = take(co_await connection->query(
          {.sql = "SELECT schemaname,tablename FROM pg_tables WHERE schemaname IN "
                  "('catalog','rules','incident','response','escalation','notification','ingest')"}));
      for (std::size_t i = 0; i < tables.size(); ++i) {
        const std::string schema(tables.value(i, 0).value_or(""));
        const std::string table(tables.value(i, 1).value_or(""));
        const auto identifier = [](std::string_view text) {
          return !text.empty() && std::ranges::all_of(text, [](char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
          });
        };
        require(identifier(schema) && identifier(table), "unsupported configuration table identifier");
        auto qualified = schema;
        qualified += ".";
        qualified += table;
        if (table != "psim_migrations" && !retained.contains(qualified)) {
          arguments.push_back("--exclude-table-data=" + qualified);
        }
      }
      process(arguments, pg_environment(context->config.at("postgres")), {}, destination);
      entry = co_await repository->put_file(prefix + "/product-config", destination);
    } else {
      const auto name_on_disk = id + ".zip";
      const auto destination =
          fs::path(context->config.at("clickhouse").at("backup_directory").get<std::string>()) / name_on_disk;
      p::clickhouse::Pool client(&context->blocking, clickhouse(context->config.at("clickhouse")));
      take(co_await client.execute_ddl("BACKUP DATABASE default TO Disk('backups','" + name_on_disk + "')"));
      entry = co_await repository->put_file(prefix + "/clickhouse", destination);
      fs::remove(destination);
    }
    manifest["components"][name] = std::move(entry);
  }
  if (manifest["components"].contains("postgres")) {
    auto connection = take(co_await p::pg::Connection::connect(postgres(context->config.at("postgres"))));
    const auto target = "psim-" + id;
    auto point = take(
        co_await connection->query({.sql = "SELECT pg_create_restore_point($1)", .parameters = {Param::text(target)}}));
    const std::string lsn(point.value(0, 0).value_or(""));
    auto wal = take(
        co_await connection->query({.sql = "SELECT pg_walfile_name($1::pg_lsn)", .parameters = {Param::text(lsn)}}));
    const std::string filename(wal.value(0, 0).value_or(""));
    manifest["checkpoint"] = {{"name", target}, {"lsn", lsn}, {"wal_file", filename}, {"at_seconds", seconds()}};
    take(co_await connection->query({.sql = "SELECT pg_switch_wal()"}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
    asio::steady_timer timer(co_await asio::this_coro::executor);
    for (;;) {
      auto state = take(co_await connection->query({.sql = "SELECT last_archived_wal FROM pg_stat_archiver"}));
      const auto archived = state.value(0, 0);
      if (archived && *archived >= filename) {
        break;
      }
      require(std::chrono::steady_clock::now() < deadline, "checkpoint WAL was not archived");
      timer.expires_after(std::chrono::seconds(1));
      co_await timer.async_wait(asio::use_awaitable);
    }
    // Scan after the checkpoint, including every object referenced at that recovery target.
    manifest["components"]["plans"] = co_await copy_plans(context, repository, prefix + "/checkpoint");
  }
  manifest["completed_at_seconds"] = seconds();
  co_await repository->commit_manifest(manifest);
  Json updates = Json::object();
  for (const auto& [name, entry] : manifest["components"].items()) {
    std::uint64_t size = 0;
    const auto add = [&size](const Json& file) {
      for (const auto& chunk : file.at("chunks")) {
        size += chunk.at("size").get<std::uint64_t>();
      }
    };
    if (entry.contains("objects")) {
      for (const auto& item : entry.at("objects")) {
        add(item);
      }
    } else if (entry.contains("files")) {
      for (const auto& item : entry.at("files")) {
        if (!item.value("directory", false)) {
          add(item);
        }
      }
    } else {
      add(entry);
    }
    updates[name] = {{"last_success_seconds", seconds()},
                     {"size_bytes", size},
                     {"last_attempt_success", 1},
                     {"duration_seconds", seconds() - manifest.at("started_at_seconds").get<double>()}};
  }
  backup_status(*context, updates);
  co_return manifest;
}
}  // namespace

void backup_status(const Context& context, const Json& updates) {
  const fs::path path(context.config.at("backup").at("status_file").get<std::string>());
  fs::create_directories(path.parent_path());
  const FileLock lock(path.string() + ".lock", true);
  auto status = fs::exists(path) ? read_json(path) : Json{{"backups", Json::object()}};
  for (const auto name : kComponents) {
    if (!status["backups"].contains(name)) {
      status["backups"][name] = {{"last_success_seconds", 0}, {"size_bytes", 0}};
    }
  }
  for (const auto& [name, value] : updates.items()) {
    status["backups"][name].update(value);
  }
  status["updated_at_seconds"] = seconds();
  write_json(path, status);
  fs::permissions(path,
                  fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read);
}

Await verify_plans(Context* context) {
  auto client = take(co_await p::s3::Client::create(&context->blocking, storage(context->config.at("plans"))));
  auto connection = take(co_await p::pg::Connection::connect(postgres(context->config.at("postgres"))));
  take(co_await connection->query({.sql = "BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY"}));
  take(co_await connection->query(
      {.sql = "DECLARE plans_cursor CURSOR FOR SELECT key,body::text FROM catalog.files WHERE linked ORDER BY key"}));
  std::set<std::string> referenced;
  Json missing = Json::array();
  Json corrupt = Json::array();
  for (;;) {
    auto rows = take(co_await connection->query({.sql = "FETCH 200 FROM plans_cursor"}));
    if (rows.size() == 0) {
      break;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const std::string key(rows.value(i, 0).value_or(""));
      referenced.insert(key);
      const auto expected = Json::parse(rows.value(i, 1).value_or("{}"));
      auto metadata = co_await client->head(key);
      if (!metadata) {
        require(metadata.error().code() == p::ErrorCode::kCommonNotFound, "plan storage unavailable");
        missing.push_back(key);
        continue;
      }
      if (metadata->size != std::stoull(expected.at("size").get<std::string>())
          || metadata->sha256 != expected.at("sha256").get<std::string>()) {
        corrupt.push_back(key);
        continue;
      }
      Temporary const temporary;
      auto verified = co_await client->get(*metadata, temporary.path("plan").string());
      if (!verified) {
        corrupt.push_back(key);
      }
    }
  }
  take(co_await connection->query({.sql = "COMMIT"}));
  Json unlinked = Json::array();
  std::string token;
  while (true) {
    auto page = take(co_await client->list(token));
    token = std::move(page.continuation);
    for (const auto& key : page.keys) {
      if (!referenced.contains(key)) {
        unlinked.push_back(key);
      }
    }
    if (token.empty()) {
      break;
    }
  }
  co_return Json{{"intact", missing.empty() && corrupt.empty()},
                 {"references", referenced.size()},
                 {"missing", missing},
                 {"corrupt", corrupt},
                 {"unlinked", unlinked}};
}

Await backup(Context* context, Arguments args) {
  require(args.words.size() >= 2, "backup action required");
  const auto& action = args.words[1];
  if (action == "status") {
    backup_status(*context);
    co_return read_json(context->config.at("backup").at("status_file").get<std::string>());
  }
  const bool writer = action == "create" || action == "wal-push";
  Repository repository(context, !writer);
  co_await repository.open();
  if (action == "create") {
    co_return co_await snapshot(context, &repository, args);
  }
  if (action == "wal-push" || action == "wal-fetch") {
    require(args.words.size() == 4 && safe_name(args.words[2]), "WAL filename and path required");
    const auto& filename = args.words[2];
    const auto valid_hex = [](std::string_view value) {
      return std::ranges::all_of(value, [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'); });
    };
    require((filename.size() == 24 && valid_hex(filename))
                || (filename.size() == 16 && filename.ends_with(".history")
                    && valid_hex(std::string_view(filename).substr(0, 8)))
                || (filename.size() == 40 && filename.substr(24, 1) == "." && filename.ends_with(".backup")
                    && valid_hex(std::string_view(filename).substr(0, 24))
                    && valid_hex(std::string_view(filename).substr(25, 8))),
            "invalid WAL filename");
    const auto key = "wal/" + context->config.at("backup").at("wal_namespace").get<std::string>() + "/" + filename;
    Temporary const temporary;
    if (writer) {
      const fs::path directory(context->config.at("state_directory").get<std::string>());
      fs::create_directories(directory);
      const auto receipt = directory / (filename + ".json");
      const auto hash = file_hash(args.words[3]);
      if (fs::exists(receipt)) {
        require(read_json(receipt).at("source_sha256") == hash, "archived WAL filename was reused");
      } else {
        auto stored = co_await repository.put_one(key, args.words[3], temporary.path("encrypted"));
        stored["source_sha256"] = hash;
        write_json(receipt, stored);
      }
    } else {
      const auto metadata = take(co_await repository.client()->head(key));
      const auto clear = temporary.path("clear");
      co_await repository.get_one(object(metadata), clear, temporary.path("encrypted"));
      fs::copy_file(clear, args.words[3], fs::copy_options::overwrite_existing);
    }
    co_return Json{{"wal", filename}};
  }
  require(args.words.size() == 3, "snapshot identifier required");
  auto manifest = co_await repository.manifest(args.words[2]);
  if (action == "inspect") {
    co_return manifest;
  }
  if (action == "recovery-secure") {
    require(args.has("--offline"), "recovery security reset requires --offline while ingress is closed");
    auto connection = take(co_await p::pg::Connection::connect(postgres(context->config.at("postgres"))));
    auto state = take(co_await connection->query({.sql = "SELECT pg_is_in_recovery()"}));
    require(state.value(0, 0) == "f", "promote the validated recovery target before resetting access");
    take(co_await connection->query({.sql = "BEGIN"}));
    auto changed = take(
        co_await connection->query({.sql = "UPDATE ingest.registrations SET "
                                           "body=jsonb_set(jsonb_set(body,'{state}','\"RESOURCE_STATE_DISABLED\"'::"
                                           "jsonb),'{version}',to_jsonb(((body->>'version')::bigint+1)::text))"}));
    const auto disabled = changed.affected();
    take(co_await connection->query({.sql = "DELETE FROM ingest.sessions"}));
    take(co_await connection->query({.sql = "DELETE FROM ingest.gateways"}));
    take(co_await connection->query({.sql = "UPDATE ingest.control SET revision=revision+1 WHERE id=1"}));
    take(co_await connection->query({.sql = "COMMIT"}));
    // Restored identity sessions must be invalidated before public ingress is reopened.
    const auto& identity = context->config.at("identity");
    p::security::HttpOptions const options{.tls = tls(identity),
                                           .timeout = std::chrono::seconds(30),
                                           .allow_plaintext = identity.value("allow_plaintext", false),
                                           .bearer_token = secret(identity, "token_file")};
    const auto response = take(
        co_await p::security::http_request(identity.at("endpoint").get<std::string>() + "/admin/realms/psim/logout-all",
                                           options, "{}", "application/json"));
    require(response.status >= 200 && response.status < 300,
            "identity session invalidation failed; keep ingress closed");
    co_return Json{{"disabled_connectors", disabled},
                   {"identity_sessions_invalidated", true},
                   {"requires_fresh_connector_certificates", true}};
  }
  if (action == "restore-plans") {
    auto client = take(co_await p::s3::Client::create(&context->blocking, storage(context->config.at("plans"))));
    std::size_t count = 0;
    for (const auto& entry : manifest.at("components").at("plans").at("objects")) {
      Temporary const temporary;
      const auto clear = temporary.path("plan");
      co_await repository.get_file(entry, clear);
      const auto stored = take(co_await client->put(entry.at("object_key"), clear.string()));
      take(co_await client->get(stored, temporary.path("verified").string()));
      ++count;
    }
    co_return Json{{"restored_objects", count}};
  }
  if (action == "restore-postgres" || action == "restore-protected") {
    const auto* const name = action == "restore-postgres" ? "postgres" : "identity-pki";
    const fs::path directory(args.one("--directory"));
    require(!directory.empty(), "restore directory required");
    const auto target_time = args.one("--target-time");
    if (action == "restore-postgres") {
      const auto uid = context->config.at("postgres").at("data_uid").get<uid_t>();
      require(uid > 0 && (::getuid() == 0 || ::getuid() == uid),
              "restore must run as root or the PostgreSQL data owner");
      const auto target_seconds =
          target_time.empty() ? manifest.at("checkpoint").at("at_seconds").get<double>() : recovery_time(target_time);
      require(target_time.empty()
                  || target_seconds
                         >= manifest.at("components").at("postgres").at("base_backup_completed_seconds").get<double>(),
              "recovery time precedes the completed base backup");
      const auto plans_id = args.one("--plans-snapshot");
      const auto coverage = plans_id.empty() ? manifest : co_await repository.manifest(plans_id);
      require(coverage.at("components").at("plans").at("covered_until_seconds").get<double>() >= target_seconds,
              "plans do not cover the recovery target");
    }
    co_await restore_tree(&repository, manifest.at("components").at(name), directory);
    if (action == "restore-postgres") {
      const auto target = manifest.at("checkpoint").at("name").get<std::string>();
      require(safe_name(target), "invalid checkpoint");
      auto timestamp = target_time;
      if (!timestamp.empty()) {
        timestamp[10] = ' ';
        timestamp.pop_back();
        timestamp += "+00:00";
      }
      const auto setting =
          target_time.empty() ? "recovery_target_name='" + target : "recovery_target_time='" + timestamp;
      const auto config_path = context->config_path.string();
      require(config_path.find_first_of("'\"\\ \t\n\r%;$`&|()<>") == std::string::npos,
              "unsafe recovery configuration path");
      const auto restore = "/usr/bin/psimctl --config " + config_path + " backup wal-fetch %f %p";
      const auto previous =
          fs::exists(directory / "postgresql.auto.conf") ? read_file(directory / "postgresql.auto.conf") : "";
      write_file(directory / "postgresql.auto.conf",
                 previous + "\nrecovery_target=''\nrecovery_target_name=''\nrecovery_target_time=''\nrecovery_target_xid=''\nrecovery_target_lsn=''\nrestore_command='" + restore + "'\n" + setting
                     + "'\nrecovery_target_action='pause'\nrecovery_target_timeline='latest'\n",
                 true);
      write_file(directory / "recovery.signal", "");
      fs::remove(directory / "standby.signal");
      if (::getuid() == 0) {
        const auto uid = context->config.at("postgres").at("data_uid").get<uid_t>();
        require(uid > 0, "non-root PostgreSQL data owner required");
        for (const auto& item : fs::recursive_directory_iterator(directory)) {
          require(::chown(item.path().c_str(), uid, uid) == 0, "PostgreSQL data owner assignment failed");
        }
        require(::chown(directory.c_str(), uid, uid) == 0, "PostgreSQL data owner assignment failed");
      }
    }
    co_return Json{{"restored", name}, {"directory", directory.string()}};
  }
  if (action == "recovery-check") {
    auto connection = take(co_await p::pg::Connection::connect(postgres(context->config.at("postgres"))));
    const auto target_time = args.one("--target-time");
    if (!target_time.empty()) {
      require(recovery_time(target_time)
                  >= manifest.at("components").at("postgres").at("base_backup_completed_seconds").get<double>(),
              "recovery time precedes the completed base backup");
      auto state = take(
          co_await connection->query({.sql = "SELECT pg_is_in_recovery() AND pg_get_wal_replay_pause_state()='paused' "
                                             "AND current_setting('recovery_target_time')::timestamptz=$1::timestamptz",
                                      .parameters = {Param::text(target_time)}}));
      require(state.value(0, 0) == "t", "recovery time has not been reached and paused");
      auto plans = co_await verify_plans(context);
      require(plans.at("intact"), "restored plan references differ");
      co_return Json{{"ready_for_promotion", true}, {"plans", plans}};
    }
    auto state =
        take(co_await connection->query({.sql = "SELECT "
                                                "pg_is_in_recovery(),current_setting('recovery_target_name'),pg_get_"
                                                "wal_replay_pause_state(),pg_last_wal_replay_lsn()>=$1::pg_lsn",
                                         .parameters = {Param::text(manifest.at("checkpoint").at("lsn"))}}));
    require(state.value(0, 0) == "t" && state.value(0, 1) == manifest.at("checkpoint").at("name").get<std::string>()
                && state.value(0, 2) == "paused" && state.value(0, 3) == "t",
            "recovery target has not been reached and paused");
    auto plans = co_await verify_plans(context);
    require(plans.at("intact"), "restored plan references differ");
    co_return Json{{"ready_for_promotion", true}, {"plans", plans}};
  }
  Temporary const temporary;
  const auto clear = temporary.path("artifact");
  const std::map<std::string, std::string> names{
      {"restore-clickhouse", "clickhouse"}, {"restore-config", "product-config"}, {"restore-kafka", "kafka-config"}};
  require(names.contains(action), "unknown backup operation");
  co_await repository.get_file(manifest.at("components").at(names.at(action)), clear);
  if (action == "restore-kafka") {
    require(args.has("--offline"), "Kafka restore requires --offline");
    co_return co_await import_topics(context, clear);
  }
  if (action == "restore-config") {
    require(args.has("--empty-installation"), "configuration restore requires --empty-installation");
    auto connection = take(co_await p::pg::Connection::connect(postgres(context->config.at("postgres"))));
    auto rows = take(co_await connection->query(
        {.sql = "SELECT count(*) FROM pg_namespace WHERE nspname IN "
                "('catalog','rules','incident','response','escalation','notification','ingest')"}));
    require(rows.value(0, 0) == "0", "configuration import requires absent product schemas");
    process({"/usr/lib/postgresql/18/bin/pg_restore", "--exit-on-error", "--single-transaction", "--no-acl",
             "--dbname=" + context->config.at("postgres").value("database", "psim"), clear.string()},
            pg_environment(context->config.at("postgres")));
  } else {
    const auto name = p::stream::uuid7() + ".zip";
    const auto path = fs::path(context->config.at("clickhouse").at("backup_directory").get<std::string>()) / name;
    fs::copy_file(clear, path);
    if (::getuid() == 0) {
      const auto uid = context->config.at("clickhouse").at("data_uid").get<uid_t>();
      require(uid > 0 && ::chown(path.c_str(), uid, uid) == 0, "ClickHouse backup owner assignment failed");
    }
    p::clickhouse::Pool client(&context->blocking, clickhouse(context->config.at("clickhouse")));
    take(co_await client.execute_ddl("RESTORE DATABASE default FROM Disk('backups','" + name + "')"));
    fs::remove(path);
  }
  co_return Json{{"restored", names.at(action)}};
}
}  // namespace psim::ctl
