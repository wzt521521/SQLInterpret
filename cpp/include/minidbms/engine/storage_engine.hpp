#pragma once

#include "minidbms/engine/catalog_manager.hpp"
#include "minidbms/engine/record.hpp"

#include <string>
#include <utility>
#include <vector>

namespace minidbms {

struct RowRef {
    std::string table_name;
    wzt::PageId page_id{};
    std::uint32_t slot_id{};
};

using ScannedRow = std::pair<RowRef, minisql::Row>;

class StorageEngine {
public:
    StorageEngine(wzt::StorageManager& storage, CatalogManager& catalog);
    RowRef insert(const std::string& table_name, const minisql::Row& values);
    [[nodiscard]] std::vector<ScannedRow> scan(const std::string& table_name);
    std::size_t erase(const std::vector<RowRef>& refs);

private:
    wzt::StorageManager& storage_;
    CatalogManager& catalog_;
    RecordCodec codec_;
};

}  // namespace minidbms
