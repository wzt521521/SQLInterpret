#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wzt {

inline constexpr std::size_t PAGE_SIZE = 4096;
using Byte = std::uint8_t;
using PageId = std::int64_t;
using Bytes = std::vector<Byte>;
using PageData = std::array<Byte, PAGE_SIZE>;

struct PageFrame {
    PageId page_id{};
    PageData data{};
    bool dirty{false};
    std::size_t pin_count{0};
};

struct BufferStats {
    std::uint64_t read_requests{0};
    std::uint64_t cache_hits{0};
    std::uint64_t cache_misses{0};
    std::uint64_t evictions{0};
    std::uint64_t dirty_writes{0};
};

Bytes bytes_from_string(const std::string& text);
std::string page_prefix_as_string(const PageData& page);
std::string stats_to_string(const BufferStats& stats);

}  // namespace wzt
