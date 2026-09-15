#include "minidbms/engine/catalog_manager.hpp"

#include "minidbms/engine/errors.hpp"
#include "minisql/format.h"
#include "wzt/errors.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace minidbms {
namespace {

constexpr std::array<wzt::Byte, 8> ROOT_MAGIC{'M', 'D', 'B', 'C', 'A', 'T', '0', '1'};
constexpr std::array<wzt::Byte, 8> CHUNK_MAGIC{'M', 'D', 'B', 'C', 'T', 'C', 'H', '1'};
constexpr std::uint32_t FORMAT_VERSION = 1;
constexpr std::uint32_t NO_PAGE = 0xffffffffU;
constexpr std::size_t ROOT_HEADER_SIZE = 24;
constexpr std::size_t CHUNK_HEADER_SIZE = 16;

std::uint32_t read_u32(const wzt::PageData& data, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(data[offset + index]) << (index * 8);
    }
    return value;
}

void append_u32(wzt::Bytes& data, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        data.push_back(static_cast<wzt::Byte>((value >> (index * 8)) & 0xffU));
    }
}

std::uint32_t crc32(std::string_view data) {
    std::uint32_t value = 0xffffffffU;
    for (const unsigned char byte : data) {
        value ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value >> 1U) ^ (0xedb88320U & (0U - (value & 1U)));
        }
    }
    return value ^ 0xffffffffU;
}

void append_utf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7f) output.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    }
}

struct ParsedTable {
    std::string name;
    std::vector<std::pair<std::string, std::string>> columns;
    std::vector<std::uint32_t> pages;
};

class CatalogParser {
public:
    explicit CatalogParser(std::string_view input) : input_(input) {}

    std::vector<ParsedTable> parse() {
        expect('{');
        require_key("tables");
        expect(':');
        expect('[');
        std::vector<ParsedTable> tables;
        if (!take(']')) {
            do { tables.push_back(parse_table()); } while (take(','));
            expect(']');
        }
        expect('}');
        whitespace();
        if (position_ != input_.size()) fail("unexpected trailing JSON");
        return tables;
    }

private:
    std::string_view input_;
    std::size_t position_{0};

    ParsedTable parse_table() {
        ParsedTable table;
        expect('{');
        require_key("name");
        expect(':');
        table.name = string();
        expect(',');
        require_key("columns");
        expect(':');
        expect('[');
        if (!take(']')) {
            do {
                expect('[');
                const auto name = string();
                expect(',');
                const auto type = string();
                expect(']');
                table.columns.emplace_back(name, type);
            } while (take(','));
            expect(']');
        }
        expect(',');
        require_key("pages");
        expect(':');
        expect('[');
        if (!take(']')) {
            do { table.pages.push_back(number()); } while (take(','));
            expect(']');
        }
        expect('}');
        return table;
    }

    void whitespace() {
        while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) {
            ++position_;
        }
    }

    bool take(char expected) {
        whitespace();
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!take(expected)) fail(std::string("expected '") + expected + "'");
    }

    void require_key(const std::string& key) {
        if (string() != key) fail("unexpected object key");
    }

    static int hex(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    std::uint32_t unicode_escape() {
        if (position_ + 4 > input_.size()) fail("truncated unicode escape");
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index) {
            const auto digit = hex(input_[position_++]);
            if (digit < 0) fail("invalid unicode escape");
            value = (value << 4U) | static_cast<std::uint32_t>(digit);
        }
        return value;
    }

    std::string string() {
        whitespace();
        if (position_ >= input_.size() || input_[position_++] != '"') fail("expected string");
        std::string output;
        while (position_ < input_.size()) {
            const auto value = static_cast<unsigned char>(input_[position_++]);
            if (value == '"') return output;
            if (value < 0x20) fail("control byte in string");
            if (value != '\\') {
                output.push_back(static_cast<char>(value));
                continue;
            }
            if (position_ >= input_.size()) fail("truncated escape");
            const char escape = input_[position_++];
            if (escape == '"' || escape == '\\' || escape == '/') output.push_back(escape);
            else if (escape == 'b') output.push_back('\b');
            else if (escape == 'f') output.push_back('\f');
            else if (escape == 'n') output.push_back('\n');
            else if (escape == 'r') output.push_back('\r');
            else if (escape == 't') output.push_back('\t');
            else if (escape == 'u') {
                auto codepoint = unicode_escape();
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (position_ + 2 > input_.size() || input_[position_] != '\\' || input_[position_ + 1] != 'u') {
                        fail("missing low surrogate");
                    }
                    position_ += 2;
                    const auto low = unicode_escape();
                    if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
                    codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U) + (low - 0xdc00U);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    fail("unexpected low surrogate");
                }
                append_utf8(output, codepoint);
            } else fail("invalid string escape");
        }
        fail("unterminated string");
    }

    std::uint32_t number() {
        whitespace();
        if (position_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
            fail("expected unsigned integer");
        }
        std::uint64_t value = 0;
        while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
            value = value * 10 + static_cast<unsigned>(input_[position_++] - '0');
            if (value > std::numeric_limits<std::uint32_t>::max()) fail("integer is too large");
        }
        return static_cast<std::uint32_t>(value);
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(message + " at byte " + std::to_string(position_));
    }
};

