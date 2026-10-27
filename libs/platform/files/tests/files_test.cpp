#include "psim/platform/files.hpp"

#include <gtest/gtest.h>
#include <stdlib.h>  // NOLINT(modernize-deprecated-headers): POSIX mkdtemp.

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

namespace psim::platform::files {
namespace {
class Files : public ::testing::Test {
 public:
  Files(const Files&) = delete;
  Files& operator=(const Files&) = delete;
  Files(Files&&) = delete;
  Files& operator=(Files&&) = delete;

  ~Files() override {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
  }

 protected:
  Files() {
    auto pattern = (std::filesystem::temp_directory_path() / "psim-files-XXXXXX").string();
    const auto* created = ::mkdtemp(pattern.data());
    if (created == nullptr) {
      throw std::runtime_error("cannot create test directory");
    }
    directory_ = created;
  }

  [[nodiscard]] std::filesystem::path path(const std::string& name) const { return directory_ / name; }

 private:
  std::filesystem::path directory_;
};

TEST_F(Files, JournalAppendPersistsRecordsAndEnforcesReadLimit) {
  ASSERT_TRUE(append(path("journal"), "first\n"));
  ASSERT_TRUE(append(path("journal"), "second\n"));
  const auto value = read(path("journal"));
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, "first\nsecond\n");
  EXPECT_FALSE(read(path("journal"), 3));
  EXPECT_EQ(std::filesystem::status(path("journal")).permissions(),
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}

TEST_F(Files, SymlinksAndDirectoriesCannotBeReadOrAppendedAsFiles) {
  ASSERT_TRUE(append(path("original"), "original"));
  std::filesystem::create_symlink(path("original"), path("link"));
  EXPECT_FALSE(read(path("link")));
  EXPECT_FALSE(append(path("link"), "replaced"));
  EXPECT_FALSE(read(path(".")));
  EXPECT_FALSE(append(path("."), "invalid"));
  const auto original = read(path("original"));
  ASSERT_TRUE(original);
  EXPECT_EQ(*original, "original");
}
}  // namespace
}  // namespace psim::platform::files
