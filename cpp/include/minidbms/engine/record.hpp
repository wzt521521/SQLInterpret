#pragma once

#include "minisql/common.h"
#include "wzt/page.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace minidbms {

class RecordCodec {
public:
    [[nodiscard]] wzt::Bytes encode(const minisql::TableSchema& schema,
                                    const minisql::Row& values) const;
    [[nodiscard]] minisql::Row decode(const minisql::TableSchema& schema,
                                      const wzt::Bytes& data) const;
};

class RowPage {
public:
    inline static constexpr std::size_t HEADER_SIZE = 16;
    inline static constexpr std::size_t SLOT_SIZE = 8;
    inline static constexpr std::size_t MAX_RECORD_SIZE = wzt::PAGE_SIZE - HEADER_SIZE - SLOT_SIZE;

    explicit RowPage(const wzt::PageData& data);
    [[nodiscard]] static RowPage empty();
    [[nodiscard]] bool can_fit(std::size_t record_size) const;
    std::uint32_t insert(const wzt::Bytes& record);
    [[nodiscard]] std::vector<std::pair<std::uint32_t, wzt::Bytes>> live_records() const;
    bool erase(std::uint32_t slot_id);
    [[nodiscard]] const wzt::PageData& data() const noexcept { return data_; }

private:
    wzt::PageData data_{};
    std::uint32_t slot_count_{0};
    std::size_t free_end_{wzt::PAGE_SIZE};
};

}  // namespace minidbms
