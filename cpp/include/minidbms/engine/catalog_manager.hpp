#pragma once

#include "minisql/common.h"
#include "wzt/storage_manager.hpp"

#include <map>
#include <string>
#include <vector>

namespace minidbms {

struct TableInfo {
    minisql::TableSchema schema;
    std::vector<wzt::PageId> pages;
};

class CatalogManager final : public minisql::CatalogView {
public:
    explicit CatalogManager(wzt::StorageManager& storage);

    [[nodiscard]] bool table_exists(const std::string& table_name) const override;
    [[nodiscard]] minisql::TableSchema get_schema(const std::string& table_name) const override;
    [[nodiscard]] std::vector<wzt::PageId> page_ids(const std::string& table_name) const;
    [[nodiscard]] std::vector<TableInfo> list_tables() const;

    void create_table(const minisql::TableSchema& schema);
    void add_page(const std::string& table_name, wzt::PageId page_id);

private:
    wzt::StorageManager& storage_;
    std::map<std::string, TableInfo> tables_;
    std::vector<wzt::PageId> snapshot_pages_;

    [[nodiscard]] const TableInfo& info(const std::string& table_name) const;
    void load(const wzt::PageData& root);
    [[nodiscard]] std::pair<std::string, std::vector<wzt::PageId>>
        read_snapshot(std::uint32_t first, std::uint32_t size);
    void persist(const std::map<std::string, TableInfo>& tables);
};

}  // namespace minidbms
