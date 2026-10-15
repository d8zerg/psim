#include <boost/uuid/name_generator_sha1.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <google/protobuf/message.h>
#include <google/protobuf/util/json_util.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "psim/platform/stream/runtime.hpp"

namespace psim::platform::stream {
std::string uuid7() {
  static std::mutex mutex;
  static boost::uuids::random_generator random;
  static std::uint64_t previous = 0;
  static std::uint16_t sequence = 0;
  const std::scoped_lock lock(mutex);
  const auto now = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
  auto time = std::max(now, previous);
  if (time == previous) {
    if (++sequence == 4096) {
      ++time;
      sequence = 0;
    }
  } else {
    sequence = 0;
  }
  previous = time;
  auto value = random();
  for (std::size_t i = 0; i < 6; ++i) {
    value.data[i] = static_cast<unsigned char>(time >> ((5 - i) * 8));
  }
  value.data[6] = static_cast<unsigned char>(0x70U | (sequence >> 8U));
  value.data[7] = static_cast<unsigned char>(sequence & 255U);
  value.data[8] = static_cast<unsigned char>((value.data[8] & 63U) | 128U);
  return boost::uuids::to_string(value);
}

std::string uuid5(std::string_view space, const std::vector<std::string>& parts) {
  std::string name;
  for (const auto& part : parts) {
    auto size = part.size();
    while (size >= 128) {
      name.push_back(static_cast<char>((size & 127U) | 128U));
      size >>= 7U;
    }
    name.push_back(static_cast<char>(size));
    name += part;
  }
  return boost::uuids::to_string(
      boost::uuids::name_generator_sha1(boost::uuids::string_generator()(std::string(space)))(name));
}

bool uuid(std::string_view value) {
  try {
    return value.size() == 36 && boost::uuids::to_string(boost::uuids::string_generator()(std::string(value))) == value;
  } catch (const std::exception&) {
    return false;
  }
}

std::string sha256(std::string_view bytes) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int size = 0;
  if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &size, EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("SHA-256 failed");
  }
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string out;
  for (unsigned int i = 0; i < size; ++i) {
    out += kHex[digest[i] >> 4U];
    out += kHex[digest[i] & 15U];
  }
  return out;
}

std::string json(const google::protobuf::Message& message) {
  std::string result;
  google::protobuf::util::JsonPrintOptions options;
  options.always_print_fields_with_no_presence = true;
  if (!google::protobuf::util::MessageToJsonString(message, &result, options).ok()) {
    throw std::runtime_error("cannot encode protocol JSON");
  }
  return result;
}
}  // namespace psim::platform::stream
