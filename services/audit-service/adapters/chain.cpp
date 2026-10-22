#include "chain.hpp"

#include <fcntl.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/message.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/types.h>
#include <psim/audit/v1/audit.pb.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/stream/runtime.hpp"

namespace psim::audit_service {
namespace {
using Time = google::protobuf::util::TimeUtil;
using KeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

std::string checkpoint_payload(Checkpoint value) {
  value.clear_signature();
  return (std::string("psim.audit.checkpoint.v1") + '\0') + canonical(value);
}

std::string read_file(const std::filesystem::path& path) {
  if (std::filesystem::is_symlink(path) || std::filesystem::file_size(path) > 16384) {
    throw std::runtime_error("invalid external checkpoint file");
  }
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("external checkpoint unavailable");
  }
  return {std::istreambuf_iterator<char>(input), {}};
}

class Descriptor {
 public:
  explicit Descriptor(int value) : value_(value) {
    if (value_ < 0) {
      throw std::runtime_error("checkpoint archive unavailable");
    }
  }

  ~Descriptor() { (void)::close(value_); }

  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&&) = delete;
  Descriptor& operator=(Descriptor&&) = delete;

  int get() const { return value_; }

 private:
  int value_;
};
}  // namespace

std::string canonical(const google::protobuf::Message& value) {
  std::string result;
  {
    google::protobuf::io::StringOutputStream stream(&result);
    google::protobuf::io::CodedOutputStream coded(&stream);
    coded.SetSerializationDeterministic(true);
    if (!value.SerializeToCodedStream(&coded) || coded.HadError()) {
      throw std::runtime_error("canonical serialization failed");
    }
  }
  return result;
}

// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast): OpenSSL consumes unsigned byte buffers.
std::string digest(std::string_view value) {
  std::array<unsigned char, 32> bytes{};
  unsigned int size = 0;
  if (EVP_Digest(value.data(), value.size(), bytes.data(), &size, EVP_sha256(), nullptr) != 1 || size != 32) {
    throw std::runtime_error("SHA-256 failed");
  }
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string hex(std::string_view value) {
  std::string result;
  constexpr std::string_view kDigits = "0123456789abcdef";
  for (const char ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    result += kDigits[byte >> 4];
    result += kDigits[byte & 15];
  }
  return result;
}

std::string unhex(std::string_view value) {
  if (value.size() % 2 != 0
      || !std::ranges::all_of(value, [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); })) {
    throw std::invalid_argument("invalid hex");
  }
  std::string result;
  auto digit = [](char ch) {
    return ch <= '9' ? ch - '0' : ch - 'a' + 10;
  };
  for (std::size_t i = 0; i < value.size(); i += 2) {
    result += static_cast<char>((digit(value[i]) * 16) + digit(value[i + 1]));
  }
  return result;
}

std::string entry_hash(Entry value) {
  const auto previous = value.prev_hash();
  value.clear_hash();
  value.clear_prev_hash();
  return digest(previous + canonical(value));
}

Json json(const google::protobuf::Message& value) {
  return Json::parse(platform::stream::json(value));
}

bool matches(const Entry& value, const Json& query) {
  const auto& entry = value.entry();
  for (const auto& [name, actual] :
       std::map<std::string, std::string>{{"actorId", entry.actor().id()},
                                          {"objectType", entry.object_type()},
                                          {"objectId", entry.object_id()},
                                          {"siteId", entry.site_id()},
                                          {"outcome", audit::v1::AuditOutcome_Name(entry.outcome())}}) {
    if (query.contains(name) && query.at(name) != actual) {
      return false;
    }
  }
  if (!entry.action().starts_with(query.value("action", std::string{}))) {
    return false;
  }
  for (const auto* name : {"from", "to"}) {
    if (!query.contains(name)) {
      continue;
    }
    google::protobuf::Timestamp time;
    if (!Time::FromString(query.at(name).get<std::string>(), &time)) {
      throw std::invalid_argument("query time");
    }
    const auto actual = std::pair(value.occurred_at().seconds(), value.occurred_at().nanos());
    const auto bound = std::pair(time.seconds(), time.nanos());
    if ((std::string_view(name) == "from" && actual < bound) || (std::string_view(name) == "to" && actual > bound)) {
      return false;
    }
  }
  return true;
}

