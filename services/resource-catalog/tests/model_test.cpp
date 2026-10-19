#include "model.hpp"

#include <gtest/gtest.h>
#include <png.h>
#include <pngconf.h>
#include <turbojpeg.h>

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "file_store.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::resource_catalog {
namespace {
constexpr const char* kId = "11111111-1111-4111-8111-111111111111";

TEST(CatalogModel, CanonicalJsonAndUnknownFields) {
  auto result =
      parse("location", {{"name", "Site"}, {"code", "site"}, {"kind", "LOCATION_KIND_SITE"}, {"timezone", "UTC"}});
  ASSERT_TRUE(result);
  EXPECT_TRUE(canonical_uuid(result->id));
  EXPECT_EQ(result->body.at("state"), "RESOURCE_STATE_ACTIVE");
  EXPECT_FALSE(parse("location", {{"unknown", true}}));
  EXPECT_FALSE(parse("device_type", {{"name", "Sensor"}, {"code", "s"}, {"capabilities", {{{"code", "UPPER"}}}}}));
}

TEST(CatalogModel, RejectsDuplicateKeysIncludingNestedObjects) {
  EXPECT_THROW((void)read_json(R"({"name":"a","name":"b"})"), std::invalid_argument);
  EXPECT_THROW((void)read_json(R"({"a":{"x":1,"x":2}})"), std::invalid_argument);
  EXPECT_NO_THROW((void)read_json(R"([{"name":"a"},{"name":"b"}])"));
}

TEST(CatalogModel, SourceIdentityMatchesGatewayAndRejectsDuplicates) {
  Json device{{"name", "Sensor"},
              {"deviceTypeId", kId},
              {"zoneId", kId},
              {"siteId", kId},
              {"connectorId", kId},
              {"externalId", "1"},
              {"sources", {{{"externalRef", "a"}, {"kind", "alarm"}}}}};
  auto parsed = parse("device", device);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->body["sources"][0]["sourceId"],
            p::stream::uuid5("728d47ed-1c69-5809-a2df-6e8b98e48ca6", {kId, "a"}));
  device["sources"].push_back(device["sources"][0]);
  EXPECT_FALSE(parse("device", device));
}

TEST(CatalogModel, ScopeFailsClosedAndWriteRequiresAdmin) {
  p::security::Principal principal{.subject = "user",
                                   .tenant = kId,
                                   .roles = {"operator"},
                                   .scopes = {kId},
                                   .expires_at = std::chrono::system_clock::now() + std::chrono::hours(1)};
  EXPECT_TRUE(allowed(principal, kId));
  EXPECT_FALSE(allowed(principal, kId, true));
  EXPECT_FALSE(allowed(principal, ""));
  principal.roles = {"admin"};
  EXPECT_TRUE(allowed(principal, kId, true));
  principal.scopes.clear();
  EXPECT_FALSE(allowed(principal, kId));
}

TEST(CatalogModel, CsvRoundTripAndRowFailures) {
  std::vector<Json> const rows{
      {{"name", "Sensor, \"A\"\nline"}, {"externalId", "a"}, {"sources", {{{"kind", "alarm"}, {"externalRef", "1"}}}}}};
  auto restored = import_rows("csv", export_rows("csv", rows));
  ASSERT_TRUE(restored);
  EXPECT_EQ(*restored, rows);
  EXPECT_FALSE(import_rows("csv", "name,name\r\nx,y\r\n"));
  EXPECT_FALSE(import_rows("csv", "name\n\"unfinished"));
  auto invalid = import_rows("csv", "name,externalId\nx\ny,z\n");
  ASSERT_TRUE(invalid);
  EXPECT_TRUE(invalid->at(0).is_null());
  EXPECT_EQ(invalid->at(1).at("name"), "y");
}

