#include "state_store.hpp"

#include <rocksdb/cache.h>
#include <rocksdb/compression_type.h>
#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/status.h>
#include <rocksdb/table.h>
#include <rocksdb/write_batch.h>
#include <rocksdb/write_buffer_manager.h>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::correlation::live {
namespace {
p::Result<void> check(const rocksdb::Status& status) {
  return status.ok() ? p::Result<void>{} : p::fail(p::ErrorCode::kCommonInternal, "correlation state store failed");
}

std::string family(const std::string& key) {
  const auto first = key.find('/');
  const auto second = first == std::string::npos ? first : key.find('/', first + 1);
  return second == std::string::npos ? rocksdb::kDefaultColumnFamilyName : key.substr(0, second);
}
}  // namespace

struct StateStore::Impl {
  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
  std::unique_ptr<rocksdb::DB> database{};
  std::map<std::string, rocksdb::ColumnFamilyHandle*> families{};
  rocksdb::ColumnFamilyOptions options{};

  ~Impl() {
    for (const auto& [name, handle] : families) {
      (void)database->DestroyColumnFamilyHandle(handle);
    }
  }

  p::Result<rocksdb::ColumnFamilyHandle*> writable(const std::string& name) {
    const auto found = families.find(name);
    if (found != families.end()) {
      return found->second;
    }
    auto destroy = [this](rocksdb::ColumnFamilyHandle* value) {
      (void)database->DestroyColumnFamilyHandle(value);
    };
    std::unique_ptr<rocksdb::ColumnFamilyHandle, decltype(destroy)> handle(nullptr, destroy);
    if (auto result = check(database->CreateColumnFamily(options, name, std::out_ptr(handle))); !result) {
      return std::unexpected(result.error());
    }
    families.emplace(name, handle.get());
    return handle.release();
  }
};

StateStore::StateStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

StateStore::~StateStore() = default;

p::Result<std::unique_ptr<StateStore>> StateStore::open(const std::string& directory) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    return p::fail(p::ErrorCode::kCommonInvalidArgument, "cannot create correlation state directory");
  }
  rocksdb::Options options;
  options.create_if_missing = true;
  options.create_missing_column_families = true;
  options.max_open_files = 64;
  options.max_background_jobs = 2;
  options.write_buffer_size = std::size_t{4194304};
  options.max_write_buffer_number = 2;
  options.compression = rocksdb::kZSTD;
  static const auto kCache = rocksdb::NewLRUCache(67108864);
  static const auto kBuffers = std::make_shared<rocksdb::WriteBufferManager>(67108864, kCache);
  options.write_buffer_manager = kBuffers;
  rocksdb::BlockBasedTableOptions table;
  table.block_cache = kCache;
  options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table));
  auto impl = std::make_unique<Impl>();
  impl->options = rocksdb::ColumnFamilyOptions(options);
  std::vector<std::string> names;
  if (std::filesystem::exists(std::filesystem::path(directory) / "CURRENT")) {
    if (auto checked = check(rocksdb::DB::ListColumnFamilies(options, directory, &names)); !checked) {
      return std::unexpected(checked.error());
    }
  } else {
    names.push_back(rocksdb::kDefaultColumnFamilyName);
  }
  std::vector<rocksdb::ColumnFamilyDescriptor> descriptors;
  descriptors.reserve(names.size());
  for (const auto& name : names) {
    descriptors.emplace_back(name, impl->options);
  }
  std::vector<rocksdb::ColumnFamilyHandle*> handles;
  rocksdb::DB* database = nullptr;
  auto opened = rocksdb::DB::Open(options, directory, descriptors, &handles, &database);
  if (!opened.ok()) {
    return p::fail(p::ErrorCode::kCommonInternal, "cannot open correlation state store");
  }
  impl->database.reset(database);
  for (std::size_t index = 0; index < names.size(); ++index) {
    impl->families.emplace(names[index], handles[index]);
  }
  return std::unique_ptr<StateStore>(new StateStore(std::move(impl)));
}