std::string serialize_catalog(const std::map<std::string, TableInfo>& tables) {
    std::string output = "{\"tables\":[";
    bool first_table = true;
    for (const auto& entry : tables) {
        if (!first_table) output += ',';
        first_table = false;
        output += "{\"name\":" + minisql::json_quote(entry.first) + ",\"columns\":[";
        bool first_column = true;
        for (const auto& column : entry.second.schema.columns) {
            if (!first_column) output += ',';
            first_column = false;
            output += "[" + minisql::json_quote(column.name) + "," +
                      minisql::json_quote(minisql::type_name(column.data_type)) + "]";
        }
        output += "],\"pages\":[";
        for (std::size_t index = 0; index < entry.second.pages.size(); ++index) {
            if (index != 0) output += ',';
            output += std::to_string(entry.second.pages[index]);
        }
        output += "]}";
    }
    return output + "]}";
}

wzt::Bytes root_page(std::uint32_t first, std::uint32_t size, std::uint32_t checksum) {
    wzt::Bytes page;
    page.insert(page.end(), ROOT_MAGIC.begin(), ROOT_MAGIC.end());
    append_u32(page, FORMAT_VERSION);
    append_u32(page, first);
    append_u32(page, size);
    append_u32(page, checksum);
    return page;
}

}  // namespace

CatalogManager::CatalogManager(wzt::StorageManager& storage) : storage_(storage) {
    try {
        load(storage_.read_page(0));
    } catch (const wzt::StorageError& error) {
        if (error.code() != "PAGE_NOT_FOUND") throw;
        if (storage_.allocate_page() != 0) {
            execution_error("CATALOG_ROOT_MISSING", "new database did not allocate catalog page zero");
        }
        storage_.write_page(0, root_page(NO_PAGE, 0, 0));
        storage_.flush_page(0);
    }
}

bool CatalogManager::table_exists(const std::string& table_name) const {
    return tables_.find(minisql::lower_ascii(table_name)) != tables_.end();
}

minisql::TableSchema CatalogManager::get_schema(const std::string& table_name) const {
    return info(table_name).schema;
}

std::vector<wzt::PageId> CatalogManager::page_ids(const std::string& table_name) const {
    return info(table_name).pages;
}

std::vector<TableInfo> CatalogManager::list_tables() const {
    std::vector<TableInfo> result;
    for (const auto& entry : tables_) result.push_back(entry.second);
    return result;
}

void CatalogManager::create_table(const minisql::TableSchema& schema) {
    const auto name = minisql::lower_ascii(schema.table_name);
    if (table_exists(name)) execution_error("TABLE_EXISTS", "table '" + name + "' already exists");
    if (schema.columns.empty()) execution_error("EMPTY_SCHEMA", "a table needs at least one column");
    minisql::TableSchema normalized{name, {}};
    std::set<std::string> names;
    for (const auto& column : schema.columns) {
        const auto column_name = minisql::lower_ascii(column.name);
        if (!names.insert(column_name).second) execution_error("DUPLICATE_COLUMN", "duplicate column");
        if (column.data_type != minisql::DataType::Int && column.data_type != minisql::DataType::Varchar) {
            execution_error("UNSUPPORTED_TYPE", "table columns must be INT or VARCHAR");
        }
        normalized.columns.push_back({column_name, column.data_type});
    }
    auto updated = tables_;
    updated.emplace(name, TableInfo{normalized, {}});
    persist(updated);
}