TEST(CatalogModel, MaintenanceBoundariesRestartAndProtectedTypes) {
  const auto now = std::chrono::system_clock::now();
  Json body{{"startsAt", timestamp(now + std::chrono::seconds(1))},
            {"endsAt", timestamp(now + std::chrono::seconds(2))},
            {"reason", "Test"},
            {"responsible", "user"},
            {"suppression", {{"eventTypes", {"access.denied"}}}},
            {"state", "MAINTENANCE_STATE_SCHEDULED"}};
  EXPECT_TRUE(validate_maintenance(body, now));
  auto before = maintenance_transition(body, now);
  ASSERT_TRUE(before);
  EXPECT_TRUE(before->empty());
  auto active = maintenance_transition(body, now + std::chrono::seconds(1));
  ASSERT_TRUE(active);
  EXPECT_EQ(*active, "activate");
  auto done = maintenance_transition(body, now + std::chrono::seconds(3));
  ASSERT_TRUE(done);
  EXPECT_EQ(*done, "complete");
  EXPECT_EQ(body.at("completedAt"), body.at("endsAt"));
  body["suppression"]["eventTypes"] = {"fire.alarm"};
  EXPECT_FALSE(validate_maintenance(body, now));
  body["suppression"]["eventTypes"] = {"*"};
  EXPECT_FALSE(validate_maintenance(body, now));
  body.erase("endsAt");
  EXPECT_FALSE(validate_maintenance(body, now));
}

std::string png() {
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = 2;
  image.height = 2;
  image.format = PNG_FORMAT_RGBA;
  const std::vector<unsigned char> pixels(16, 255);
  png_alloc_size_t size = 0;
  if (png_image_write_to_memory(&image, nullptr, &size, 0, pixels.data(), 0, nullptr) == 0) {
    return {};
  }
  std::string result(size, '\0');
  if (png_image_write_to_memory(&image, result.data(), &size, 0, pixels.data(), 0, nullptr) == 0) {
    return {};
  }
  result.resize(size);
  return result;
}

TEST(CatalogImages, DecodePngAndRejectTruncationSpoofAndOversize) {
  const auto bytes = png();
  ASSERT_FALSE(bytes.empty());
  auto valid = validate_image("image/png", bytes);
  ASSERT_TRUE(valid);
  EXPECT_EQ(valid->width, 2);
  EXPECT_EQ(valid->height, 2);
  EXPECT_FALSE(validate_image("image/jpeg", bytes));
  EXPECT_FALSE(validate_image("image/png", bytes.substr(0, bytes.size() / 2)));
  EXPECT_FALSE(validate_image("image/svg+xml", "<svg/>"));
  EXPECT_FALSE(validate_image("image/png", std::string(kMaxImageBytes + 1, 'x')));
}

TEST(CatalogImages, DecodeJpegAndRejectCorruptScan) {
  auto* handle = tj3Init(TJINIT_COMPRESS);
  ASSERT_NE(handle, nullptr);
  ASSERT_EQ(tj3Set(handle, TJPARAM_QUALITY, 90), 0);
  ASSERT_EQ(tj3Set(handle, TJPARAM_SUBSAMP, TJSAMP_444), 0);
  const std::vector<unsigned char> pixels(12, 255);
  unsigned char* encoded = nullptr;
  std::size_t size = 0;
  ASSERT_EQ(tj3Compress8(handle, pixels.data(), 2, 0, 2, TJPF_RGB, &encoded, &size), 0);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): copy the encoded TurboJPEG byte buffer.
  const std::string bytes(reinterpret_cast<const char*>(encoded), size);
  tj3Free(encoded);
  tj3Destroy(handle);
  auto valid = validate_image("image/jpeg", bytes);
  ASSERT_TRUE(valid);
  EXPECT_EQ(valid->width, 2);
  EXPECT_FALSE(validate_image("image/jpeg", bytes.substr(0, bytes.size() - 8)));
}
}  // namespace
}  // namespace psim::resource_catalog