p::Result<std::optional<std::string>> StateStore::get(const std::string& key) const {
  const auto found = impl_->families.find(family(key));
  if (found == impl_->families.end()) {
    return std::optional<std::string>{};
  }
  std::string bytes;
  const auto status = impl_->database->Get(rocksdb::ReadOptions{}, found->second, key, &bytes);
  if (status.IsNotFound()) {
    return std::optional<std::string>{};
  }
  if (auto result = check(status); !result) {
    return std::unexpected(result.error());
  }
  return std::optional<std::string>(std::move(bytes));
}

p::Result<std::map<std::string, std::string>> StateStore::entries() const {
  return page("", std::numeric_limits<std::size_t>::max());
}

p::Result<std::map<std::string, std::string>> StateStore::page(const std::string& after, std::size_t limit) const {
  std::map<std::string, std::string> values;
  for (const auto& [name, handle] : impl_->families) {
    if (name == rocksdb::kDefaultColumnFamilyName) {
      continue;
    }
    std::unique_ptr<rocksdb::Iterator> iterator(impl_->database->NewIterator(rocksdb::ReadOptions{}, handle));
    for (iterator->Seek(after); iterator->Valid() && values.size() < limit; iterator->Next()) {
      if (iterator->key().ToString() <= after) {
        continue;
      }
      values.emplace(iterator->key().ToString(), iterator->value().ToString());
    }
    if (auto checked = check(iterator->status()); !checked) {
      return std::unexpected(checked.error());
    }
  }
  return values;
}

p::Result<void> StateStore::apply(const Changes& changes, std::int64_t next_offset) {
  rocksdb::WriteBatch batch;
  for (const auto& [key, value] : changes) {
    auto handle = impl_->writable(family(key));
    if (!handle) {
      return std::unexpected(handle.error());
    }
    if (value) {
      batch.Put(*handle, key, *value);
    } else {
      batch.Delete(*handle, key);
    }
  }
  batch.Put(impl_->families.at(rocksdb::kDefaultColumnFamilyName), "checkpoint", std::to_string(next_offset));
  const auto now =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count();
  batch.Put(impl_->families.at(rocksdb::kDefaultColumnFamilyName), "checkpoint_time", std::to_string(now));
  rocksdb::WriteOptions options;
  options.sync = true;
  return check(impl_->database->Write(options, &batch));
}

p::Result<std::int64_t> StateStore::checkpoint() const {
  auto time = get("checkpoint_time");
  if (!time) {
    return std::unexpected(time.error());
  }
  if (!*time) {
    return std::int64_t{0};
  }
  std::int64_t checked_at = 0;
  const auto& time_value = **time;
  const auto parsed_time = std::from_chars(time_value.data(), std::to_address(time_value.end()), checked_at);
  const auto now =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count();
  if (parsed_time.ec != std::errc{} || parsed_time.ptr != std::to_address(time_value.end()) || checked_at < 0
      || now < checked_at || now - checked_at > 3600000) {
    return std::int64_t{0};
  }
  auto bytes = get("checkpoint");
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  if (!*bytes) {
    return std::int64_t{0};
  }
  std::int64_t offset = 0;
  const auto& value = **bytes;
  const auto parsed = std::from_chars(value.data(), std::to_address(value.end()), offset);
  if (parsed.ec != std::errc{} || parsed.ptr != std::to_address(value.end()) || offset < 0) {
    return p::fail(p::ErrorCode::kCommonInternal, "invalid local changelog checkpoint");
  }
  return offset;
}

p::Result<void> StateStore::clear() {
  for (auto it = impl_->families.begin(); it != impl_->families.end();) {
    if (it->first == rocksdb::kDefaultColumnFamilyName) {
      ++it;
      continue;
    }
    if (auto dropped = check(impl_->database->DropColumnFamily(it->second)); !dropped) {
      return dropped;
    }
    if (auto released = check(impl_->database->DestroyColumnFamilyHandle(it->second)); !released) {
      return released;
    }
    it = impl_->families.erase(it);
  }
  return apply({}, 0);
}
}  // namespace psim::correlation::live