void CatalogManager::add_page(const std::string& table_name, wzt::PageId page_id) {
    const auto name = minisql::lower_ascii(table_name);
    const auto& existing = info(name);
    if (page_id <= 0 || std::find(snapshot_pages_.begin(), snapshot_pages_.end(), page_id) != snapshot_pages_.end()) {
        execution_error("INVALID_DATA_PAGE", "page cannot be assigned to table");
    }
    for (const auto& entry : tables_) {
        if (std::find(entry.second.pages.begin(), entry.second.pages.end(), page_id) != entry.second.pages.end()) {
            execution_error("INVALID_DATA_PAGE", "page is already assigned to a table");
        }
    }
    auto updated = tables_;
    updated[name] = existing;
    updated[name].pages.push_back(page_id);
    persist(updated);
}

const TableInfo& CatalogManager::info(const std::string& table_name) const {
    const auto name = minisql::lower_ascii(table_name);
    const auto found = tables_.find(name);
    if (found == tables_.end()) execution_error("TABLE_NOT_FOUND", "table '" + name + "' does not exist");
    return found->second;
}

void CatalogManager::load(const wzt::PageData& root) {
    if (!std::equal(ROOT_MAGIC.begin(), ROOT_MAGIC.end(), root.begin()) || read_u32(root, 8) != FORMAT_VERSION) {
        execution_error("CORRUPT_CATALOG", "catalog root magic or version is invalid");
    }
    const auto first = read_u32(root, 12);
    const auto size = read_u32(root, 16);
    const auto checksum = read_u32(root, 20);
    if (size == 0) {
        if (first != NO_PAGE || checksum != 0) execution_error("CORRUPT_CATALOG", "empty catalog root is invalid");
        return;
    }
    if (first == 0 || first == NO_PAGE) execution_error("CORRUPT_CATALOG", "catalog snapshot pointer is invalid");
    auto snapshot = read_snapshot(first, size);
    if (crc32(snapshot.first) != checksum) execution_error("CORRUPT_CATALOG", "catalog checksum does not match");

    std::map<std::string, TableInfo> tables;
    std::set<wzt::PageId> used_pages{0};
    used_pages.insert(snapshot.second.begin(), snapshot.second.end());
    try {
        for (const auto& parsed : CatalogParser(snapshot.first).parse()) {
            if (parsed.name.empty() || parsed.name != minisql::lower_ascii(parsed.name) || tables.count(parsed.name) != 0) {
                throw std::runtime_error("duplicate or invalid table name");
            }
            if (parsed.columns.empty()) throw std::runtime_error("table has no columns");
            minisql::TableSchema schema{parsed.name, {}};
            std::set<std::string> column_names;
            for (const auto& column : parsed.columns) {
                minisql::DataType type = minisql::DataType::Unknown;
                if (column.second == "INT") type = minisql::DataType::Int;
                else if (column.second == "VARCHAR") type = minisql::DataType::Varchar;
                if (column.first.empty() || column.first != minisql::lower_ascii(column.first) ||
                    !column_names.insert(column.first).second || type == minisql::DataType::Unknown) {
                    throw std::runtime_error("invalid column metadata");
                }
                schema.columns.push_back({column.first, type});
            }
            std::vector<wzt::PageId> pages;
            for (const auto page : parsed.pages) {
                if (page == 0 || !used_pages.insert(page).second) throw std::runtime_error("duplicate or invalid data page");
                pages.push_back(page);
            }
            tables.emplace(parsed.name, TableInfo{std::move(schema), std::move(pages)});
        }
    } catch (const std::exception& error) {
        execution_error("CORRUPT_CATALOG", std::string("catalog metadata is invalid: ") + error.what());
    }
    tables_ = std::move(tables);
    snapshot_pages_ = std::move(snapshot.second);
}

