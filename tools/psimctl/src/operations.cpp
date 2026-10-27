#include "operations.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <print>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/s3/client.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::ctl {
Await api(Context* context, std::string path, Json body, std::string request_id) {
  const auto& config = context->config.at("api");
  p::security::HttpOptions options{.tls = tls(config),
                                   .timeout = std::chrono::seconds(60),
                                   .max_body_bytes = static_cast<std::size_t>(64 * 1024 * 1024),
                                   .allow_plaintext = config.value("allow_plaintext", false),
                                   .bearer_token = secret(config, "token_file")};
  if (!request_id.empty()) {
    require(p::stream::uuid(request_id), "invalid operation request identifier");
    options.headers["Idempotency-Key"] = request_id;
  }
  const auto response =
      take(co_await p::security::http_request(config.at("endpoint").get<std::string>() + path, std::move(options),
                                              body.is_null() ? "" : body.dump(), "application/json"));
  require(response.status >= 200 && response.status < 300, "administrative API request rejected");
  co_return Json::parse(response.body);
}

namespace {
Await diagnose(Context* context) {
  Json checks = Json::object();
  bool healthy = true;
  for (const auto& [name, config] : context->config.value("health", Json::object()).items()) {
    p::security::HttpOptions options{.tls = tls(config),
                                     .timeout = std::chrono::seconds(5),
                                     .max_body_bytes = 65536,
                                     .allow_plaintext = config.value("allow_plaintext", false)};
    const auto response = co_await p::security::http_request(config.at("endpoint"), std::move(options));
    const bool success = response && response->status == 200;
    checks[name] = {{"healthy", success}};
    healthy = healthy && success;
  }
  auto database = co_await p::pg::Connection::connect(postgres(context->config.at("postgres")));
  checks["postgres"] = {{"healthy", database.has_value()}};
  healthy = healthy && database.has_value();
  const fs::path status(context->config.at("backup").at("status_file").get<std::string>());
  Json backups = nullptr;
  if (fs::exists(status)) {
    const auto source = read_json(status);
    backups = Json::object();
    for (const auto& [name, entry] : source.value("backups", Json::object()).items()) {
      for (const auto* field :
           {"last_success_seconds", "last_attempt_seconds", "last_attempt_success", "duration_seconds"}) {
        if (entry.contains(field) && entry.at(field).is_number()) {
          backups[name][field] = entry.at(field);
        }
      }
    }
  }
  co_return Json{{"deployment", context->config.at("deployment")},
                 {"healthy", healthy},
                 {"checks", checks},
                 {"backups", std::move(backups)}};
}

Await dispatch(Context* context, Arguments args) {
  require(!args.words.empty(), "operation required");
  const auto& command = args.words[0];
  if (command == "backup") {
    co_return co_await backup(context, args);
  }
  if (command == "plans") {
    require(args.words.size() == 2 && args.words[1] == "verify", "unknown plans operation");
    co_return co_await verify_plans(context);
  }
  if (command == "dlq" || command == "normalizer" || command == "history") {
    co_return co_await kafka_operation(context, args);
  }
  if (command == "diagnose") {
    co_return co_await diagnose(context);
  }
  if (command == "support") {
    const auto output = args.one("--output");
    require(!output.empty() && !fs::exists(output), "support destination must be new");
    const auto result = co_await diagnose(context);
    Temporary const temporary;
    write_json(temporary.path("diagnostics.json"), result);
    // Only constructed aggregates enter the archive, never configuration, tokens or raw logs.
    process({"/usr/bin/tar", "-czf", output, "-C", temporary.path("").string(), "diagnostics.json"});
    co_return Json{{"path", output}, {"healthy", result.at("healthy")}};
  }
  if (command == "certificates") {
    require(args.words.size() == 2, "certificate action required");
    const std::map<std::string, std::string> actions{
        {"inspect", "INSPECT"}, {"renew", "ROTATE"}, {"revoke", "REVOKE"}, {"recover", "RECOVER"}};
    require(actions.contains(args.words[1]), "unknown certificate action");
    const auto request = args.one("--request");
    const auto document = read_json(request);
    require(document.at("action") == "REGISTRATION_ACTION_" + actions.at(args.words[1]),
            "certificate request action differs");
    Temporary const temporary;
    process({"/usr/bin/psim-gateway-admin", context->config.at("gateway").at("endpoint"),
             context->config.at("gateway").at("tls_directory"), request, context->config.at("api").at("token_file")},
            {}, {}, temporary.path("result"));
    co_return read_json(temporary.path("result"));
  }
  if (command == "replay") {
    require(args.words.size() == 2, "replay kind required");
    const auto& kind = args.words[1];
    if (kind == "result") {
      const auto id = args.one("--id");
      require(p::stream::uuid(id), "invalid replay identifier");
      co_return co_await api(context, "/v1/dry-runs/" + id);
    }
    const auto replay_id = args.one("--replay-id");
    require(p::stream::uuid(replay_id), "invalid replay identifier");
    if (kind == "rules") {
      const auto rule = args.one("--rule");
      require(p::stream::uuid(rule), "invalid replay rule");
      const auto request = read_json(args.one("--request"));
      auto result = co_await api(context, "/v1/rules/" + rule + ":dry-run", request, replay_id);
      co_return Json{{"replay_id", replay_id}, {"job", result}};
    }
    require(kind == "response", "unknown replay kind");
    const auto input = args.one("--input");
    const auto scope = args.one("--scope");
    const auto document = Json::parse(read_file(input, static_cast<std::size_t>(1024 * 1024)));
    require(!scope.empty() && document.at("plan").contains("version"), "replay scope and pinned plan version required");
    const auto output = args.one("--output");
    require(!output.empty() && !fs::exists(output), "replay destination exists");
    Temporary const temporary;
    // The pure replay executable receives only bounded stdin and a clean environment.
    process({"/usr/bin/psim-response-replay"}, {}, input, temporary.path("result"));
    const auto result = read_json(temporary.path("result"));
    require(result.at("externalCalls") == 0, "diagnostic replay attempted an external effect");
    Json report{{"replay_id", replay_id},
                {"scope", scope},
                {"actor", context->config.at("actor")},
                {"plan_version", document.at("plan").at("version")},
                {"result", result}};
    write_file(output, report.dump() + "\n");
    co_return report;
  }
  throw std::runtime_error("unknown operation");
}
}  // namespace

