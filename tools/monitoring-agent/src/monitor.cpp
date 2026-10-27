#include "monitor.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <sys/statvfs.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/redis/connection.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/secret.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::monitoring {
namespace beast = boost::beast;
namespace http = beast::http;
using Tcp = asio::ip::tcp;
using Socket = beast::ssl_stream<beast::tcp_stream>;

std::string sample(std::string_view name, double value, const Labels& labels) {
  require(std::isfinite(value), "non-finite metric");
  std::string output(name);
  if (!labels.empty()) {
    output += '{';
    for (const auto& [key, text] : labels) {
      if (output.back() != '{') {
        output += ',';
      }
      output += key + "=" + Json(text).dump();
    }
    output += '}';
  }
  std::ostringstream number;
  number << std::setprecision(17) << value;
  auto formatted = number.str();
  if (formatted.find_first_of(".eE") == std::string::npos) {
    formatted += ".0";
  }
  return output + " " + formatted + "\n";
}

std::string backup_metrics(const fs::path& path, const Json& expected) {
  Json entries = Json::object();
  double updated = 0;
  bool valid = false;
  try {
    const auto document = read_json(path);
    updated = document.at("updated_at_seconds");
    require(std::isfinite(updated) && updated > 0 && updated <= seconds() + 60, "invalid backup status timestamp");
    entries = document.at("backups");
    for (const auto& name : expected) {
      const auto& entry = entries.at(name.get<std::string>());
      const auto timestamp = entry.at("last_success_seconds").get<double>();
      const auto size = entry.at("size_bytes").get<double>();
      require(std::isfinite(timestamp) && std::isfinite(size) && timestamp >= 0 && timestamp <= updated && size >= 0,
              "invalid backup record");
    }
    valid = true;
  } catch (const std::exception&) {
    entries = Json::object();
    updated = 0;
  }
  auto output =
      sample("psim_backup_status_valid", valid ? 1 : 0) + sample("psim_backup_status_updated_seconds", updated);
  for (const auto& name : expected) {
    const auto key = name.get<std::string>();
    const auto entry = entries.value(key, Json::object());
    const Labels labels{{"backup", key}};
    output += sample("psim_backup_last_success_seconds", entry.value("last_success_seconds", 0.0), labels);
    output += sample("psim_backup_size_bytes", entry.value("size_bytes", 0.0), labels);
    if (entry.contains("last_attempt_success")) {
      output += sample("psim_backup_last_attempt_success", entry.at("last_attempt_success"), labels);
    }
    if (entry.contains("duration_seconds")) {
      output += sample("psim_backup_duration_seconds", entry.at("duration_seconds"), labels);
    }
  }
  return output;
}

namespace {
p::security::TlsOptions agent_tls(const Json& config, const std::string& name = "tls") {
  const auto& entry = config.at(name);
  return {.ca_file = entry.at("ca"), .certificate_file = entry.at("cert"), .private_key_file = entry.at("key")};
}

std::string agent_secret(const Json& config, const std::string& name) {
  return std::string(
      take(p::security::read_secret(config.at("secrets").get<std::string>() + "/" + name + ".secret")).view());
}

asio::awaitable<std::string> request(std::string url, p::security::HttpOptions options, std::string body = {}) {
  options.timeout = std::chrono::seconds(3);
  options.max_body_bytes = static_cast<std::size_t>(4 * 1024 * 1024);
  const auto response =
      take(co_await p::security::http_request(url, std::move(options), std::move(body), "text/plain"));
  require(response.status >= 200 && response.status < 300, "probe rejected");
  co_return response.body;
}

asio::awaitable<std::string> raw_probe(Json target, const p::security::TlsOptions* tls_options, std::string command) {
  const auto executor = co_await asio::this_coro::executor;
  Tcp::resolver resolver(executor);
  const auto host = target.at("host").get<std::string>();
  const auto endpoints =
      co_await resolver.async_resolve(host, std::to_string(target.at("port").get<int>()), asio::use_awaitable);
  if (tls_options == nullptr) {
    beast::tcp_stream stream(executor);
    stream.expires_after(std::chrono::seconds(3));
    co_await stream.async_connect(endpoints, asio::use_awaitable);
    co_return std::string{};
  }
  auto context = take(p::security::tls_context(*tls_options));
  Socket stream(executor, *context);
  beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(3));
  take(p::security::configure_hostname(stream.native_handle(), host));
  co_await beast::get_lowest_layer(stream).async_connect(endpoints, asio::use_awaitable);
  co_await stream.async_handshake(asio::ssl::stream_base::client, asio::use_awaitable);
  if (command.empty()) {
    co_return std::string{};
  }
  co_await asio::async_write(stream, asio::buffer(command), asio::use_awaitable);
  std::string output;
  std::array<char, 8192> buffer{};
  for (;;) {
    auto [error, count] = co_await stream.async_read_some(asio::buffer(buffer), asio::as_tuple(asio::use_awaitable));
    output.append(buffer.data(), count);
    require(output.size() <= 65536, "raw probe response exceeds limit");
    if (error) {
      require(error == asio::error::eof || error == asio::ssl::error::stream_truncated, "raw probe read failed");
      break;
    }
  }
  co_return output;
}

