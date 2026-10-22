#pragma once
#include <nlohmann/json.hpp>
#include <psim/audit/v1/audit.pb.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace psim::audit_service {
using Json = nlohmann::json;
using Entry = audit::v1::AuditChainEntry;
using Checkpoint = audit::v1::AuditCheckpoint;

std::string canonical(const google::protobuf::Message& value);
std::string digest(std::string_view value);
std::string hex(std::string_view value);
std::string unhex(std::string_view value);
std::string entry_hash(Entry value);
Json json(const google::protobuf::Message& value);
bool matches(const Entry& value, const Json& query);
template <typename T>
T parse(const Json& value);

struct Verification {
  bool intact = true;
  std::uint64_t checked = 0;
  std::uint64_t broken = 0;
  std::string detail = "chain and external checkpoints agree";
};

class Signer {
 public:
  Signer(std::string_view private_pem, const Json& trusted_keys);
  Checkpoint sign(Checkpoint value) const;
  bool verify(const Checkpoint& value) const;

  const std::string& key_id() const { return key_id_; }

 private:
  struct Key;
  std::shared_ptr<Key> private_;
  std::map<std::string, std::shared_ptr<Key>> trusted_;
  std::string key_id_;
};

Verification verify(const std::vector<Entry>& entries, const std::vector<Checkpoint>& checkpoints, const Signer& signer,
                    std::string_view tenant);

/// An independently administered directory, never inside the PostgreSQL data volume.
class Archive {
 public:
  explicit Archive(std::string directory) : directory_(std::move(directory)) {}

  std::vector<Checkpoint> read(std::string_view tenant) const;
  std::vector<std::string> tenants() const;
  void append(const Checkpoint& value) const;

 private:
  std::string directory_;
};
}  // namespace psim::audit_service
