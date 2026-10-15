#include "psim/platform/connector/client.hpp"

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/util/time_util.h>
#include <grpc/grpc_security_constants.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/support/channel_arguments.h>
#include <psim/connector/v1/connector.pb.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <expected>
#include <fstream>
#include <functional>
#include <ios>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "psim/connector/v1/connector.grpc.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/secret.hpp"
#include "store.hpp"

namespace psim::platform::connector {
namespace {
using Clock = std::chrono::steady_clock;

std::string pem(const std::string& path) {
  std::ifstream input(path);
  std::string text;
  std::array<char, 4096> buffer{};
  while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || input.gcount() > 0) {
    text.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
    if (text.size() > 1048576) {
      throw std::runtime_error("TLS material exceeds size bound");
    }
  }
  if (text.empty() || !input.eof()) {
    throw std::runtime_error("TLS material unavailable");
  }
  return text;
}

std::chrono::milliseconds duration(const google::protobuf::Duration& value) {
  if (!google::protobuf::util::TimeUtil::IsDurationValid(value)) {
    throw std::runtime_error("invalid session duration");
  }
  const auto result = std::chrono::milliseconds(google::protobuf::util::TimeUtil::DurationToMilliseconds(value));
  if (result < std::chrono::milliseconds(0) || result > std::chrono::minutes(10)) {
    throw std::runtime_error("session duration exceeds supported bound");
  }
  return result;
}

wire::SessionRequest heartbeat() {
  wire::SessionRequest request;
  *request.mutable_heartbeat()->mutable_sent_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
  return request;
}

bool feature(const wire::Registered& registration, std::string_view name) {
  return std::ranges::any_of(registration.capabilities(), [name](const auto& value) { return value == name; });
}
}  // namespace

Result<std::shared_ptr<grpc::Channel>> channel(const Options& options) {
  try {
    if (options.endpoint.empty()) {
      return fail(ErrorCode::kCommonInvalidArgument, "connector endpoint required");
    }
    auto key = security::read_secret(options.private_key_file);
    if (!key) {
      return std::unexpected(key.error());
    }
    auto provider = std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
    const auto roots = provider->UpdateRoot(pem(options.ca_file));
    const auto identity =
        provider->UpdateIdentityKeyCertPair(std::vector<grpc::experimental::IdentityKeyOrSignerCertPair>{
            {std::string(key->view()), pem(options.certificate_file)}});
    if (!roots.ok() || !identity.ok()) {
      return fail(ErrorCode::kCommonInvalidArgument, "invalid connector TLS material");
    }
    grpc::experimental::TlsChannelCredentialsOptions tls;
    tls.set_root_certificate_provider(provider);
    tls.set_identity_certificate_provider(provider);
    tls.set_min_tls_version(TLS1_3);
    grpc::ChannelArguments arguments;
    arguments.SetMaxReceiveMessageSize(256 * 1024);
    arguments.SetMaxSendMessageSize(256 * 1024);
    return grpc::CreateCustomChannel(options.endpoint, grpc::experimental::TlsCredentials(tls), arguments);
  } catch (const std::exception&) {
    return fail(ErrorCode::kCommonInvalidArgument, "connector TLS material unavailable or invalid");
  }
}

class Client::Impl {
 public:
  Impl(Options options, Callbacks callbacks)
      : options_(std::move(options)), callbacks_(std::move(callbacks)), store_(options_) {
    if (options_.reconnect_min < std::chrono::milliseconds(1) || options_.reconnect_max < options_.reconnect_min
        || options_.registration_timeout < std::chrono::milliseconds(10)
        || options_.acknowledgement_timeout < std::chrono::milliseconds(10)
        || options_.registration_timeout > std::chrono::minutes(10)
        || options_.acknowledgement_timeout > std::chrono::minutes(10)
        || options_.reconnect_max > std::chrono::minutes(10) || options_.connector_version.empty()) {
      throw std::invalid_argument("invalid connector timing or version");
    }
  }

