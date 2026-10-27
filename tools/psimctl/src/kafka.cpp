#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <librdkafka/rdkafka.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::ctl {
namespace {
using Consumer = p::kafka::Consumer;
using Producer = p::kafka::Producer;
using TopicPartition = p::kafka::TopicPartition;
using Queue = std::unique_ptr<rd_kafka_queue_t, decltype(&rd_kafka_queue_destroy)>;
using Event = std::unique_ptr<rd_kafka_event_t, decltype(&rd_kafka_event_destroy)>;
using Metadata = std::unique_ptr<const rd_kafka_metadata_t, decltype(&rd_kafka_metadata_destroy)>;

std::string hex(std::string_view input) {
  constexpr std::string_view kDigits = "0123456789abcdef";
  std::string output;
  output.reserve(input.size() * 2);
  for (const char character : input) {
    const auto byte = static_cast<unsigned char>(character);
    output += kDigits[byte >> 4];
    output += kDigits[byte & 15U];
  }
  return output;
}

std::string unhex(std::string_view input) {
  require(input.size() % 2 == 0 && input.size() <= static_cast<std::size_t>(32 * 1024 * 1024),
          "invalid snapshot bytes");
  const auto digit = [](char ch) -> unsigned char {
    require((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'), "invalid snapshot hex");
    return static_cast<unsigned char>(ch <= '9' ? ch - '0' : ch - 'a' + 10);
  };
  std::string output;
  output.reserve(input.size() / 2);
  for (std::size_t i = 0; i < input.size(); i += 2) {
    output += static_cast<char>((digit(input[i]) << 4) | digit(input[i + 1]));
  }
  return output;
}

Metadata metadata(rd_kafka_t* handle) {
  const rd_kafka_metadata_t* value = nullptr;
  require(rd_kafka_metadata(handle, 1, nullptr, &value, 10000) == RD_KAFKA_RESP_ERR_NO_ERROR,
          "Kafka metadata unavailable");
  return {value, rd_kafka_metadata_destroy};
}

Event event(const Queue& queue) {
  Event value(rd_kafka_queue_poll(queue.get(), 15000), rd_kafka_event_destroy);
  require(value && rd_kafka_event_error(value.get()) == RD_KAFKA_RESP_ERR_NO_ERROR, "Kafka administration failed");
  return value;
}

void inactive(rd_kafka_t* handle, const std::string& group) {
  const Queue queue(rd_kafka_queue_new(handle), rd_kafka_queue_destroy);
  const char* name = group.c_str();
  rd_kafka_DescribeConsumerGroups(handle, &name, 1, nullptr, queue.get());
  const auto response = event(queue);
  std::size_t count = 0;
  const auto* groups = rd_kafka_DescribeConsumerGroups_result_groups(
      rd_kafka_event_DescribeConsumerGroups_result(response.get()), &count);
  require(count == 1, "consumer group result missing");
  const auto* error = rd_kafka_ConsumerGroupDescription_error(*groups);
  require(error == nullptr || rd_kafka_error_code(error) == RD_KAFKA_RESP_ERR_GROUP_ID_NOT_FOUND,
          "consumer group inspection failed");
  require(rd_kafka_ConsumerGroupDescription_member_count(*groups) == 0, "stop every consumer before recovery");
}

struct Reader {
  std::unique_ptr<Consumer> source;
  p::kafka::Offsets targets;
  std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);

  asio::awaitable<void> open(Context* context, std::vector<std::string> topics, std::string group = {}) {
    source = take(Consumer::create(co_await asio::this_coro::executor,
                                   {.client = kafka(context->config.at("kafka")),
                                    .group_id = group.empty() ? "psimctl-read-" + p::stream::uuid7() : group,
                                    .topics = std::move(topics),
                                    .group_protocol = "classic",
                                    .manual = true},
                                   {.blocking = &context->blocking}));
    auto bounds = take(co_await source->offset_bounds(std::chrono::seconds(10)));
    targets = std::move(bounds.end);
    take(source->assign(bounds.begin));
  }

  [[nodiscard]] bool done() const {
    return std::ranges::all_of(targets, [this](const auto& item) {
      const auto found = source->positions().find(item.first);
      return found != source->positions().end() && found->second >= item.second;
    });
  }

  asio::awaitable<std::vector<p::kafka::Message>> next() {
    require(std::chrono::steady_clock::now() < deadline, "Kafka snapshot did not reach its captured boundary");
    auto batch = take(co_await source->poll(128, std::chrono::milliseconds(100)));
    std::erase_if(batch, [this](const auto& message) {
      return !targets.contains(message.topic_partition()) || message.offset() >= targets.at(message.topic_partition());
    });
    co_return batch;
  }
};

Json configurations(rd_kafka_t* handle, const std::string& name) {
  const std::unique_ptr<rd_kafka_ConfigResource_t, decltype(&rd_kafka_ConfigResource_destroy)> resource(
      rd_kafka_ConfigResource_new(RD_KAFKA_RESOURCE_TOPIC, name.c_str()), rd_kafka_ConfigResource_destroy);
  const Queue queue(rd_kafka_queue_new(handle), rd_kafka_queue_destroy);
  // NOLINTNEXTLINE(misc-const-correctness): the C API requires a mutable pointer array.
  rd_kafka_ConfigResource_t* pointer =
      resource.get();  // NOLINT(misc-const-correctness): the C API requires a mutable pointer array
  rd_kafka_DescribeConfigs(handle, &pointer, 1, nullptr, queue.get());
  const auto response = event(queue);
  std::size_t count = 0;
  const auto* resources =
      rd_kafka_DescribeConfigs_result_resources(rd_kafka_event_DescribeConfigs_result(response.get()), &count);
  require(count == 1 && rd_kafka_ConfigResource_error(*resources) == RD_KAFKA_RESP_ERR_NO_ERROR,
          "topic configuration unavailable");
  const auto* entries = rd_kafka_ConfigResource_configs(*resources, &count);
  Json config = Json::object();
  for (std::size_t i = 0; i < count; ++i) {
    const auto* value = rd_kafka_ConfigEntry_value(std::span(entries, count)[i]);
    if (value != nullptr && (rd_kafka_ConfigEntry_is_read_only(std::span(entries, count)[i]) == 0)
        && (rd_kafka_ConfigEntry_is_sensitive(std::span(entries, count)[i]) == 0)) {
      config[rd_kafka_ConfigEntry_name(std::span(entries, count)[i])] = value;
    }
  }
  return config;
}

void create_topic(rd_kafka_t* handle, const Json& entry) {
  const auto name = entry.at("topic").get<std::string>();
  require(name.starts_with("psim.") && name.size() < 250, "invalid product topic name");
  const auto partitions = entry.at("partitions").get<int>();
  const auto replicas = entry.at("replication_factor").get<int>();
  require(partitions > 0 && partitions <= 4096 && replicas > 0 && replicas <= 3, "invalid topic topology");
  std::array<char, 512> error{};
  const std::unique_ptr<rd_kafka_NewTopic_t, decltype(&rd_kafka_NewTopic_destroy)> topic(
      rd_kafka_NewTopic_new(name.c_str(), partitions, replicas, error.data(), error.size()), rd_kafka_NewTopic_destroy);
  require(topic != nullptr, "cannot construct restored topic");
  for (const auto& [key, value] : entry.at("config").items()) {
    require(rd_kafka_NewTopic_set_config(topic.get(), key.c_str(), value.get<std::string>().c_str())
                == RD_KAFKA_RESP_ERR_NO_ERROR,
            "invalid topic configuration");
  }
  const Queue queue(rd_kafka_queue_new(handle), rd_kafka_queue_destroy);
  auto* pointer = topic.get();
  rd_kafka_CreateTopics(handle, &pointer, 1, nullptr, queue.get());
  const auto response = event(queue);
  std::size_t count = 0;
  const auto* topics = rd_kafka_CreateTopics_result_topics(rd_kafka_event_CreateTopics_result(response.get()), &count);
  require(count == 1 && rd_kafka_topic_result_error(*topics) == RD_KAFKA_RESP_ERR_NO_ERROR,
          "restored topic creation failed");
}
}  // namespace

asio::awaitable<void> export_topics(Context* context, fs::path destination) {
  Reader initial;
  co_await initial.open(context, {"psim.config.v1"});
  const auto all = metadata(initial.source->handle());
  std::ofstream output(destination);
  require(output.good(), "cannot create topic snapshot");
  output << Json{{"format", "psim.kafka.snapshot.v1"}}.dump() << '\n';
  for (int i = 0; i < all->topic_cnt; ++i) {
    const auto& topic = std::span(all->topics, static_cast<std::size_t>(all->topic_cnt))[static_cast<std::size_t>(i)];
    const std::string name(topic.topic);
    if (!name.starts_with("psim.")) {
      continue;
    }
    require(topic.err == RD_KAFKA_RESP_ERR_NO_ERROR && topic.partition_cnt > 0, "topic metadata unavailable");
    const auto config = configurations(initial.source->handle(), name);
    output << Json{{"topic", name},
                   {"partitions", topic.partition_cnt},
                   {"replication_factor", topic.partitions->replica_cnt},
                   {"config", config}}
                  .dump()
           << '\n';
    if (config.value("cleanup.policy", "delete").find("compact") == std::string::npos) {
      continue;
    }
    Reader reader;
    co_await reader.open(context, {name});
    while (!reader.done()) {
      auto messages = co_await reader.next();
      for (const auto& message : messages) {
        Json headers = Json::array();
        for (const auto& [key, value] : message.headers()) {
          headers.push_back({key, hex(value)});
        }
        output << Json{{"record", name},
                       {"partition", message.partition()},
                       {"offset", message.offset()},
                       {"key", hex(message.key())},
                       {"value", hex(message.value())},
                       {"tombstone", message.tombstone()},
                       {"headers", headers}}
                      .dump()
               << '\n';
        require(output.good(), "topic snapshot write failed");
      }
    }
    co_await reader.source->close();
  }
  output << Json{{"complete", true}}.dump() << '\n';
  output.flush();
  require(output.good(), "topic snapshot finalization failed");
  co_await initial.source->close();
}

Await import_topics(Context* context, fs::path source) {
  Reader reader;
  reader.source = take(Consumer::create(co_await asio::this_coro::executor,
                                        {.client = kafka(context->config.at("kafka")),
                                         .group_id = "psimctl-restore-" + p::stream::uuid7(),
                                         .topics = {"psim.config.v1"},
                                         .group_protocol = "classic",
                                         .manual = true},
                                        {.blocking = &context->blocking}));
  const auto all = metadata(reader.source->handle());
  std::set<std::string> existing;
  for (int i = 0; i < all->topic_cnt; ++i) {
    existing.insert(
        std::span(all->topics, static_cast<std::size_t>(all->topic_cnt))[static_cast<std::size_t>(i)].topic);
  }
  std::ifstream input(source);
  std::string line;
  require(static_cast<bool>(std::getline(input, line)), "empty topic snapshot");
  require(Json::parse(line) == Json{{"format", "psim.kafka.snapshot.v1"}}, "invalid topic snapshot format");
  std::map<std::string, Json> topics;
  bool complete = false;
  while (std::getline(input, line)) {
    require(line.size() <= static_cast<std::size_t>(32 * 1024 * 1024) && !complete, "invalid topic snapshot framing");
    const auto entry = Json::parse(line);
    if (entry.contains("topic")) {
      const auto name = entry.at("topic").get<std::string>();
      require(!existing.contains(name) && !topics.contains(name), "topic restore requires absent product topics");
      topics[name] = entry;
    } else if (entry.contains("record")) {
      const auto& declaration = topics.at(entry.at("record").get<std::string>());
      require(entry.at("partition").get<int>() >= 0 && entry.at("partition") < declaration.at("partitions"),
              "invalid restored partition");
      (void)unhex(entry.at("key").get<std::string>());
      (void)unhex(entry.at("value").get<std::string>());
    } else {
      require(entry == Json{{"complete", true}}, "invalid snapshot footer");
      complete = true;
    }
  }
  require(complete && !topics.empty(), "incomplete topic snapshot");
  for (const auto& [name, entry] : topics) {
    create_topic(reader.source->handle(), entry);
  }
  auto producer =
      take(Producer::create(co_await asio::this_coro::executor, {.client = kafka(context->config.at("kafka"))},
                            {.blocking = &context->blocking}));
  input.clear();
  input.seekg(0);
  std::size_t count = 0;
  while (std::getline(input, line)) {
    const auto entry = Json::parse(line);
    if (!entry.contains("record")) {
      continue;
    }
    p::kafka::Record record{.topic = entry.at("record"),
                            .key = unhex(entry.at("key").get<std::string>()),
                            .value = unhex(entry.at("value").get<std::string>()),
                            .partition = entry.at("partition"),
                            .tombstone = entry.value("tombstone", false)};
    for (const auto& header : entry.at("headers")) {
      record.headers.emplace_back(header.at(0), unhex(header.at(1).get<std::string>()));
    }
    take(co_await producer->enqueue(std::move(record)));
    ++count;
  }
  take(co_await producer->flush(std::chrono::minutes(2)));
  co_await reader.source->close();
  co_return Json{{"records", count}, {"topics", topics.size()}, {"consumer_offsets_restored", false}};
}

Await kafka_operation(Context* context, Arguments args) {
  require(args.words.size() == 2, "Kafka operation action required");
  const auto config = context->config.at("kafka");
  if (args.words[0] == "dlq") {
    const auto topic = args.one("--topic");
    const auto partition = args.number("--partition");
    auto from = args.number("--from-offset");
    const auto to = args.number("--to-offset");
    require(topic.starts_with("psim.dlq.") && topic.ends_with(".v1") && partition >= 0 && partition < 4096 && from >= 0
                && from < to && to - from <= 10000,
            "invalid DLQ range");
    const bool apply = args.has("--apply");
    require(args.words[1] == "inspect" || args.words[1] == "retry", "unknown DLQ operation");
    require(!apply || args.words[1] == "retry", "inspect cannot apply changes");
    const auto group = "psimctl-" + topic + "-" + std::to_string(partition);
    Reader reader;
    co_await reader.open(context, {topic}, group);
    const TopicPartition target{topic, static_cast<std::int32_t>(partition)};
    auto previous = co_await reader.source->committed({target}, std::chrono::seconds(10));
    for (int attempt = 1; !previous && attempt < 3; ++attempt) {
      asio::steady_timer timer(co_await asio::this_coro::executor, std::chrono::milliseconds(500));
      co_await timer.async_wait(asio::use_awaitable);
      previous = co_await reader.source->committed({target}, std::chrono::seconds(10));
    }
    if (!previous) {
      throw OperationError("DLQ committed offsets: " + previous.error().message());
    }
    const auto committed = take(std::move(previous));
    if (committed.contains(target)) {
      from = std::max(from, committed.at(target));
    }
    if (from >= to) {
      co_await reader.source->close();
      co_return Json{{"records", 0}, {"already_processed", true}};
    }
    const auto bounds = take(co_await reader.source->offset_bounds(std::chrono::seconds(10)));
    require(bounds.begin.at(target) <= from && to <= bounds.end.at(target), "requested DLQ history is not retained");
    reader.targets = {{target, to}};
    take(reader.source->assign({{target, from}}));
    std::unique_ptr<Producer> producer;
    if (apply) {
      producer = take(Producer::create(co_await asio::this_coro::executor,
                                       {.client = kafka(config), .transactional_id = group},
                                       {.blocking = &context->blocking}));
      take(co_await producer->init_transactions(std::chrono::seconds(15)));
      take(producer->begin_transaction());
    }
    std::size_t count = 0;
    Json routes = Json::array();
    while (!reader.done()) {
      auto messages = co_await reader.next();
      for (const auto& message : messages) {
        const std::string origin(message.header("psim-dlq-source-topic").value_or(""));
        const auto allowed = config.at("dlq_sources").at(topic).get<std::vector<std::string>>();
        require(std::ranges::find(allowed, origin) != allowed.end(), "DLQ route is outside its allowed source topics");
        const auto original = message.header("psim-dlq-source-partition");
        require(original.has_value(), "DLQ original partition missing");
        const auto number = std::stoll(std::string(original.value_or("")));
        require(number >= 0 && number <= 4095, "invalid original partition");
        if (apply) {
          p::kafka::Record record{.topic = origin,
                                  .key = std::string(message.key()),
                                  .value = std::string(message.value()),
                                  .partition = static_cast<std::int32_t>(number),
                                  .tombstone = message.tombstone()};
          for (const auto& header : message.headers()) {
            if (!header.first.starts_with("psim-dlq-")) {
              record.headers.push_back(header);
            }
          }
          take(co_await producer->enqueue(std::move(record)));
        } else {
          routes.push_back({{"offset", message.offset()}, {"topic", origin}, {"partition", number}});
        }
        ++count;
      }
    }
    if (apply) {
      take(co_await producer->commit_transaction(reader.source.get(), {{target, to}}, std::chrono::seconds(30)));
    }
    co_await reader.source->close();
    co_return Json{{"records", count}, {"applied", apply}, {"routes", routes}};
  }
  if (args.words[0] == "normalizer") {
    require(args.words[1] == "cleanup", "unknown normalizer operation");
    const auto tenant = args.one("--tenant");
    require(p::stream::uuid(tenant), "invalid cleanup tenant");
    const auto& ids = args.flags.at("--source");
    require(!ids.empty() && ids.size() <= 1000, "invalid cleanup scope");
    std::set<std::string> prefixes;
    auto connection = take(co_await p::pg::Connection::connect(postgres(context->config.at("postgres"))));
    for (const auto& id : ids) {
      require(p::stream::uuid(id), "invalid cleanup source");
      auto rows = take(co_await connection->query(
          {.sql = "SELECT r.body->>'state' FROM catalog.sources s JOIN catalog.resources r ON r.tenant=s.tenant AND "
                  "r.kind='device' AND r.id=s.device WHERE s.tenant=$1::uuid AND s.source=$2::uuid",
           .parameters = {Param::text(tenant), Param::text(id)}}));
      require(rows.size() == 1 && rows.value(0, 0) == "RESOURCE_STATE_ARCHIVED",
              "cleanup source must belong to an archived device");
      auto prefix = tenant;
      prefix += "/";
      prefix += id;
      prefix += ":";
      prefixes.insert(std::move(prefix));
    }
    Reader reader;
    co_await reader.open(context, {"psim.normalizer.state.v1"});
    inactive(reader.source->handle(), "psim-normalizer");
    std::map<std::pair<std::int32_t, std::string>, bool> cursors;
    while (!reader.done()) {
      auto messages = co_await reader.next();
      for (const auto& message : messages) {
        if (std::ranges::any_of(prefixes,
                                [&message](const auto& prefix) { return message.key().starts_with(prefix); })) {
          cursors[{message.partition(), std::string(message.key())}] = !message.tombstone();
        }
      }
    }
    const bool apply = args.has("--apply");
    std::size_t count = 0;
    auto producer = take(Producer::create(co_await asio::this_coro::executor,
                                          {.client = kafka(config), .transactional_id = "psimctl-normalizer-cleanup"},
                                          {.blocking = &context->blocking}));
    if (apply) {
      take(co_await producer->init_transactions(std::chrono::seconds(15)));
      take(producer->begin_transaction());
    }
    for (const auto& [cursor, present] : cursors) {
      if (!present) {
        continue;
      }
      ++count;
      if (apply) {
        take(co_await producer->enqueue(
            {.topic = "psim.normalizer.state.v1", .key = cursor.second, .partition = cursor.first, .tombstone = true}));
      }
    }
    if (apply) {
      take(co_await producer->commit_transaction(nullptr, {}, std::chrono::seconds(30)));
    }
    co_await reader.source->close();
    co_return Json{{"cursors", count}, {"applied", apply}};
  }
  require(args.words[0] == "history" && args.words[1] == "reload", "unknown Kafka operation");
  const auto group = args.one("--group");
  const std::map<std::string, std::vector<std::string>> loaders{
      {"psim-event-history", {"psim.events.normalized.v1"}},
      {"psim-history-facts", {"psim.signals.v1", "psim.incidents.events.v1"}},
      {"psim-projection-service", {"psim.incidents.events.v1", "psim.response.events.v1"}}};
  require(loaders.contains(group), "unsupported history loader group");
  Reader reader;
  co_await reader.open(context, loaders.at(group), group);
  inactive(reader.source->handle(), group);
  auto connection = take(co_await p::pg::Connection::connect(postgres(context->config.at("postgres"))));
  const std::map<std::string, std::string> journals{
      {"psim-event-history", "psim-event-history/events_v1"},
      {"psim-history-facts", "psim-history-facts/history_facts_v1"},
      {"psim-projection-service", "psim-projection-service/incident_facts_v1"}};
  auto pending =
      take(co_await connection->query({.sql = "SELECT count(*) FROM psim_clickhouse.pending_batches WHERE loader=$1",
                                       .parameters = {Param::text(journals.at(group))}}));
  require(pending.value(0, 0) == "0", "finish prepared ClickHouse batches before resetting loader offsets");
  const auto bounds = take(co_await reader.source->offset_bounds(std::chrono::seconds(10)));
  Json offsets = Json::array();
  for (const auto& [partition, offset] : bounds.begin) {
    offsets.push_back({{"topic", partition.topic}, {"partition", partition.partition}, {"offset", offset}});
  }
  if (args.has("--apply")) {
    take(co_await reader.source->commit(bounds.begin));
  }
  co_await reader.source->close();
  co_return Json{{"applied", args.has("--apply")}, {"offsets", offsets}, {"retention_limited", true}};
}
}  // namespace psim::ctl
