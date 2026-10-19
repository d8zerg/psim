#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <psim/connector/v1/connector.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <ratio>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../common/settings.hpp"
#include "model.hpp"
#include "psim/platform/connector/client.hpp"
#include "psim/platform/connector/grpc_process.hpp"

int main(int argc, char** argv) {
  try {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): argc guards argv access
    if (argc == 2 && std::string(argv[1]) == "--version") {
      std::cout << "psim-simulator " << PSIM_CONNECTOR_VERSION << '\n';
      return 0;
    }
    namespace sdk = psim::platform::connector;
    namespace sim = psim::connectors::simulator;
    using psim::connectors::require;
    std::string manifest_path;
    sim::Profile profile;
    auto options = psim::connectors::options("simulator-2");
    for (int index = 1; index < argc; ++index) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): bounded command-line argument index
      const std::string option = argv[index];
      if (option == "--conformance") {
        profile.conformance = true;
        profile.count = 20;
        profile.batch = 1;
        profile.devices = 1;
        profile.rate = 20;
        continue;
      }
      require(index + 1 < argc, "simulator option requires a value");
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): bounded command-line argument index
      const std::string value = argv[++index];
      if (option == "--count") {
        profile.count = std::stoull(value);
      } else if (option == "--devices") {
        profile.devices = std::stoull(value);
      } else if (option == "--batch") {
        profile.batch = std::stoull(value);
      } else if (option == "--rate") {
        profile.rate = std::stoull(value);
      } else if (option == "--scenario") {
        profile.scenario = value;
      } else if (option == "--run-id") {
        profile.run_id = value;
      } else if (option == "--burst-every") {
        profile.burst_every = std::stoull(value);
      } else if (option == "--burst-size") {
        profile.burst_size = std::stoull(value);
      } else if (option == "--source-prefix") {
        profile.source_prefix = value;
      } else if (option == "--endpoint") {
        options.endpoint = value;
      } else if (option == "--manifest") {
        manifest_path = value;
      } else if (option == "--buffer") {
        options.buffer_directory = value;
      } else {
        throw std::invalid_argument("unknown simulator option");
      }
    }
    require(!options.buffer_directory.empty(), "simulator requires a persistent buffer directory");
    options.max_batch_events = 1000;
    options.max_batch_bytes = 250000;
    options.max_buffer_bytes = 512ULL * 1024 * 1024;
    options.max_pending_events = 1000000;
    sim::Model model(profile);
    std::ofstream manifest;
    if (!manifest_path.empty()) {
      manifest.open(manifest_path, std::ios::out | std::ios::trunc);
      require(manifest.is_open(), "simulator manifest unavailable");
    }
    const sdk::GrpcProcess grpc;
    sdk::Client client(
        options,
        {.command = [&](const auto& command, const auto& stop) { return model.command(command, stop); },
         .snapshot = [&](const auto&, const auto& stop) { return model.snapshot(stop); },
         .rejection =
             [](const auto& rejection) {
               std::cerr << nlohmann::json{{"rejected_event", rejection.event_id()}, {"code", rejection.code()}}.dump()
                         << '\n';
             }});
    const auto recovered = client.diagnostics().pending_events;
    require(client.start().has_value(), "simulator SDK startup failed");
    const auto started = std::chrono::steady_clock::now();
    const auto wall = std::chrono::system_clock::now();
    double max_lateness_ms = 0;
    for (std::size_t index = 0; index < profile.count;) {
      const auto offset = std::chrono::nanoseconds(model.scheduled_nanoseconds(index));
      std::this_thread::sleep_until(started + offset);
      max_lateness_ms =
          profile.rate == 0
              ? 0
              : std::max(max_lateness_ms,
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started - offset)
                             .count());
      const auto count = std::min(profile.rate == 0 ? profile.batch : std::size_t{1}, profile.count - index);
      std::vector<sim::wire::ConnectorEvent> batch;
      for (std::size_t item = 0; item < count; ++item) {
        auto event = model.event(index + item);
        const auto at =
            profile.rate == 0
                ? std::chrono::system_clock::now().time_since_epoch()
                : (wall + std::chrono::nanoseconds(model.scheduled_nanoseconds(index + item))).time_since_epoch();
        *event.mutable_occurred_at() = google::protobuf::util::TimeUtil::NanosecondsToTimestamp(
            std::chrono::duration_cast<std::chrono::nanoseconds>(at).count());
        batch.push_back(std::move(event));
      }
      const auto stored = client.publish_batch(std::move(batch), sdk::Observation::kCurrent);
      require(stored.has_value(), "simulator durable admission failed");
      if (manifest.is_open()) {
        for (const auto& event : *stored) {
          manifest << event.event_id() << '\n';
        }
        require(manifest.good(), "simulator manifest write failed");
      }
      index += count;
    }
    if (manifest.is_open()) {
      manifest.flush();
      require(manifest.good(), "simulator manifest flush failed");
    }
    const auto generated = std::chrono::steady_clock::now();
    const auto timeout = std::stoi(psim::connectors::setting("PSIM_CONNECTOR_FLUSH_TIMEOUT_SECONDS", "120"));
    require(client.flush(std::chrono::seconds(timeout)), "simulator buffer did not drain");
    const auto drained = std::chrono::steady_clock::now();
    if (profile.conformance) {
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    client.stop();
    const auto data = client.diagnostics();
    const auto generation_seconds = std::chrono::duration<double>(generated - started).count();
    const auto delivery_seconds = std::chrono::duration<double>(drained - started).count();
    std::cout << nlohmann::json{{"run_id", profile.run_id},
                                {"recovered", recovered},
                                {"generated", profile.count},
                                {"devices", profile.devices},
                                {"generation_seconds", generation_seconds},
                                {"generation_events_per_second",
                                 static_cast<double>(profile.count) / generation_seconds},
                                {"delivery_seconds", delivery_seconds},
                                {"acknowledged", data.acknowledged},
                                {"rejected", data.rejected},
                                {"pending", data.pending_events},
                                {"max_lateness_ms", max_lateness_ms}}
                     .dump()
              << '\n';
    return data.pending_events == 0 && data.acknowledged + data.rejected == profile.count + recovered
                   && data.rejected == (profile.conformance ? 1U : 0U)
               ? 0
               : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
