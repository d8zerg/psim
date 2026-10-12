#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace psim::platform::security::detail {
inline const unsigned char* octets(std::string_view bytes) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): OpenSSL byte API
  return reinterpret_cast<const unsigned char*>(bytes.data());
}

inline unsigned char* writable_octets(std::string& bytes) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): OpenSSL byte API
  return reinterpret_cast<unsigned char*>(bytes.data());
}

inline std::string_view native_text(const unsigned char* bytes, std::size_t size) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): ASN.1 string byte API
  return {reinterpret_cast<const char*>(bytes), size};
}
}  // namespace psim::platform::security::detail