  ~Impl() { stop(); }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  Result<void> start() {
    const std::scoped_lock lifecycle(lifecycle_);
    if (worker_.joinable()) {
      return fail(ErrorCode::kCommonInvalidArgument, "connector already started");
    }
    auto connection = channel(options_);
    if (!connection) {
      return std::unexpected(connection.error());
    }
    channel_ = *connection;
    commands_ = std::jthread([this](const std::stop_token& stop) { controls(stop); });
    worker_ = std::jthread([this](const std::stop_token& stop) { run(stop); });
    return {};
  }

  void stop() {
    const std::scoped_lock lifecycle(lifecycle_);
    if (worker_.joinable()) {
      worker_.request_stop();
      commands_.request_stop();
      {
        const std::scoped_lock lock(mutex_);
        if (context_ != nullptr) {
          context_->TryCancel();
        }
        tasks_.clear();
      }
      changed_.notify_all();
      worker_.join();
      commands_.join();
      channel_.reset();
    }
  }

  Result<wire::ConnectorEvent> publish(wire::ConnectorEvent event, Observation observation) {
    try {
      {
        const std::scoped_lock lock(mutex_);
        event.set_observation_session_id(session_);
        event.set_live_observation(observation == Observation::kCurrent && !session_.empty());
      }
      auto result = store_.append(std::move(event));
      changed_.notify_all();
      return result;
    } catch (const std::exception& error) {
      set_error(error.what());
      return fail(ErrorCode::kCommonUnavailable, "event not confirmed by local durable buffer; inspect diagnostics");
    }
  }

  Result<void> report(wire::StateObservation observation) {
    if (!observation.has_observed_at() || !google::protobuf::util::TimeUtil::IsTimestampValid(observation.observed_at())
        || observation.cursor().source_ref().empty() || observation.cursor().source_ref().size() > 256
        || observation.values_size() > 64 || observation.ByteSizeLong() > 32768) {
      return fail(ErrorCode::kCommonInvalidArgument, "invalid current state observation");
    }
    std::string observed_session;
    {
      const std::scoped_lock lock(mutex_);
      observed_session = session_;
    }
    const auto cursors = store_.cursors();
    for (const auto& cursor : cursors) {
      if (cursor.source_ref() == observation.cursor().source_ref()) {
        *observation.mutable_cursor() = cursor;
      }
    }
    const std::scoped_lock lock(mutex_);
    if (!state_enabled_ || session_.empty() || session_ != observed_session || outgoing_.size() >= 64) {
      return fail(ErrorCode::kCommonUnavailable, "state reporting unavailable; keep quality unknown");
    }
    wire::SessionRequest request;
    *request.mutable_state_report()->add_observations() = std::move(observation);
    outgoing_.push_back(std::move(request));
    changed_.notify_all();
    return {};
  }

  bool flush(std::chrono::milliseconds timeout) {
    const auto end = Clock::now() + timeout;
    while (store_.count() != 0 && Clock::now() < end) {
      std::unique_lock lock(mutex_);
      changed_.wait_for(lock, std::chrono::milliseconds(10));
    }
    return store_.count() == 0;
  }

  Diagnostics diagnostics() const {
    const auto count = store_.count();
    const auto bytes = store_.bytes();
    const std::scoped_lock lock(mutex_);
    auto result = diagnostics_;
    result.pending_events = count;
    result.buffer_bytes = bytes;
    result.connected = !session_.empty();
    return result;
  }

 private:
  void set_error(std::string error) {
    const std::scoped_lock lock(mutex_);
    diagnostics_.last_error = std::move(error);
  }