template <typename T>
T parse(const Json& value) {
  T result;
  if (!google::protobuf::util::JsonStringToMessage(value.dump(), &result).ok()) {
    throw std::invalid_argument("invalid protobuf JSON");
  }
  return result;
}

template Entry parse<Entry>(const Json&);
template Checkpoint parse<Checkpoint>(const Json&);

struct Signer::Key {
  explicit Key(EVP_PKEY* value) : value(value, EVP_PKEY_free) {
    if (!value || EVP_PKEY_is_a(value, "ED25519") != 1) {
      throw std::invalid_argument("Ed25519 key required");
    }
  }

  KeyPtr value;
};

Signer::Signer(std::string_view private_pem, const Json& trusted_keys) {
  auto load = [](std::string_view pem, bool private_key) {
    const std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())),
                                                        BIO_free);
    return std::make_shared<Key>(private_key
                                     ? PEM_read_bio_PrivateKey(
                                           bio.get(), nullptr, [](char*, int, int, void*) { return 0; }, nullptr)
                                     : PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr));
  };
  auto identity = [](const std::shared_ptr<Key>& key) {
    std::array<unsigned char, 32> raw{};
    std::size_t size = raw.size();
    if (EVP_PKEY_get_raw_public_key(key->value.get(), raw.data(), &size) != 1 || size != raw.size()) {
      throw std::invalid_argument("invalid public key");
    }
    return hex(digest({reinterpret_cast<const char*>(raw.data()), raw.size()}));
  };
  if (!trusted_keys.is_object() || trusted_keys.empty()) {
    throw std::invalid_argument("trusted signing keys required");
  }
  for (const auto& item : trusted_keys.items()) {
    auto key = load(item.value().get<std::string>(), false);
    if (identity(key) != item.key()) {
      throw std::invalid_argument("trusted key id mismatch");
    }
    trusted_.emplace(item.key(), std::move(key));
  }
  if (!private_pem.empty()) {
    private_ = load(private_pem, true);
    key_id_ = identity(private_);
    if (!trusted_.contains(key_id_)) {
      throw std::invalid_argument("signing key not trusted");
    }
  }
}

Checkpoint Signer::sign(Checkpoint value) const {
  if (!private_) {
    throw std::invalid_argument("private signing key required");
  }
  value.set_key_id(key_id_);
  value.clear_signature();
  const auto payload = checkpoint_payload(value);
  const Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  std::array<unsigned char, 64> signature{};
  std::size_t size = signature.size();
  if (!context || EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, private_->value.get()) != 1
      || EVP_DigestSign(context.get(), signature.data(), &size, reinterpret_cast<const unsigned char*>(payload.data()),
                        payload.size())
             != 1
      || size != signature.size()) {
    throw std::runtime_error("checkpoint signing failed");
  }
  value.set_signature(signature.data(), size);
  return value;
}

bool Signer::verify(const Checkpoint& value) const {
  const auto key = trusted_.find(value.key_id());
  if (key == trusted_.end() || value.signature().size() != 64 || value.hash().size() != 32 || value.audit_seq() == 0
      || !platform::stream::uuid(value.tenant_id()) || !Time::IsTimestampValid(value.created_at())) {
    return false;
  }
  const auto payload = checkpoint_payload(value);
  const Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  return context && EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key->second->value.get()) == 1
         && EVP_DigestVerify(context.get(), reinterpret_cast<const unsigned char*>(value.signature().data()),
                             value.signature().size(), reinterpret_cast<const unsigned char*>(payload.data()),
                             payload.size())
                == 1;
}

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

Verification verify(const std::vector<Entry>& entries, const std::vector<Checkpoint>& checkpoints, const Signer& signer,
                    std::string_view tenant) {
  Verification result;
  auto broken = [&result](std::uint64_t seq, std::string detail) {
    result.intact = false;
    result.broken = seq;
    result.detail = std::move(detail);
    return result;
  };
  std::string previous(32, '\0');
  std::uint64_t sequence = 1;
  for (const auto& value : entries) {
    if (value.audit_seq() != sequence || value.tenant_id() != tenant || value.prev_hash() != previous
        || value.hash() != entry_hash(value)) {
      return broken(sequence, "record sequence or hash mismatch");
    }
    previous = value.hash();
    ++sequence;
    ++result.checked;
  }
  for (const auto& value : checkpoints) {
    if (!signer.verify(value) || value.tenant_id() != tenant) {
      return broken(value.audit_seq(), "external checkpoint signature or tenant mismatch");
    }
    if (value.audit_seq() >= sequence) {
      return broken(sequence, "records missing below external checkpoint");
    }
    if (entries.at(value.audit_seq() - 1).hash() != value.hash()) {
      return broken(value.audit_seq(), "chain disagrees with external checkpoint");
    }
  }
  return result;
}

