#pragma once
#include "minisql/common.h"
#include <map>

namespace minisql::app {
// CLI session metadata only; row execution and persistence belong to the host.
class SessionCatalog final : public CatalogView {
 public:
  bool table_exists(const std::string& name) const override { return schemas_.count(name) != 0; }
  TableSchema get_schema(const std::string& name) const override {
    const auto found = schemas_.find(name);
    if (found == schemas_.end()) throw DBError(Stage::Semantic, "TABLE_NOT_FOUND", "Unknown session table: " + name);
    return found->second;
  }
  void register_schema(const TableSchema& schema) {
    if (!schemas_.emplace(schema.table_name, schema).second)
      throw DBError(Stage::Semantic, "TABLE_ALREADY_EXISTS", "Session table already exists: " + schema.table_name);
  }
 private:
  std::map<std::string, TableSchema> schemas_;
};
}  // namespace minisql::app