  void controls(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
      std::function<void(std::stop_token)> task;
      {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, std::chrono::milliseconds(20),
                          [this, stop] { return !tasks_.empty() || stop.stop_requested(); });
        if (stop.stop_requested()) {
          return;
        }
        if (tasks_.empty()) {
          continue;
        }
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }
      try {
        task(stop);
      } catch (const std::exception&) {
        set_error("connector callback or durable result failed");
      }
    }
  }

  bool enqueue(std::function<void(std::stop_token)> task) {
    const std::scoped_lock lock(mutex_);
    if (tasks_.size() >= 32) {
      return false;
    }
    tasks_.push_back(std::move(task));
    changed_.notify_all();
    return true;
  }

  void command(const wire::Command& command, bool enabled) {
    if (!detail::uuid(command.command_id()) || command.device_external_id().empty() || command.capability().empty()
        || command.parameters_size() > 64 || !command.has_deadline_at()
        || !google::protobuf::util::TimeUtil::IsTimestampValid(command.deadline_at())
        || command.ByteSizeLong() > 32768) {
      throw std::runtime_error("invalid command frame");
    }
    const bool scheduled = enqueue([this, command, enabled](const std::stop_token& stop) {
      auto claimed = store_.claim(command);
      {
        const std::scoped_lock lock(mutex_);
        wire::SessionRequest received;
        received.mutable_command_ack()->set_command_id(command.command_id());
        if (outgoing_.size() >= 64) {
          throw std::runtime_error("command response queue full");
        }
        outgoing_.push_back(std::move(received));
      }
      if (claimed.execute) {
        auto& result = claimed.result;
        const auto expired = [&command] {
          const auto now = google::protobuf::util::TimeUtil::GetCurrentTime();
          return std::make_tuple(command.deadline_at().seconds(), command.deadline_at().nanos())
                 <= std::make_tuple(now.seconds(), now.nanos());
        };
        if (expired()) {
          result.set_code("COMMAND_EXPIRED");
          result.set_message("command deadline elapsed before execution");
        } else if (!enabled || !callbacks_.command) {
          result.set_code("COMMAND_CAPABILITY_UNSUPPORTED");
          result.set_message("command execution not supported");
        } else {
          try {
            result = callbacks_.command(command, stop);
            result.set_command_id(command.command_id());
            if (expired() && !result.success()) {
              result.set_code("COMMAND_TIMED_OUT");
            }
          } catch (const std::exception&) {
            result.set_success(false);
            result.set_command_id(command.command_id());
            result.set_code("COMMAND_DEVICE_REJECTED");
            result.set_message("command handler failed");
          }
        }
        if (result.message().size() > 1024 || (!result.success() && !parse_error_code(result.code()))) {
          result.set_success(false);
          result.set_code("COMMAND_DEVICE_REJECTED");
          result.set_message("invalid command handler result");
        }
        store_.complete(result);
      }
      {
        const std::scoped_lock lock(mutex_);
        if (outgoing_.size() >= 64) {
          throw std::runtime_error("command response queue full");
        }
        wire::SessionRequest response;
        *response.mutable_command_result() = std::move(claimed.result);
        outgoing_.push_back(std::move(response));
      }
      changed_.notify_all();
    });
    if (!scheduled) {
      throw std::runtime_error("command worker queue full");
    }
  }

  void reconcile(const std::string& session) {
    if (!callbacks_.snapshot) {
      return;
    }
    const auto boundaries = store_.cursors();
    if (!enqueue([this, session, boundaries](const std::stop_token& stop) {
          auto observations = callbacks_.snapshot(boundaries, stop);
          if (observations.size() > 1000) {
            throw std::runtime_error("snapshot exceeds source bound");
          }
          std::deque<wire::SessionRequest> reports;
          wire::SessionRequest request;
          auto* report = request.mutable_state_report();
          report->set_snapshot(true);
          report->set_snapshot_id(detail::uuid7());
          for (auto& observation : observations) {
            const auto source = observation.cursor().source_ref();
            wire::SourceCursor boundary;
            boundary.set_source_ref(source);
            boundary.set_source_epoch(1);
            for (const auto& cursor : boundaries) {
              if (cursor.source_ref() == source) {
                boundary = cursor;
              }
            }
            *observation.mutable_cursor() = boundary;
            if (source.empty() || source.size() > 256 || !observation.has_observed_at()
                || !google::protobuf::util::TimeUtil::IsTimestampValid(observation.observed_at())
                || observation.values_size() > 64 || observation.ByteSizeLong() > 32768) {
              throw std::runtime_error("invalid equipment snapshot");
            }
            if (report->ByteSizeLong() + observation.ByteSizeLong() + 8 > 65536) {
              const auto id = report->snapshot_id();
              reports.push_back(std::move(request));
              request.Clear();
              report = request.mutable_state_report();
              report->set_snapshot(true);
              report->set_snapshot_id(id);
            }
            *report->add_observations() = std::move(observation);
          }
          report->set_complete(true);
          reports.push_back(std::move(request));
          for (auto& item : reports) {
            std::unique_lock lock(mutex_);
            while (session_ == session && outgoing_.size() >= 64 && !stop.stop_requested()) {
              changed_.wait_for(lock, std::chrono::milliseconds(20));
            }
            if (session_ != session || stop.stop_requested()) {
              return;
            }
            outgoing_.push_back(std::move(item));
            lock.unlock();
            changed_.notify_all();
          }
        })) {
      throw std::runtime_error("snapshot worker queue full");
    }
  }

  void session(const std::stop_token& stop) {
    auto stub = wire::ConnectorGatewayService::NewStub(channel_);
    grpc::ClientContext context;
    {
      const std::scoped_lock lock(mutex_);
      if (stop.stop_requested()) {
        return;
      }
      context_ = &context;
      incoming_.clear();
      outgoing_.clear();
    }
    std::atomic_bool ended{false};
    std::atomic<Clock::duration::rep> deadline{
        (Clock::now() + options_.registration_timeout).time_since_epoch().count()};
    std::jthread watchdog([&](const std::stop_token& watch_stop) {
      while (!watch_stop.stop_requested()) {
        if (Clock::now().time_since_epoch().count() > deadline.load()) {
          context.TryCancel();
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });
    auto stream = stub->Session(&context);
    std::jthread reader([&] {
      wire::SessionResponse response;
      while (stream->Read(&response)) {
        const std::scoped_lock lock(mutex_);
        if (incoming_.size() >= 64) {
          context.TryCancel();
          break;
        }
        incoming_.push_back(std::move(response));
        response.Clear();
        changed_.notify_all();
      }
      ended.store(true);
      changed_.notify_all();
    });
    try {
      wire::SessionRequest first;
      auto* registration = first.mutable_register_();
      registration->add_protocol_versions("1.1");
      registration->add_protocol_versions("1.0");
      registration->set_instance_id(instance_);
      registration->set_connector_version(options_.connector_version);
      registration->set_config_version(config_version_);
      registration->add_capabilities("state.v1");
      if (callbacks_.snapshot) {
        registration->add_capabilities("snapshot.v1");
      }
      if (callbacks_.command) {
        registration->add_capabilities("commands.v1");
      }
      if (!stream->Write(first)) {
        throw std::runtime_error("connector registration write failed");
      }
      wire::Registered parameters;
      auto last_received = Clock::now();
      auto last_heartbeat = last_received;
      auto batch_sent = last_received;
      std::optional<wire::EventBatch> inflight;
      std::uint64_t batch_sequence = 0;
      std::uint64_t credits = 0;
      bool registered = false;
      while (!stop.stop_requested()) {
        std::deque<wire::SessionResponse> responses;
        {
          std::unique_lock lock(mutex_);
          changed_.wait_for(lock, std::chrono::milliseconds(5));
          responses.swap(incoming_);
        }
        for (const auto& response : responses) {
          last_received = Clock::now();
          if (!registered) {
            if (!response.has_registered()) {
              throw std::runtime_error("Registered must be the first server message");
            }
            parameters = response.registered();
            if ((parameters.protocol_version() != "1.0" && parameters.protocol_version() != "1.1")
                || parameters.session_id().empty() || parameters.max_batch_events() == 0
                || parameters.max_batch_bytes() < 128 || parameters.max_batch_bytes() > 1048576
                || duration(parameters.heartbeat_interval()) < std::chrono::milliseconds(10)
                || duration(parameters.session_timeout()) <= duration(parameters.heartbeat_interval())) {
              throw std::runtime_error("invalid negotiated session parameters");
            }
            credits = parameters.initial_credits();
            registered = true;
            {
              const std::scoped_lock lock(mutex_);
              session_ = parameters.session_id();
              state_enabled_ = feature(parameters, "state.v1");
              ++diagnostics_.sessions;
              diagnostics_.last_error.clear();
            }
            if (feature(parameters, "state.v1") && feature(parameters, "snapshot.v1")) {
              reconcile(parameters.session_id());
            }
          } else if (response.has_credits()) {
            if (response.credits().grant() == 0 || credits + response.credits().grant() > 1000000) {
              throw std::runtime_error("invalid credit grant");
            }
            credits += response.credits().grant();
          } else if (response.has_batch_ack()) {
            const auto& ack = response.batch_ack();
            if (!inflight || ack.batch_seq() != inflight->batch_seq()
                || static_cast<std::uint64_t>(ack.accepted()) + static_cast<std::uint64_t>(ack.rejections_size())
                       != static_cast<std::uint64_t>(inflight->events_size())) {
              throw std::runtime_error("acknowledgement does not cover the in-flight batch");
            }
            std::vector<std::string> ids;
            std::map<std::string, bool> membership;
            for (const auto& event : inflight->events()) {
              ids.push_back(event.event_id());
              membership.emplace(event.event_id(), false);
            }
            for (const auto& rejection : ack.rejections()) {
              const auto item = membership.find(rejection.event_id());
              if (item == membership.end() || item->second || !parse_error_code(rejection.code())) {
                throw std::runtime_error("invalid permanent event rejection");
              }
              item->second = true;
              if (callbacks_.rejection) {
                callbacks_.rejection(rejection);
              }
            }
            {
              const std::scoped_lock lock(mutex_);
              diagnostics_.acknowledged += ack.accepted();
              diagnostics_.rejected += static_cast<std::size_t>(ack.rejections_size());
            }
            store_.acknowledge(ids);
            inflight.reset();
            changed_.notify_all();
          } else if (response.has_command()) {
            command(response.command(), feature(parameters, "commands.v1") || parameters.protocol_version() == "1.0");
          } else if (response.has_config_update()) {
            if (response.config_update().config_version() > config_version_) {
              if (callbacks_.config) {
                callbacks_.config(response.config_update());
              }
              config_version_ = response.config_update().config_version();
            }
          } else if (response.has_goodbye()) {
            throw std::runtime_error("gateway ended connector session");
          } else if (!response.has_heartbeat()) {
            throw std::runtime_error("unexpected server frame");
          }
          deadline.store(
              (last_received + (registered ? duration(parameters.session_timeout()) : options_.registration_timeout))
                  .time_since_epoch()
                  .count());
        }
        if (ended.load()) {
          break;
        }
        const auto now = Clock::now();
        const auto timeout = registered ? duration(parameters.session_timeout()) : options_.registration_timeout;
        if (now - last_received > timeout) {
          throw std::runtime_error("connector session timed out");
        }
        if (inflight && now - batch_sent > options_.acknowledgement_timeout) {
          throw std::runtime_error("connector batch acknowledgement timed out");
        }
        if (!registered) {
          continue;
        }
        if (now - last_heartbeat >= duration(parameters.heartbeat_interval())) {
          if (!stream->Write(heartbeat())) {
            break;
          }
          last_heartbeat = now;
        }
        std::deque<wire::SessionRequest> outgoing;
        {
          const std::scoped_lock lock(mutex_);
          outgoing.swap(outgoing_);
        }
        changed_.notify_all();
        for (const auto& request : outgoing) {
          if (Clock::now() - last_heartbeat >= duration(parameters.heartbeat_interval())) {
            if (!stream->Write(heartbeat())) {
              throw std::runtime_error("connector heartbeat write failed");
            }
            last_heartbeat = Clock::now();
          }
          if (!stream->Write(request)) {
            throw std::runtime_error("connector control write failed");
          }
        }
        if (!inflight && credits != 0) {
          auto events = store_.pending(
              static_cast<std::size_t>(std::min<std::uint64_t>({credits, parameters.max_batch_events(), 32})),
              std::min<std::size_t>(parameters.max_batch_bytes(), 65536));
          if (events.empty()) {
            if (store_.count() != 0) {
              set_error("first pending event exceeds negotiated batch size");
            }
            continue;
          }
          wire::SessionRequest request;
          auto* batch = request.mutable_event_batch();
          batch->set_batch_seq(++batch_sequence);
          for (auto& event : events) {
            if (event.observation_session_id() != parameters.session_id()) {
              event.set_live_observation(false);
            }
            *batch->add_events() = std::move(event);
          }
          credits -= events.size();
          inflight = *batch;
          batch_sent = now;
          if (!stream->Write(request)) {
            break;
          }
        }
      }
    } catch (const std::exception& error) {
      set_error(error.what());
    }
    context.TryCancel();
    watchdog.request_stop();
    watchdog.join();
    reader.join();
    (void)stream->Finish();
    {
      const std::scoped_lock lock(mutex_);
      context_ = nullptr;
      session_.clear();
      state_enabled_ = false;
      outgoing_.clear();
    }
    changed_.notify_all();
  }

  void run(const std::stop_token& stop) {
    std::minstd_rand random(std::random_device{}());
    auto delay = options_.reconnect_min;
    while (!stop.stop_requested()) {
      const auto before = Clock::now();
      session(stop);
      if (Clock::now() - before > std::chrono::seconds(30)) {
        delay = options_.reconnect_min;
      }
      std::unique_lock lock(mutex_);
      const auto jitter = std::chrono::milliseconds(static_cast<std::int64_t>(random() % 100U));
      changed_.wait_for(lock, delay + jitter, [stop] { return stop.stop_requested(); });
      delay = std::min(delay * 2, options_.reconnect_max);
    }
  }

  Options options_;
  Callbacks callbacks_;
  detail::Store store_;
  std::shared_ptr<grpc::Channel> channel_;
  const std::string instance_ = detail::uuid7();
  std::uint64_t config_version_ = 0;
  mutable std::mutex mutex_;
  std::mutex lifecycle_;
  std::condition_variable changed_;
  std::jthread worker_, commands_;
  grpc::ClientContext* context_ = nullptr;
  std::deque<wire::SessionResponse> incoming_;
  std::deque<wire::SessionRequest> outgoing_;
  std::deque<std::function<void(std::stop_token)>> tasks_;
  std::string session_;
  bool state_enabled_ = false;
  Diagnostics diagnostics_;
};

Client::Client(Options options, Callbacks callbacks)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(callbacks))) {}

Client::~Client() = default;

Result<void> Client::start() {
  return impl_->start();
}

void Client::stop() {
  impl_->stop();
}

Result<wire::ConnectorEvent> Client::publish(wire::ConnectorEvent event, Observation observation) {
  return impl_->publish(std::move(event), observation);
}

Result<void> Client::report(wire::StateObservation observation) {
  return impl_->report(std::move(observation));
}

bool Client::flush(std::chrono::milliseconds timeout) {
  return impl_->flush(timeout);
}

Diagnostics Client::diagnostics() const {
  return impl_->diagnostics();
}

std::string Client::metrics() const {
  const auto data = diagnostics();
  std::ostringstream text;
  text << "psim_connector_connected " << data.connected << '\n'
       << "psim_connector_pending_events " << data.pending_events << '\n'
       << "psim_connector_buffer_bytes " << data.buffer_bytes << '\n'
       << "psim_connector_sessions_total " << data.sessions << '\n'
       << "psim_connector_acknowledged_events_total " << data.acknowledged << '\n'
       << "psim_connector_rejected_events_total " << data.rejected << '\n';
  return text.str();
}
}  // namespace psim::platform::connector
