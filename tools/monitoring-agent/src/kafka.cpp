#include <librdkafka/rdkafka.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "common.hpp"
#include "monitor.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"

namespace psim::monitoring {
namespace {
std::string collect(rd_kafka_t* handle) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  const auto remaining = [&deadline]() {
    const auto value =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    require(value > 0, "Kafka diagnostics budget exceeded");
    return static_cast<int>(std::min<std::int64_t>(3000, value));
  };
  const rd_kafka_metadata_t* metadata_pointer = nullptr;
  require(rd_kafka_metadata(handle, 1, nullptr, &metadata_pointer, remaining()) == RD_KAFKA_RESP_ERR_NO_ERROR,
          "Kafka metadata unavailable");
  const std::unique_ptr<const rd_kafka_metadata_t, decltype(&rd_kafka_metadata_destroy)> metadata(
      metadata_pointer, rd_kafka_metadata_destroy);
  std::map<std::pair<std::string, std::int32_t>, std::int64_t> offsets;
  std::map<std::string, std::int64_t> dlq;
  double under_replicated = 0;
  double offline = 0;
  for (int i = 0; i < metadata->topic_cnt; ++i) {
    const auto& topic =
        std::span(metadata->topics, static_cast<std::size_t>(metadata->topic_cnt))[static_cast<std::size_t>(i)];
    const std::string name(topic.topic);
    if (!name.starts_with("psim.")) {
      continue;
    }
    require(topic.err == RD_KAFKA_RESP_ERR_NO_ERROR, "topic metadata incomplete");
    for (int j = 0; j < topic.partition_cnt; ++j) {
      const auto& partition =
          std::span(topic.partitions, static_cast<std::size_t>(topic.partition_cnt))[static_cast<std::size_t>(j)];
      under_replicated += partition.isr_cnt < partition.replica_cnt ? 1 : 0;
      offline += partition.leader < 0 ? 1 : 0;
      std::int64_t low = 0;
      std::int64_t high = 0;
      require(rd_kafka_query_watermark_offsets(handle, name.c_str(), partition.id, &low, &high, remaining())
                  == RD_KAFKA_RESP_ERR_NO_ERROR,
              "Kafka offsets unavailable");
      offsets[{name, partition.id}] = high;
      if (name.starts_with("psim.dlq.")) {
        dlq[name] += high;
      }
    }
  }
  std::string output = sample("psim_kafka_under_replicated_partitions", under_replicated)
                       + sample("psim_kafka_offline_partitions", offline);
  for (const auto& [topic, offset] : dlq) {
    output += sample("psim_kafka_dlq_end_offset", static_cast<double>(offset), {{"topic", topic}});
  }
  const std::unique_ptr<rd_kafka_queue_t, decltype(&rd_kafka_queue_destroy)> queue(rd_kafka_queue_new(handle),
                                                                                   rd_kafka_queue_destroy);
  rd_kafka_ListConsumerGroups(handle, nullptr, queue.get());
  const std::unique_ptr<rd_kafka_event_t, decltype(&rd_kafka_event_destroy)> listing(
      rd_kafka_queue_poll(queue.get(), remaining()), rd_kafka_event_destroy);
  require(listing && rd_kafka_event_error(listing.get()) == RD_KAFKA_RESP_ERR_NO_ERROR, "consumer groups unavailable");
  const auto* result = rd_kafka_event_ListConsumerGroups_result(listing.get());
  std::size_t count = 0;
  (void)rd_kafka_ListConsumerGroups_result_errors(result, &count);
  require(count == 0, "consumer group listing incomplete");
  const auto* groups = rd_kafka_ListConsumerGroups_result_valid(result, &count);
  constexpr std::array<std::string_view, 9> kPrefixes{"psim-",           "console-",     "correlation-",
                                                      "gateway-",        "realtime",     "catalog-",
                                                      "incident-facts-", "rule-replay-", "psimctl-"};
  for (std::size_t i = 0; i < count; ++i) {
    const std::string group(rd_kafka_ConsumerGroupListing_group_id(std::span(groups, count)[i]));
    if (!std::ranges::any_of(kPrefixes, [&group](auto prefix) { return group.starts_with(prefix); })) {
      continue;
    }
    const std::unique_ptr<rd_kafka_ListConsumerGroupOffsets_t, decltype(&rd_kafka_ListConsumerGroupOffsets_destroy)>
        request(rd_kafka_ListConsumerGroupOffsets_new(group.c_str(), nullptr),
                rd_kafka_ListConsumerGroupOffsets_destroy);
    auto* pointer = request.get();
    rd_kafka_ListConsumerGroupOffsets(handle, &pointer, 1, nullptr, queue.get());
    const std::unique_ptr<rd_kafka_event_t, decltype(&rd_kafka_event_destroy)> response(
        rd_kafka_queue_poll(queue.get(), remaining()), rd_kafka_event_destroy);
    require(response && rd_kafka_event_error(response.get()) == RD_KAFKA_RESP_ERR_NO_ERROR,
            "consumer offsets unavailable");
    std::size_t group_count = 0;
    const auto* entries = rd_kafka_ListConsumerGroupOffsets_result_groups(
        rd_kafka_event_ListConsumerGroupOffsets_result(response.get()), &group_count);
    require(group_count == 1 && rd_kafka_group_result_error(*entries) == nullptr, "group offset result missing");
    const auto* partitions = rd_kafka_group_result_partitions(*entries);
    for (int j = 0; partitions != nullptr && j < partitions->cnt; ++j) {
      const auto& partition =
          std::span(partitions->elems, static_cast<std::size_t>(partitions->cnt))[static_cast<std::size_t>(j)];
      require(partition.err == RD_KAFKA_RESP_ERR_NO_ERROR, "group partition offset unavailable");
      const auto key = std::pair(std::string(partition.topic), partition.partition);
      if (partition.offset >= 0 && offsets.contains(key)) {
        output += sample(
            "psim_kafka_group_lag", static_cast<double>(std::max<std::int64_t>(0, offsets.at(key) - partition.offset)),
            {{"group", group}, {"topic", partition.topic}, {"partition", std::to_string(partition.partition)}});
      }
    }
  }
  return output;
}
}  // namespace

asio::awaitable<std::string> monitor_kafka(Json config, p::async::BlockingPool* blocking) {
  p::kafka::ClientOptions client{
      .bootstrap = config.at("kafka").at("brokers"),
      .client_id = "monitoring",
      .security = p::kafka::TransportSecurity{.tls = {.ca_file = config.at("tls").at("ca"),
                                                      .certificate_file = config.at("tls").at("cert"),
                                                      .private_key_file = config.at("tls").at("key")}}};
  auto source = take(p::kafka::Consumer::create(co_await asio::this_coro::executor,
                                                {.client = std::move(client),
                                                 .group_id = "monitoring-diagnostics",
                                                 .topics = {"psim.config.v1"},
                                                 .group_protocol = "classic",
                                                 .manual = true},
                                                {.blocking = blocking}));
  auto value = co_await blocking->run([handle = source->handle()] { return collect(handle); });
  co_await source->close();
  co_return take(std::move(value));
}
}  // namespace psim::monitoring
