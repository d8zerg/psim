#include "chain.hpp"

#include <google/protobuf/util/time_util.h>
#include <gtest/gtest.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/stream/runtime.hpp"

namespace {
namespace a = psim::audit_service;
using Time = google::protobuf::util::TimeUtil;
constexpr auto kTenant = "11111111-1111-4111-8111-111111111111";
constexpr auto kOther = "22222222-2222-4222-8222-222222222222";

struct Keys {
  std::string private_pem;
  a::Json public_keys;

  Keys() {
    const std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
        EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY* raw_key = nullptr;
    if (!context || EVP_PKEY_keygen_init(context.get()) != 1 || EVP_PKEY_keygen(context.get(), &raw_key) != 1) {
      throw std::runtime_error("key generation failed");
    }
    const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(raw_key, EVP_PKEY_free);
    const std::unique_ptr<BIO, decltype(&BIO_free)> private_bio(BIO_new(BIO_s_mem()), BIO_free);
    const std::unique_ptr<BIO, decltype(&BIO_free)> public_bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!key || PEM_write_bio_PrivateKey(private_bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1
        || PEM_write_bio_PUBKEY(public_bio.get(), key.get()) != 1) {
      throw std::runtime_error("key generation failed");
    }
    char const* buffer = nullptr;
    const auto private_size =
        BIO_get_mem_data(private_bio.get(), &buffer);  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast): OpenSSL macro.
    private_pem.assign(buffer, static_cast<std::size_t>(private_size));
    const auto public_size =
        BIO_get_mem_data(public_bio.get(), &buffer);  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast): OpenSSL macro.
    std::string public_pem(buffer, static_cast<std::size_t>(public_size));
    std::array<unsigned char, 32> raw{};
    std::size_t size = raw.size();
    if (EVP_PKEY_get_raw_public_key(key.get(), raw.data(), &size) != 1) {
      throw std::runtime_error("public key failed");
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): OpenSSL unsigned buffer.
    public_keys = {{a::hex(a::digest({reinterpret_cast<const char*>(raw.data()), size})), public_pem}};
  }
};

std::vector<a::Entry> chain() {
  std::vector<a::Entry> values;
  std::string previous(32, '\0');
  for (int i = 1; i <= 4; ++i) {
    a::Entry value;
    value.set_audit_seq(static_cast<std::uint64_t>(i));
    value.set_tenant_id(kTenant);
    value.set_source_event_id(psim::platform::stream::uuid7());
    value.mutable_entry()->set_action("incident.acknowledge");
    (*value.mutable_entry()->mutable_details())["z"] = "second";
    (*value.mutable_entry()->mutable_details())["a"] = "first";
    *value.mutable_occurred_at() = Time::GetCurrentTime();
    *value.mutable_recorded_at() = value.occurred_at();
    value.set_prev_hash(previous);
    value.set_hash(a::entry_hash(value));
    previous = value.hash();
    values.push_back(std::move(value));
  }
  return values;
}

a::Checkpoint point(const std::vector<a::Entry>& values, const a::Signer& signer) {
  a::Checkpoint result;
  result.set_tenant_id(kTenant);
  result.set_audit_seq(values.back().audit_seq());
  result.set_hash(values.back().hash());
  *result.mutable_created_at() = Time::GetCurrentTime();
  return signer.sign(result);
}

