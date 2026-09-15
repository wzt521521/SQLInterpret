#include "wzt/buffer.hpp"
#include "wzt/errors.hpp"
#include "wzt/file_manager.hpp"
#include "wzt/storage_manager.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

class TempDirectory {
public:
    TempDirectory() {
        path_ = fs::temp_directory_path() /
            ("minidb-storage-test-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        fs::create_directories(path_);
    }
    ~TempDirectory() { std::error_code ignored; fs::remove_all(path_, ignored); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("assertion failed: " + message);
}

template <typename Operation>
void expect_storage_error(const std::string& code, Operation operation) {
    try { operation(); }
    catch (const wzt::StorageError& error) {
        require(error.code() == code, "unexpected storage error " + error.code());
        return;
    }
    throw std::runtime_error("expected StorageError " + code);
}

void test_file_format_and_restart() {
    TempDirectory temporary;
    const auto path = temporary.path() / "pages.db";
    {
        wzt::FileManager file(path);
        require(file.allocate_page() == 0 && file.allocate_page() == 1 && file.allocate_page() == 2,
                "sequential allocation");
        file.write_page(0, wzt::bytes_from_string("alpha"));
        file.write_page(1, wzt::bytes_from_string("beta"));
        file.free_page(1);
    }
    {
        wzt::FileManager file(path);
        require(file.page_count() == 3, "page count survives restart");
        require(wzt::page_prefix_as_string(file.read_page(0)) == "alpha", "page data survives restart");
        expect_storage_error("PAGE_FREED", [&] { static_cast<void>(file.read_page(1)); });
        require(file.allocate_page() == 1, "lowest free page is reused");
        require(wzt::page_prefix_as_string(file.read_page(1)).empty(), "reused page is zeroed");
    }
}

std::vector<wzt::PageId> replacement(const fs::path& path, const std::string& policy) {
    wzt::FileManager file(path);
    for (int index = 0; index < 3; ++index) file.allocate_page();
    wzt::BufferPool pool(2, policy, file);
    static_cast<void>(pool.read_page(0));
    static_cast<void>(pool.read_page(1));
    static_cast<void>(pool.read_page(0));
    static_cast<void>(pool.read_page(2));
    require(pool.stats().cache_hits == 1 && pool.stats().cache_misses == 3, "cache statistics");
    return pool.resident_page_ids();
}

void test_replacement_dirty_and_pin() {
    TempDirectory temporary;
    require(replacement(temporary.path() / "lru.db", "LRU") == std::vector<wzt::PageId>({0, 2}),
            "LRU order");
    require(replacement(temporary.path() / "fifo.db", "FIFO") == std::vector<wzt::PageId>({1, 2}),
            "FIFO order");

    wzt::FileManager file(temporary.path() / "dirty.db");
    file.allocate_page();
    file.allocate_page();
    file.write_page(0, wzt::bytes_from_string("old"));
    wzt::BufferPool pool(1, "LRU", file);
    pool.write_page(0, wzt::bytes_from_string("changed"));
    static_cast<void>(pool.read_page(1));
    require(wzt::page_prefix_as_string(file.read_page(0)) == "changed", "dirty eviction writes back");
    static_cast<void>(pool.pin_page(1));
    expect_storage_error("NO_EVICTABLE_PAGE", [&] { static_cast<void>(pool.read_page(0)); });
    expect_storage_error("PAGE_PINNED", [&] { pool.free_page(1); });
    pool.unpin_page(1);
}

void test_facade_close_and_pressure() {
    TempDirectory temporary;
    const auto path = temporary.path() / "facade.db";
    {
        wzt::StorageManager storage(path, 2, "FIFO");
        for (wzt::PageId index = 0; index < 30; ++index) {
            require(storage.allocate_page() == index, "pressure allocation");
            storage.write_page(index, wzt::bytes_from_string("value-" + std::to_string(index)));
        }
        for (wzt::PageId index = 29; index >= 0; --index) {
            require(wzt::page_prefix_as_string(storage.read_page(index)) == "value-" + std::to_string(index),
                    "pressure read");
        }
        require(storage.stats().evictions > 0 && storage.stats().dirty_writes > 0, "pressure stats");
    }
    wzt::StorageManager reopened(path, 2, "LRU");
    require(wzt::page_prefix_as_string(reopened.read_page(29)) == "value-29", "facade restart");
    reopened.close();
    reopened.close();
    expect_storage_error("STORAGE_CLOSED", [&] { static_cast<void>(reopened.stats()); });
}

void test_boundaries_and_invalid_operations() {
    TempDirectory temporary;
    wzt::FileManager file(temporary.path() / "boundaries.db");
    const auto page_id = file.allocate_page();
    for (const std::size_t size : {0U, 1U, 4095U, 4096U}) {
        const wzt::Bytes expected(size, static_cast<wzt::Byte>('x'));
        file.write_page(page_id, expected);
        const auto actual = file.read_page(page_id);
        require(std::equal(expected.begin(), expected.end(), actual.begin()), "write boundary prefix");
        require(std::all_of(actual.begin() + static_cast<std::ptrdiff_t>(size), actual.end(),
                            [](wzt::Byte value) { return value == 0; }), "write boundary padding");
    }
    expect_storage_error("PAGE_DATA_TOO_LARGE", [&] { file.write_page(page_id, wzt::Bytes(4097, 1)); });
    expect_storage_error("INVALID_PAGE_ID", [&] { static_cast<void>(file.read_page(-1)); });
    expect_storage_error("PAGE_NOT_FOUND", [&] { static_cast<void>(file.read_page(99)); });
    file.free_page(page_id);
    expect_storage_error("PAGE_FREED", [&] { file.free_page(page_id); });
}

void test_corrupt_header_and_configuration() {
    TempDirectory temporary;
    const auto short_path = temporary.path() / "short.db";
    { std::ofstream output(short_path, std::ios::binary); output << "too short"; }
    expect_storage_error("CORRUPT_FILE", [&] { wzt::FileManager invalid(short_path); });

    const auto version_path = temporary.path() / "version.db";
    { wzt::FileManager valid(version_path); }
    {
        std::fstream file(version_path, std::ios::binary | std::ios::in | std::ios::out);
        const std::uint32_t version = wzt::FileManager::FORMAT_VERSION + 1;
        const char bytes[4]{
            static_cast<char>(version & 0xffU),
            static_cast<char>((version >> 8U) & 0xffU),
            static_cast<char>((version >> 16U) & 0xffU),
            static_cast<char>((version >> 24U) & 0xffU),
        };
        file.seekp(8);
        file.write(bytes, 4);
    }
    expect_storage_error("UNSUPPORTED_FORMAT", [&] { wzt::FileManager invalid(version_path); });

    wzt::FileManager file(temporary.path() / "config.db");
    expect_storage_error("INVALID_BUFFER_CAPACITY", [&] { wzt::BufferPool pool(0, "LRU", file); });
    expect_storage_error("UNKNOWN_REPLACEMENT_POLICY", [&] { wzt::BufferPool pool(1, "CLOCK", file); });
}

void test_explicit_flush() {
    TempDirectory temporary;
    const auto path = temporary.path() / "flush.db";
    wzt::StorageManager storage(path, 2);
    const auto first = storage.allocate_page();
    const auto second = storage.allocate_page();
    storage.write_page(first, wzt::bytes_from_string("first"));
    storage.write_page(second, wzt::bytes_from_string("second"));
    storage.flush_page(first);
    require(storage.stats().dirty_writes == 1, "single-page flush count");
    storage.flush_all();
    require(storage.stats().dirty_writes == 2, "all-page flush count");
    storage.flush_all();
    require(storage.stats().dirty_writes == 2, "clean pages are not rewritten");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"file format and restart", test_file_format_and_restart},
        {"replacement dirty and pin", test_replacement_dirty_and_pin},
        {"facade close and pressure", test_facade_close_and_pressure},
        {"boundaries and invalid operations", test_boundaries_and_invalid_operations},
        {"corrupt header and configuration", test_corrupt_header_and_configuration},
        {"explicit flush", test_explicit_flush},
    };
    for (const auto& test : tests) {
        try { test.second(); std::cout << "[PASS] " << test.first << '\n'; }
        catch (const std::exception& error) {
            std::cerr << "[FAIL] " << test.first << " -> " << error.what() << '\n';
            return 1;
        }
    }
    return 0;
}