std::pair<std::string, std::vector<wzt::PageId>>
CatalogManager::read_snapshot(std::uint32_t first, std::uint32_t size) {
    std::uint32_t remaining = size;
    std::string raw;
    raw.reserve(size);
    std::vector<wzt::PageId> pages;
    std::uint32_t page_id = first;
    while (remaining != 0) {
        if (page_id == 0 || page_id == NO_PAGE ||
            std::find(pages.begin(), pages.end(), page_id) != pages.end()) {
            execution_error("CORRUPT_CATALOG", "catalog chain has a cycle or invalid page");
        }
        pages.push_back(page_id);
        wzt::PageData page{};
        try { page = storage_.read_page(page_id); }
        catch (const wzt::StorageError& error) {
            execution_error("CORRUPT_CATALOG", std::string("catalog chunk cannot be read: ") + error.what());
        }
        if (!std::equal(CHUNK_MAGIC.begin(), CHUNK_MAGIC.end(), page.begin())) {
            execution_error("CORRUPT_CATALOG", "catalog chunk magic is invalid");
        }
        const auto next = read_u32(page, 8);
        const auto length = read_u32(page, 12);
        if (length == 0 || length > std::min<std::uint32_t>(remaining, wzt::PAGE_SIZE - CHUNK_HEADER_SIZE)) {
            execution_error("CORRUPT_CATALOG", "catalog chunk length is invalid");
        }
        raw.append(reinterpret_cast<const char*>(page.data() + CHUNK_HEADER_SIZE), length);
        remaining -= length;
        page_id = next;
    }
    if (page_id != NO_PAGE) execution_error("CORRUPT_CATALOG", "catalog chain is longer than root length");
    return {std::move(raw), std::move(pages)};
}

void CatalogManager::persist(const std::map<std::string, TableInfo>& tables) {
    const auto raw = serialize_catalog(tables);
    if (raw.size() > std::numeric_limits<std::uint32_t>::max()) {
        execution_error("CATALOG_TOO_LARGE", "catalog exceeds its 32-bit size field");
    }
    constexpr std::size_t capacity = wzt::PAGE_SIZE - CHUNK_HEADER_SIZE;
    const auto chunk_count = (raw.size() + capacity - 1) / capacity;
    const auto old_root = storage_.read_page(0);
    const auto old_pages = snapshot_pages_;
    std::vector<wzt::PageId> new_pages;
    bool committed = false;
    try {
        for (std::size_t index = 0; index < chunk_count; ++index) new_pages.push_back(storage_.allocate_page());
        for (std::size_t index = 0; index < chunk_count; ++index) {
            const auto begin = index * capacity;
            const auto length = std::min(capacity, raw.size() - begin);
            wzt::Bytes page;
            page.insert(page.end(), CHUNK_MAGIC.begin(), CHUNK_MAGIC.end());
            append_u32(page, index + 1 < new_pages.size() ? static_cast<std::uint32_t>(new_pages[index + 1]) : NO_PAGE);
            append_u32(page, static_cast<std::uint32_t>(length));
            page.insert(page.end(), raw.begin() + static_cast<std::ptrdiff_t>(begin),
                        raw.begin() + static_cast<std::ptrdiff_t>(begin + length));
            storage_.write_page(new_pages[index], page);
            storage_.flush_page(new_pages[index]);
        }
        storage_.write_page(0, root_page(static_cast<std::uint32_t>(new_pages.front()),
                                         static_cast<std::uint32_t>(raw.size()), crc32(raw)));
        storage_.flush_page(0);
        committed = true;
    } catch (...) {
        if (!committed) {
            try {
                storage_.write_page(0, wzt::Bytes(old_root.begin(), old_root.end()));
                storage_.flush_page(0);
            } catch (...) {}
            for (const auto page : new_pages) {
                try { storage_.free_page(page); } catch (...) {}
            }
        }
        throw;
    }
    tables_ = tables;
    snapshot_pages_ = new_pages;
    for (const auto page : old_pages) {
        try { storage_.free_page(page); } catch (...) {}
    }
}

}  // namespace minidbms