int operations(std::span<char*> arguments) {
  try {
    if (arguments.size() == 1 && std::string_view(arguments[0]) == "--help") {
      std::println(
          "psimctl [--config FILE] diagnose | support --output FILE | plans verify | backup "
          "create|status|inspect|restore-plans|restore-postgres|restore-clickhouse|restore-config|restore-protected|"
          "restore-kafka|recovery-check|recovery-secure|wal-push|wal-fetch | dlq inspect|retry | normalizer cleanup | "
          "history reload | "
          "certificates inspect|renew|revoke|recover | replay rules|response|result");
      return 0;
    }
    (void)::umask(0077);
    const Arguments args(arguments);
    const std::set<std::string> allowed{
        "--config",      "--component",     "--directory",   "--offline",   "--empty-installation",
        "--topic",       "--partition",     "--from-offset", "--to-offset", "--apply",
        "--tenant",      "--source",        "--group",       "--request",   "--rule",
        "--replay-id",   "--input",         "--scope",       "--output",    "--id",
        "--target-time", "--plans-snapshot"};
    for (const auto& [name, values] : args.flags) {
      require(allowed.contains(name), "unknown operation option");
    }
    Context context;
    context.config_path = fs::absolute(args.one("--config", "/etc/psim-platform/psimctl.json"));
    context.config = read_json(context.config_path);
    require(context.config.at("format") == 1 && !context.config.at("actor").get<std::string>().empty()
                && !context.config.at("deployment").get<std::string>().empty(),
            "invalid operation configuration");
    const bool wal = args.words.size() > 1 && args.words[0] == "backup"
                     && (args.words[1] == "wal-push" || args.words[1] == "wal-fetch");
    const bool heartbeat = args.words.size() == 2 && args.words[0] == "backup" && args.words[1] == "status";
    const auto id = p::stream::uuid7();
    const fs::path state(context.config.at("state_directory").get<std::string>());
    fs::create_directories(state);
    std::unique_ptr<FileLock> lock;
    if (!wal && !heartbeat) {
      lock = std::make_unique<FileLock>(state / "operation.lock");
    }
    if (!wal) {
      audit(context, args, "started", id);
    }
    const p::s3::Sdk sdk;
    asio::io_context io;
    auto future = asio::co_spawn(io, dispatch(&context, args), asio::use_future);
    io.run();
    Json result;
    try {
      result = future.get();
    } catch (...) {
      if (!wal) {
        audit(context, args, "failed", id);
      }
      if (args.words.size() > 1 && args.words[0] == "backup" && args.words[1] == "create") {
        Json updates = Json::object();
        const auto component = args.one("--component");
        for (const auto* name : {"plans", "postgres", "clickhouse", "kafka-config", "product-config", "identity-pki"}) {
          if (component.empty() || component == name) {
            updates[name] = {{"last_attempt_success", 0}};
          }
        }
        backup_status(context, updates);
      }
      throw;
    }
    if (!wal) {
      audit(context, args, "success", id);
    }
    std::println("{}", result.dump());
    return (result.contains("intact") && !result.at("intact").get<bool>())
                   || (args.words[0] == "diagnose" && !result.at("healthy").get<bool>())
               ? 1
               : 0;
  } catch (const OperationError& error) {
    (void)std::fputs("psimctl: ", stderr);
    (void)std::fputs(error.what(), stderr);
    (void)std::fputs("\n", stderr);
    return 1;
  } catch (const std::exception&) {
    // Only deliberately constructed local errors reach this boundary; remote response bodies are excluded.
    (void)std::fputs("psimctl: operation failed\n", stderr);
    return 1;
  }
}
}  // namespace psim::ctl
