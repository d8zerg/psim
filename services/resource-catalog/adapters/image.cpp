#include <png.h>
#include <turbojpeg.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "file_store.hpp"
#include "model.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::resource_catalog {
namespace {
struct PngDeleter {
  void operator()(png_image* image) const { png_image_free(image); }
};

struct JpegDeleter {
  void operator()(void* handle) const { tj3Destroy(handle); }
};
}  // namespace

platform::Result<Image> validate_image(std::string_view type, std::string_view bytes) {
  using Code = platform::ErrorCode;
  if (bytes.empty() || bytes.size() > kMaxImageBytes) {
    return platform::fail(Code::kCommonInvalidArgument, "image exceeds size limit");
  }
  if (type == "image/png") {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    const std::unique_ptr<png_image, PngDeleter> release(&image);
    if (png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()) == 0 || image.width == 0
        || image.height == 0 || static_cast<std::uint64_t>(image.width) * image.height > kMaxImagePixels) {
      return platform::fail(Code::kCommonInvalidArgument, "invalid PNG or pixel limit exceeded");
    }
    image.format = PNG_FORMAT_RGBA;
    std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(image));
    if (png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr) == 0 || image.warning_or_error != 0) {
      return platform::fail(Code::kCommonInvalidArgument, "PNG decoding failed");
    }
    return Image{image.width, image.height};
  }
  if (type == "image/jpeg") {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): TurboJPEG requires an unsigned byte view.
    const auto* encoded = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::unique_ptr<void, JpegDeleter> handle(tj3Init(TJINIT_DECOMPRESS));
    // The maximum is checked by the decoder before coefficient/pixel allocations.
    if (!handle || tj3Set(handle.get(), TJPARAM_STOPONWARNING, 1) != 0
        || tj3Set(handle.get(), TJPARAM_MAXPIXELS, static_cast<int>(kMaxImagePixels)) != 0
        || tj3Set(handle.get(), TJPARAM_MAXMEMORY, 64) != 0
        || tj3DecompressHeader(handle.get(), encoded, bytes.size()) != 0) {
      return platform::fail(Code::kCommonInvalidArgument, "invalid JPEG or pixel limit exceeded");
    }
    const auto width = tj3Get(handle.get(), TJPARAM_JPEGWIDTH);
    const auto height = tj3Get(handle.get(), TJPARAM_JPEGHEIGHT);
    if (width <= 0 || height <= 0
        || static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > kMaxImagePixels) {
      return platform::fail(Code::kCommonInvalidArgument, "JPEG pixel limit exceeded");
    }
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    if (tj3Decompress8(handle.get(), encoded, bytes.size(), pixels.data(), 0, TJPF_RGBA) != 0) {
      return platform::fail(Code::kCommonInvalidArgument, "JPEG decoding failed");
    }
    return Image{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
  }
  return platform::fail(Code::kCommonInvalidArgument, "only PNG and JPEG are accepted");
}
}  // namespace psim::resource_catalog
