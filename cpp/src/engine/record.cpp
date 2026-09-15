#include "minidbms/engine/record.hpp"

#include "minidbms/engine/errors.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace minidbms {
namespace {

constexpr std::array<wzt::Byte, 8> ROW_MAGIC{'M', 'D', 'B', 'R', 'O', 'W', '0', '1'};
constexpr std::uint32_t ROW_VERSION = 1;
constexpr std::uint32_t DELETED = 1U << 31U;

std::uint32_t read_u32(const wzt::PageData& data, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(data[offset + index]) << (index * 8);
    }
    return value;
}

void write_u32(wzt::PageData& data, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        data[offset + index] = static_cast<wzt::Byte>((value >> (index * 8)) & 0xffU);
    }
}

void append_u32(wzt::Bytes& data, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        data.push_back(static_cast<wzt::Byte>((value >> (index * 8)) & 0xffU));
    }
}

void append_i64(wzt::Bytes& data, std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    for (std::size_t index = 0; index < 8; ++index) {
        data.push_back(static_cast<wzt::Byte>((bits >> (index * 8)) & 0xffULL));
    }
}

std::uint32_t read_u32(const wzt::Bytes& data, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(data[offset + index]) << (index * 8);
    }
    return value;
}

std::int64_t read_i64(const wzt::Bytes& data, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(data[offset + index]) << (index * 8);
    }
    std::int64_t result{};
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

bool valid_utf8(const std::string& text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        std::size_t length = 0;
        std::uint32_t value = 0;
        if (first <= 0x7f) { ++index; continue; }
        if ((first & 0xe0U) == 0xc0U) { length = 2; value = first & 0x1fU; }
        else if ((first & 0xf0U) == 0xe0U) { length = 3; value = first & 0x0fU; }
        else if ((first & 0xf8U) == 0xf0U) { length = 4; value = first & 0x07U; }
        else return false;
        if (index + length > text.size()) return false;
        for (std::size_t tail = 1; tail < length; ++tail) {
            const auto byte = static_cast<unsigned char>(text[index + tail]);
            if ((byte & 0xc0U) != 0x80U) return false;
            value = (value << 6U) | (byte & 0x3fU);
        }
        if ((length == 2 && value < 0x80) || (length == 3 && value < 0x800) ||
            (length == 4 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff)) return false;
        index += length;
    }
    return true;
}

}  // namespace

wzt::Bytes RecordCodec::encode(const minisql::TableSchema& schema,
                               const minisql::Row& values) const {
    if (values.size() != schema.columns.size()) {
        execution_error("VALUE_COUNT_MISMATCH", "record value count does not match schema");
    }
    wzt::Bytes output;
    for (std::size_t index = 0; index < schema.columns.size(); ++index) {
        const auto& column = schema.columns[index];
        const auto& value = values[index];
        if (column.data_type == minisql::DataType::Int) {
            if (!std::holds_alternative<std::int64_t>(value)) {
                execution_error("TYPE_MISMATCH", "column '" + column.name + "' requires INT");
            }
            append_i64(output, std::get<std::int64_t>(value));
        } else if (column.data_type == minisql::DataType::Varchar) {
            if (!std::holds_alternative<std::string>(value)) {
                execution_error("TYPE_MISMATCH", "column '" + column.name + "' requires VARCHAR");
            }
            const auto& text = std::get<std::string>(value);
            if (!valid_utf8(text)) execution_error("INVALID_STRING", "VARCHAR is not valid UTF-8");
            if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
                execution_error("RECORD_TOO_LARGE", "VARCHAR exceeds its length prefix");
            }
            append_u32(output, static_cast<std::uint32_t>(text.size()));
            output.insert(output.end(), text.begin(), text.end());
        } else {
            execution_error("UNSUPPORTED_TYPE", "table columns must be INT or VARCHAR");
        }
    }
    return output;
}

minisql::Row RecordCodec::decode(const minisql::TableSchema& schema,
                                 const wzt::Bytes& data) const {
    minisql::Row values;
    std::size_t offset = 0;
    for (const auto& column : schema.columns) {
        if (column.data_type == minisql::DataType::Int) {
            if (offset + 8 > data.size()) execution_error("CORRUPT_RECORD", "INT field is truncated");
            values.emplace_back(read_i64(data, offset));
            offset += 8;
        } else if (column.data_type == minisql::DataType::Varchar) {
            if (offset + 4 > data.size()) execution_error("CORRUPT_RECORD", "VARCHAR length is truncated");
            const auto length = read_u32(data, offset);
            offset += 4;
            if (offset + length > data.size()) execution_error("CORRUPT_RECORD", "VARCHAR payload is truncated");
            std::string text(data.begin() + static_cast<std::ptrdiff_t>(offset),
                             data.begin() + static_cast<std::ptrdiff_t>(offset + length));
            if (!valid_utf8(text)) execution_error("CORRUPT_RECORD", "VARCHAR contains invalid UTF-8");
            values.emplace_back(std::move(text));
            offset += length;
        } else {
            execution_error("UNSUPPORTED_TYPE", "record schema contains an unsupported type");
        }
    }
    if (offset != data.size()) execution_error("CORRUPT_RECORD", "record contains trailing bytes");
    return values;
}