TEST(AuditChain, CanonicalMapsAndBinaryHash) {
  auto values = chain();
  auto changed = values[0];
  changed.mutable_entry()->clear_details();
  (*changed.mutable_entry()->mutable_details())["a"] = "first";
  (*changed.mutable_entry()->mutable_details())["z"] = "second";
  EXPECT_EQ(a::canonical(changed), a::canonical(values[0]));
  EXPECT_EQ(a::entry_hash(changed), values[0].hash());
  EXPECT_EQ(a::hex(a::digest("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(a::unhex(a::hex(std::string("a\0z", 3))), std::string("a\0z", 3));
  EXPECT_THROW(a::unhex("z0"), std::invalid_argument);
  EXPECT_THROW(a::unhex("0"), std::invalid_argument);
  EXPECT_THROW(a::parse<a::Entry>({{"unknown", true}}), std::invalid_argument);
}

TEST(AuditChain, MutationsDeletionInsertionAndTenantDetected) {
  const Keys keys;
  const a::Signer signer(keys.private_pem, keys.public_keys);
  const auto values = chain();
  const auto checkpoint = point(values, signer);
  EXPECT_TRUE(a::verify(values, {checkpoint}, signer, kTenant).intact);
  auto changed = values;
  changed[1].mutable_entry()->set_action("changed");
  EXPECT_EQ(a::verify(changed, {checkpoint}, signer, kTenant).broken, 2);
  changed = values;
  changed.erase(changed.begin() + 1);
  EXPECT_EQ(a::verify(changed, {checkpoint}, signer, kTenant).broken, 2);
  changed = values;
  changed.insert(changed.begin() + 1, values[0]);
  EXPECT_EQ(a::verify(changed, {checkpoint}, signer, kTenant).broken, 2);
  changed = values;
  changed.pop_back();
  EXPECT_EQ(a::verify(changed, {checkpoint}, signer, kTenant).broken, 4);
  EXPECT_FALSE(a::verify(values, {checkpoint}, signer, kOther).intact);
  changed = values;
  changed[0].mutable_entry()->set_action("rewritten");
  std::string previous(32, '\0');
  for (auto& value : changed) {
    value.set_prev_hash(previous);
    value.set_hash(a::entry_hash(value));
    previous = value.hash();
  }
  EXPECT_TRUE(a::verify(changed, {}, signer, kTenant).intact);
  EXPECT_EQ(a::verify(changed, {checkpoint}, signer, kTenant).broken, 4);
}

TEST(AuditChain, CheckpointSignatureBindsAllFieldsAndTrust) {
  const Keys keys;
  const Keys foreign;
  const a::Signer signer(keys.private_pem, keys.public_keys);
  const a::Signer verifier({}, keys.public_keys);
  const auto value = point(chain(), signer);
  EXPECT_TRUE(verifier.verify(value));
  for (int field = 0; field < 6; ++field) {
    auto changed = value;
    switch (field) {
      case 0:
        changed.set_tenant_id(kOther);
        break;
      case 1:
        changed.set_audit_seq(20);
        break;
      case 2:
        changed.set_hash(std::string(32, 'x'));
        break;
      case 3:
        changed.mutable_created_at()->set_seconds(0);
        break;
      case 4:
        changed.set_signature(std::string(64, 'x'));
        break;
      default:
        changed.set_key_id("unknown");
        break;
    }
    EXPECT_FALSE(verifier.verify(changed));
  }
  EXPECT_FALSE(a::Signer({}, foreign.public_keys).verify(value));
  EXPECT_THROW(a::Signer(foreign.private_pem, keys.public_keys), std::invalid_argument);
  EXPECT_THROW(a::Signer({}, a::Json::object()), std::invalid_argument);
  EXPECT_THROW(a::Signer("invalid", keys.public_keys), std::invalid_argument);
  EXPECT_THROW(verifier.sign(value), std::invalid_argument);
}

TEST(AuditChain, ExternalArchiveNeverOverwritesAndRejectsBadFiles) {
  const Keys keys;
  const a::Signer signer(keys.private_pem, keys.public_keys);
  const auto value = point(chain(), signer);
  const auto directory = std::filesystem::temp_directory_path() / psim::platform::stream::uuid7();
  std::filesystem::create_directory(directory);
  const a::Archive archive(directory.string());
  archive.append(value);
  archive.append(value);
  ASSERT_EQ(archive.read(kTenant).size(), 1);
  EXPECT_EQ(a::canonical(archive.read(kTenant)[0]), a::canonical(value));
  auto changed = value;
  changed.set_hash(std::string(32, 'x'));
  EXPECT_THROW(archive.append(changed), std::runtime_error);
  EXPECT_THROW(archive.read("../escape"), std::invalid_argument);
  std::filesystem::rename(directory / kTenant / "4.json", directory / kTenant / "5.json");
  EXPECT_THROW(archive.read(kTenant), std::runtime_error);
  std::filesystem::remove_all(directory);
  EXPECT_THROW(archive.tenants(), std::runtime_error);
}
}  // namespace