Labels with(Labels labels, const std::string& name, const std::string& value) {
  labels[name] = value;
  return labels;
}

std::string metric_proxy(const std::string& body, const Json& mapping, const Labels& labels) {
  const std::regex metric(R"(^([a-zA-Z_:][a-zA-Z0-9_:]*)(\{.*\})?\s+([^\s]+))");
  const std::regex label(R"label(([a-zA-Z_][a-zA-Z0-9_]*)=("(?:[^"\\]|\\.)*"))label");
  const std::set<std::string> allowed{"proxy", "server", "area", "id", "outcome", "status", "method", "uri"};
  std::istringstream input(body);
  std::string line;
  std::string output;
  while (std::getline(input, line)) {
    std::smatch match;
    if (!std::regex_search(line, match, metric) || !mapping.contains(match[1].str())) {
      continue;
    }
    Labels dimensions = labels;
    const auto fields = match[2].str();
    for (std::sregex_iterator item(fields.begin(), fields.end(), label), end; item != end; ++item) {
      const auto key = (*item)[1].str();
      if (allowed.contains(key)) {
        dimensions[key] = Json::parse((*item)[2].str()).get<std::string>();
      }
    }
    output += sample(mapping.at(match[1].str()).get<std::string>(), std::stod(match[3].str()), dimensions);
  }
  require(!output.empty(), "expected exporter metrics absent");
  return output;
}

asio::awaitable<std::string> probe(Json target, Json config) {
  const auto started = std::chrono::steady_clock::now();
  const auto component = target.at("component").get<std::string>();
  const auto host = target.at("host").get<std::string>();
  const auto port = target.at("port").get<int>();
  const auto kind = target.at("kind").get<std::string>();
  const Labels labels{{"component", component}, {"target", host + ":" + std::to_string(port)}};
  std::string output;
  bool success = false;
  try {
    const auto purpose = target.contains("tls") && target.at("tls").is_null() ? "" : target.value("tls", "tls");
    const auto options = purpose.empty() ? p::security::TlsOptions{} : agent_tls(config, purpose);
    if (kind == "http" || kind == "prometheus") {
      const auto body = co_await request((purpose.empty() ? "http://" : "https://") + host + ":" + std::to_string(port)
                                             + target.value("path", kind == "prometheus" ? "/metrics" : "/"),
                                         {.tls = options, .allow_plaintext = purpose.empty()});
      if (target.contains("json_key")) {
        const auto value = Json::parse(body).at(target.at("json_key").get<std::string>());
        require(value == true || value == "true", "unhealthy probe result");
      }
      if (kind == "prometheus") {
        output += metric_proxy(body, target.at("metrics"), labels);
      }
    } else if (kind == "tcp" || kind == "keeper") {
      const auto body =
          co_await raw_probe(target, purpose.empty() ? nullptr : &options, kind == "keeper" ? "mntr" : "");
      if (kind == "keeper") {
        std::istringstream lines(body);
        std::string line;
        bool found = false;
        while (std::getline(lines, line)) {
          if (line.starts_with("zk_outstanding_requests\t")) {
            output += sample("psim_keeper_outstanding_requests", std::stod(line.substr(24)), labels);
            found = true;
          }
        }
        require(found, "keeper metric absent");
      }
    } else if (kind == "redis") {
      p::redis::ConnectionOptions const redis{.endpoint = {host, std::to_string(port)},
                                              .credentials = {"monitoring", agent_secret(config, component)},
                                              .tls = {.ca_file = options.ca_file,
                                                      .certificate_file = options.certificate_file,
                                                      .private_key_file = options.private_key_file},
                                              .connect_timeout = std::chrono::seconds(3),
                                              .command_timeout = std::chrono::seconds(3),
                                              .require_primary = false};
      auto connection = take(co_await p::redis::Connection::connect(redis));
      if (component == "valkey") {
        auto reply = take(co_await connection->execute({"INFO", "replication"}));
        require(reply.kind == p::redis::Reply::Kind::kString, "invalid replication reply");
        std::istringstream lines(reply.data);
        std::string line;
        while (std::getline(lines, line)) {
          if (line.starts_with("role:")) {
            output += sample("psim_valkey_primary", line.starts_with("role:master") ? 1 : 0, labels);
          }
          if (line.starts_with("connected_slaves:")) {
            output += sample("psim_valkey_connected_replicas", std::stod(line.substr(17)), labels);
          }
        }
      } else {
        auto reply = take(co_await connection->execute({"SENTINEL", "masters"}));
        require(reply.kind == p::redis::Reply::Kind::kArray, "invalid Sentinel reply");
        output += sample("psim_sentinel_masters", static_cast<double>(reply.elements.size()), labels);
      }
    } else if (kind == "postgres") {
      const auto pg_tls = agent_tls(config, "postgres_tls");
      p::pg::ConnectionOptions const pg{.host = host,
                                        .port = std::to_string(port),
                                        .database = "psim",
                                        .user = "monitoring",
                                        .password = agent_secret(config, "postgres"),
                                        .properties = {{"sslmode", "verify-full"},
                                                       {"sslrootcert", pg_tls.ca_file},
                                                       {"sslcert", pg_tls.certificate_file},
                                                       {"sslkey", pg_tls.private_key_file},
                                                       {"target_session_attrs", "any"},
                                                       {"options", "-c default_transaction_read_only=on"}},
                                        .connect_timeout = std::chrono::seconds(3),
                                        .query_timeout = std::chrono::seconds(3)};
      auto connection = take(co_await p::pg::Connection::connect(pg));
      auto rows = take(co_await connection->query(
          {.sql = "SELECT NOT pg_is_in_recovery(),(SELECT count(*) FROM pg_stat_activity),CASE WHEN "
                  "pg_is_in_recovery() AND pg_last_wal_receive_lsn() IS DISTINCT FROM pg_last_wal_replay_lsn() THEN "
                  "COALESCE(EXTRACT(epoch FROM now()-pg_last_xact_replay_timestamp()),0) ELSE 0 END"}));
      const bool primary = rows.value(0, 0) == "t";
      output += sample("psim_postgres_primary", primary ? 1 : 0, labels);
      output += sample("psim_postgres_connections", std::stod(std::string(rows.value(0, 1).value_or("0"))), labels);
      output +=
          sample("psim_postgres_replay_lag_seconds", std::stod(std::string(rows.value(0, 2).value_or("0"))), labels);
      rows = take(co_await connection->query(
          {.sql = "SELECT COALESCE(EXTRACT(epoch FROM last_archived_time),0),failed_count FROM pg_stat_archiver"}));
      output += sample("psim_postgres_wal_archive_last_success_seconds",
                       std::stod(std::string(rows.value(0, 0).value_or("0"))), labels);
      output += sample("psim_postgres_wal_archive_failures_total",
                       std::stod(std::string(rows.value(0, 1).value_or("0"))), labels);
      if (primary && target.value("business", false)) {
        rows = take(
            co_await connection->query({.sql = "SELECT metric,labels::text,value FROM monitoring.business_metrics()"}));
        for (std::size_t i = 0; i < rows.size(); ++i) {
          const auto dimensions = Json::parse(rows.value(i, 1).value_or("{}")).get<Labels>();
          output +=
              sample(rows.value(i, 0).value_or(""), std::stod(std::string(rows.value(i, 2).value_or("0"))), dimensions);
        }
        output += sample("psim_collection_success", 1, with(labels, "collector", "business"));
      }
    } else if (kind == "clickhouse") {
      const auto url = "https://" + host + ":" + std::to_string(port) + "/";
      const p::security::HttpOptions http_options{
          .tls = options,
          .headers = {{"X-ClickHouse-User", "monitoring"}, {"X-ClickHouse-Key", agent_secret(config, "clickhouse")}}};
      const auto parts = Json::parse(co_await request(url, http_options,
                                                      "SELECT table,partition,count() AS value FROM system.parts WHERE "
                                                      "active GROUP BY table,partition FORMAT JSON"));
      for (const auto& row : parts.at("data")) {
        auto dimensions = with(labels, "table", row.at("table"));
        dimensions["partition"] = row.at("partition");
        const auto& value = row.at("value");
        output += sample("psim_clickhouse_active_parts",
                         value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>(), dimensions);
      }
      const auto replicas = Json::parse(
          co_await request(url, http_options, "SELECT table,queue_size,is_readonly FROM system.replicas FORMAT JSON"));
      for (const auto& row : replicas.at("data")) {
        const auto dimensions = with(labels, "table", row.at("table"));
        output += sample("psim_clickhouse_replication_queue", row.at("queue_size"), dimensions);
        output += sample("psim_clickhouse_replica_readonly", row.at("is_readonly"), dimensions);
      }
    } else {
      require(false, "unknown probe kind");
    }
    success = true;
  } catch (const OperationError& error) {
    output = sample("psim_collection_error", 1, with(labels, "reason", error.what()));
  } catch (const std::exception&) {
    output.clear();
  }
  output += sample("psim_probe_success", success ? 1 : 0, labels);
  output += sample("psim_probe_duration_seconds",
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(), labels);
  if (kind == "postgres" || kind == "clickhouse" || kind == "redis" || kind == "keeper" || kind == "prometheus") {
    output += sample("psim_collection_success", success ? 1 : 0, with(labels, "collector", kind));
  }
  co_return output;
}

class Agent {
 public:
  Agent(asio::io_context& io, Json config)
      : io_(io),
        config_(std::move(config)),
        timer_(io),
        acceptor_(io),
        tls_(take(p::security::tls_context(agent_tls(config_), true))) {}

  void start() {
    const Tcp::endpoint endpoint(asio::ip::make_address(config_.at("listen").get<std::string>()),
                                 config_.value("port", std::uint16_t{19443}));
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(Tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(64);
    asio::co_spawn(io_, collect(), [](const std::exception_ptr&) {});
    asio::co_spawn(io_, accept(), [](const std::exception_ptr&) {});
  }

 private:
  asio::awaitable<void> collect() {
    for (;;) {
      try {
        auto lines = sample("psim_monitoring_info", 1);
        for (const auto& [name, path] : config_.value("volumes", Json::object()).items()) {
          struct statvfs value{};
          const bool valid = ::statvfs(path.get<std::string>().c_str(), &value) == 0;
          if (valid) {
            lines +=
                sample("psim_filesystem_size_bytes",
                       static_cast<double>(value.f_blocks) * static_cast<double>(value.f_frsize), {{"volume", name}});
            lines +=
                sample("psim_filesystem_available_bytes",
                       static_cast<double>(value.f_bavail) * static_cast<double>(value.f_frsize), {{"volume", name}});
            lines += sample("psim_filesystem_files", static_cast<double>(value.f_files), {{"volume", name}});
            lines += sample("psim_filesystem_files_free", static_cast<double>(value.f_ffree), {{"volume", name}});
          }
          lines += sample("psim_collection_success", valid ? 1 : 0, {{"collector", "filesystem"}, {"component", name}});
        }
        try {
          const fs::path proc(config_.value("proc", "/host/proc"));
          std::istringstream memory(read_file(proc / "meminfo"));
          std::string line;
          std::map<std::string, double> values;
          while (std::getline(memory, line)) {
            std::istringstream fields(line);
            std::string name;
            double value = 0;
            fields >> name >> value;
            values[name] = value * 1024;
          }
          lines += sample("psim_host_memory_total_bytes", values.at("MemTotal:"));
          lines += sample("psim_host_memory_available_bytes", values.at("MemAvailable:"));
          std::istringstream cpu(read_file(proc / "stat"));
          std::string name;
          cpu >> name;
          std::array<double, 8> ticks{};
          for (auto& tick : ticks) {
            cpu >> tick;
          }
          double total = 0;
          for (const auto tick : ticks) {
            total += tick;
          }
          const double idle = ticks[3] + ticks[4];
          if (total > cpu_total_ && cpu_total_ != 0) {
            lines += sample("psim_host_cpu_utilization", 1 - ((idle - cpu_idle_) / (total - cpu_total_)));
          }
          cpu_total_ = total;
          cpu_idle_ = idle;
          lines += sample("psim_collection_success", 1, {{"collector", "host"}, {"component", "host"}});
        } catch (const std::exception&) {
          lines += sample("psim_collection_success", 0, {{"collector", "host"}, {"component", "host"}});
        }
        // Probes yield during I/O; the HTTP listener continues serving the previous complete snapshot.
        for (const auto& target : config_.value("targets", Json::array())) {
          lines += co_await probe(target, config_);
        }
        if (config_.contains("kafka")) {
          bool valid = false;
          try {
            lines += co_await monitor_kafka(config_, &blocking_);
            valid = true;
          } catch (const std::exception&) {
            valid = false;
          }
          lines += sample("psim_collection_success", valid ? 1 : 0, {{"collector", "kafka"}, {"component", "kafka"}});
        }
        for (const auto& entry : config_.value("certificates", Json::array())) {
          const Labels labels{{"owner", entry.at("owner")}, {"certificate", entry.at("name")}};
          bool valid = false;
          try {
            const auto body = read_file(entry.at("path").get<std::string>(), static_cast<std::size_t>(1024 * 1024));
            const std::unique_ptr<BIO, decltype(&BIO_free)> bio(
                BIO_new_mem_buf(body.data(), static_cast<int>(body.size())), BIO_free);
            double earliest = std::numeric_limits<double>::max();
            std::size_t count = 0;
            for (;;) {
              const std::unique_ptr<X509, decltype(&X509_free)> certificate(
                  PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free);
              if (!certificate) {
                break;
              }
              int days = 0;
              int remaining = 0;
              require(ASN1_TIME_diff(&days, &remaining, nullptr, X509_get0_notAfter(certificate.get())) == 1,
                      "invalid certificate expiry");
              earliest = std::min(earliest, seconds() + (static_cast<double>(days) * 86400) + remaining);
              ++count;
            }
            require(count > 0, "empty certificate chain");
            lines += sample("psim_certificate_expiry_seconds", earliest, labels);
            valid = true;
          } catch (const std::exception&) {
            valid = false;
          }
          auto dimensions = labels;
          dimensions["component"] = entry.at("owner");
          dimensions["collector"] = "certificate";
          lines += sample("psim_collection_success", valid ? 1 : 0, dimensions);
        }
        if (config_.contains("backups")) {
          lines +=
              backup_metrics(fs::path(config_.at("state").get<std::string>()) / "backups.json", config_.at("backups"));
        }
        lines += sample("psim_collection_timestamp_seconds", seconds());
        snapshot_ = std::move(lines);
        updated_ = std::chrono::steady_clock::now();
      } catch (const std::exception&) {
        snapshot_.clear();
      }
      timer_.expires_after(std::chrono::seconds(15));
      co_await timer_.async_wait(asio::use_awaitable);
    }
  }

  static std::string peer(Socket* socket) {
    const std::unique_ptr<X509, decltype(&X509_free)> certificate(SSL_get1_peer_certificate(socket->native_handle()),
                                                                  X509_free);
    require(certificate != nullptr, "peer certificate required");
    auto* subject = X509_get_subject_name(certificate.get());
    const auto index = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);
    require(index >= 0 && X509_NAME_get_index_by_NID(subject, NID_commonName, index) < 0,
            "single peer common name required");
    const auto* text = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(subject, index));
    const auto size = ASN1_STRING_length(text);
    require(size > 0 && size < 128, "invalid peer common name");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): OpenSSL returns bounded certificate text as bytes
    return {reinterpret_cast<const char*>(ASN1_STRING_get0_data(text)),
            static_cast<std::size_t>(size)};  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  }

  asio::awaitable<void> session(std::shared_ptr<Socket> socket) {
    beast::get_lowest_layer(*socket).expires_after(std::chrono::seconds(10));
    co_await socket->async_handshake(asio::ssl::stream_base::server, asio::use_awaitable);
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(static_cast<unsigned long>(1024 * 1024));
    parser.header_limit(16384);
    co_await http::async_read(*socket, buffer, parser, asio::use_awaitable);
    const auto input = parser.release();
    const auto owner = peer(socket.get());
    const std::string path(input.target());
    http::response<http::string_body> response(http::status::ok, 11);
    response.set(http::field::content_type, "text/plain");
    const auto ready = !snapshot_.empty() && std::chrono::steady_clock::now() - updated_ <= std::chrono::seconds(90);
    if (input.method() == http::verb::post && path == "/alerts") {
      if (owner != "alertmanager") {
        response.result(http::status::forbidden);
      } else {
        const auto document = Json::parse(input.body());
        require(document.at("alerts").is_array(), "invalid alert payload");
        const fs::path target = fs::path(config_.at("state").get<std::string>()) / "alerts.jsonl";
        if (fs::exists(target) && fs::file_size(target) > static_cast<std::uintmax_t>(16 * 1024 * 1024)) {
          fs::rename(target, target.parent_path() / "alerts.previous.jsonl");
        }
        append_file(target, document.dump() + "\n");
      }
    } else if (input.method() != http::verb::get) {
      response.result(http::status::method_not_allowed);
    } else if (owner != "prometheus" && owner != "deployment-admin"
               && !(owner == "haproxy" && path.starts_with("/operations/runbooks/"))) {
      response.result(http::status::forbidden);
    } else if (path == "/metrics" || path == "/health/ready") {
      if (!ready) {
        response.result(http::status::service_unavailable);
      } else {
        response.body() = path == "/metrics" ? snapshot_ + sample("psim_host_time_seconds", seconds()) : "ready\n";
      }
    } else if (path.starts_with("/service-metrics/")) {
      const auto name = path.substr(17);
      const auto services = config_.value("service_metrics", Json::object());
      if (!services.contains(name)) {
        response.result(http::status::not_found);
      } else {
        try {
          response.body() = co_await request("http://" + config_.at("node_address").get<std::string>() + ":"
                                                 + std::to_string(services.at(name).get<int>()) + "/metrics",
                                             {.allow_plaintext = true});
        } catch (const std::exception&) {
          response.result(http::status::bad_gateway);
        }
      }
    } else if (path.starts_with("/operations/runbooks/")) {
      const auto name = path.substr(21);
      const auto safe = std::ranges::all_of(name, [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
      });
      const auto file =
          fs::path(config_.at("runbooks").get<std::string>()) / (name.empty() ? "index.html" : name + ".html");
      if (!safe || !fs::is_regular_file(file)) {
        response.result(http::status::not_found);
      } else {
        response.body() = read_file(file, static_cast<std::size_t>(1024 * 1024));
        response.set(http::field::content_type, "text/html; charset=utf-8");
      }
    } else {
      response.result(http::status::not_found);
    }
    response.set(http::field::cache_control, "no-store");
    response.set(http::field::x_content_type_options, "nosniff");
    response.prepare_payload();
    co_await http::async_write(*socket, response, asio::use_awaitable);
  }

  asio::awaitable<void> accept() {
    for (;;) {
      auto connection = co_await acceptor_.async_accept(asio::use_awaitable);
      if (sessions_ >= 32) {
        connection.close();
        continue;
      }
      ++sessions_;
      auto socket = std::make_shared<Socket>(std::move(connection), *tls_);
      asio::co_spawn(io_, session(socket), [this, socket](const std::exception_ptr&) {
        --sessions_;
        boost::system::error_code ignored;
        (void)beast::get_lowest_layer(*socket).socket().close(ignored);
      });
    }
  }

  asio::io_context& io_;
  Json config_;
  p::async::BlockingPool blocking_{4, 32};
  asio::steady_timer timer_;
  Tcp::acceptor acceptor_;
  std::shared_ptr<asio::ssl::context> tls_;
  std::string snapshot_;
  std::chrono::steady_clock::time_point updated_{};
  double cpu_total_ = 0;
  double cpu_idle_ = 0;
  std::size_t sessions_ = 0;
};
}  // namespace

int run(std::span<char*> arguments) {
  try {
    require(arguments.size() == 2 && std::string_view(arguments[0]) == "--config",
            "usage: psim-monitoring-agent --config FILE");
    const auto config = read_json(arguments[1]);
    asio::io_context io;
    Agent agent(io, config);
    agent.start();
    asio::signal_set signals(io, SIGTERM, SIGINT);
    signals.async_wait([&io](const auto&, int) { io.stop(); });
    io.run();
    return 0;
  } catch (const std::exception&) {
    (void)std::fputs("monitoring agent failed\n", stderr);
    return 1;
  }
}
}  // namespace psim::monitoring