RowPage::RowPage(const wzt::PageData& data) : data_(data) {
    if (!std::equal(ROW_MAGIC.begin(), ROW_MAGIC.end(), data_.begin()) || read_u32(data_, 8) != ROW_VERSION) {
        execution_error("CORRUPT_PAGE", "row page magic or version is invalid");
    }
    slot_count_ = read_u32(data_, 12);
    if (slot_count_ > (wzt::PAGE_SIZE - HEADER_SIZE) / SLOT_SIZE) {
        execution_error("CORRUPT_PAGE", "row page slot count is invalid");
    }
    const auto boundary = HEADER_SIZE + static_cast<std::size_t>(slot_count_) * SLOT_SIZE;
    std::vector<std::pair<std::size_t, std::size_t>> intervals;
    for (std::uint32_t slot = 0; slot < slot_count_; ++slot) {
        const auto entry = HEADER_SIZE + static_cast<std::size_t>(slot) * SLOT_SIZE;
        const auto offset = read_u32(data_, entry);
        const auto length = read_u32(data_, entry + 4) & ~DELETED;
        if (length == 0 || offset < boundary || static_cast<std::size_t>(offset) + length > wzt::PAGE_SIZE) {
            execution_error("CORRUPT_PAGE", "row page contains an invalid slot");
        }
        intervals.emplace_back(offset, static_cast<std::size_t>(offset) + length);
    }
    std::sort(intervals.begin(), intervals.end());
    for (std::size_t index = 1; index < intervals.size(); ++index) {
        if (intervals[index - 1].second > intervals[index].first) {
            execution_error("CORRUPT_PAGE", "row page records overlap");
        }
    }
    free_end_ = intervals.empty() ? wzt::PAGE_SIZE : intervals.front().first;
}

RowPage RowPage::empty() {
    wzt::PageData data{};
    std::copy(ROW_MAGIC.begin(), ROW_MAGIC.end(), data.begin());
    write_u32(data, 8, ROW_VERSION);
    write_u32(data, 12, 0);
    return RowPage(data);
}

bool RowPage::can_fit(std::size_t record_size) const {
    return HEADER_SIZE + (static_cast<std::size_t>(slot_count_) + 1) * SLOT_SIZE + record_size <= free_end_;
}

std::uint32_t RowPage::insert(const wzt::Bytes& record) {
    if (record.empty() || record.size() > MAX_RECORD_SIZE) {
        execution_error("RECORD_TOO_LARGE", "record cannot fit in a 4096-byte page");
    }
    if (!can_fit(record.size())) execution_error("PAGE_FULL", "row page has no space for this record");
    free_end_ -= record.size();
    std::copy(record.begin(), record.end(), data_.begin() + static_cast<std::ptrdiff_t>(free_end_));
    const auto slot_id = slot_count_;
    const auto entry = HEADER_SIZE + static_cast<std::size_t>(slot_id) * SLOT_SIZE;
    write_u32(data_, entry, static_cast<std::uint32_t>(free_end_));
    write_u32(data_, entry + 4, static_cast<std::uint32_t>(record.size()));
    ++slot_count_;
    write_u32(data_, 12, slot_count_);
    return slot_id;
}

std::vector<std::pair<std::uint32_t, wzt::Bytes>> RowPage::live_records() const {
    std::vector<std::pair<std::uint32_t, wzt::Bytes>> result;
    for (std::uint32_t slot = 0; slot < slot_count_; ++slot) {
        const auto entry = HEADER_SIZE + static_cast<std::size_t>(slot) * SLOT_SIZE;
        const auto offset = read_u32(data_, entry);
        const auto marked_length = read_u32(data_, entry + 4);
        if ((marked_length & DELETED) != 0) continue;
        result.emplace_back(slot, wzt::Bytes(
            data_.begin() + static_cast<std::ptrdiff_t>(offset),
            data_.begin() + static_cast<std::ptrdiff_t>(offset + marked_length)));
    }
    return result;
}

bool RowPage::erase(std::uint32_t slot_id) {
    if (slot_id >= slot_count_) execution_error("ROW_NOT_FOUND", "slot does not exist");
    const auto entry = HEADER_SIZE + static_cast<std::size_t>(slot_id) * SLOT_SIZE;
    const auto marked_length = read_u32(data_, entry + 4);
    if ((marked_length & DELETED) != 0) return false;
    write_u32(data_, entry + 4, marked_length | DELETED);
    return true;
}

}  // namespace minidbms