std::vector<std::string> Archive::tenants() const {
  std::vector<std::string> result;
  if (!std::filesystem::is_directory(directory_) || std::filesystem::is_symlink(directory_)) {
    throw std::runtime_error("external checkpoint directory required");
  }
  for (const auto& item : std::filesystem::directory_iterator(directory_)) {
    const auto name = item.path().filename().string();
    if (!platform::stream::uuid(name) || !item.is_directory() || item.is_symlink()) {
      throw std::runtime_error("invalid checkpoint tenant directory");
    }
    result.push_back(name);
  }
  return result;
}

std::vector<Checkpoint> Archive::read(std::string_view tenant) const {
  if (!platform::stream::uuid(tenant)) {
    throw std::invalid_argument("invalid tenant");
  }
  (void)tenants();
  std::vector<Checkpoint> result;
  const auto directory = std::filesystem::path(directory_) / tenant;
  if (!std::filesystem::exists(directory)) {
    return result;
  }
  if (std::filesystem::is_symlink(directory)) {
    throw std::runtime_error("invalid tenant directory");
  }
  for (const auto& item : std::filesystem::directory_iterator(directory)) {
    if (item.path().extension() != ".json") {
      continue;
    }
    auto value = parse<Checkpoint>(Json::parse(read_file(item.path())));
    if (value.tenant_id() != tenant || item.path().filename() != std::to_string(value.audit_seq()) + ".json") {
      throw std::runtime_error("checkpoint identity mismatch");
    }
    result.push_back(std::move(value));
  }
  std::ranges::sort(result, {}, &Checkpoint::audit_seq);
  return result;
}

void Archive::append(const Checkpoint& value) const {
  if (!platform::stream::uuid(value.tenant_id()) || value.audit_seq() == 0) {
    throw std::invalid_argument("invalid checkpoint identity");
  }
  (void)tenants();
  const auto directory = std::filesystem::path(directory_) / value.tenant_id();
  std::filesystem::create_directory(directory);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open has a variadic ABI.
  const Descriptor parent(::open(directory_.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (::fsync(parent.get()) != 0) {
    throw std::runtime_error("checkpoint parent sync failed");
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open has a variadic ABI.
  const Descriptor dir(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  const auto name = std::to_string(value.audit_seq()) + ".json";
  const auto content = json(value).dump() + '\n';
  if (std::filesystem::exists(directory / name)) {
    if (canonical(parse<Checkpoint>(Json::parse(read_file(directory / name)))) != canonical(value)) {
      throw std::runtime_error("conflicting external checkpoint");
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open has a variadic ABI.
    const Descriptor existing(::open((directory / name).c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (::fsync(existing.get()) != 0 || ::fsync(dir.get()) != 0) {
      throw std::runtime_error("checkpoint recovery sync failed");
    }
    return;
  }
  const auto temporary = ".pending-" + platform::stream::uuid7();
  const Descriptor file(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX openat requires a creation mode.
      ::openat(dir.get(), temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0440));
  std::size_t offset = 0;
  while (offset < content.size()) {
    const auto count = ::write(file.get(), std::string_view(content).substr(offset).data(), content.size() - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      throw std::runtime_error("checkpoint write failed");
    }
    offset += static_cast<std::size_t>(count);
  }
  if (::fsync(file.get()) != 0 || ::linkat(dir.get(), temporary.c_str(), dir.get(), name.c_str(), 0) != 0) {
    throw std::runtime_error("checkpoint publication failed");
  }
  if (::unlinkat(dir.get(), temporary.c_str(), 0) != 0 || ::fsync(dir.get()) != 0) {
    throw std::runtime_error("checkpoint directory sync failed");
  }
}
}  // namespace psim::audit_service
