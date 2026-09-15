#pragma once

#include "minidbms/engine/catalog_manager.hpp"
#include "minidbms/engine/storage_engine.hpp"
#include "minisql/execution.h"

#include <string>
#include <vector>

namespace minidbms {

class Executor final : public minisql::Executor {
public:
    Executor(CatalogManager& catalog, StorageEngine& rows);
    minisql::ExecutionResult execute(const minisql::PlanPtr& plan) override;

private:
    CatalogManager& catalog_;
    StorageEngine& rows_;

    [[nodiscard]] std::vector<ScannedRow> scan(const minisql::PlanPtr& plan);
    [[nodiscard]] std::string table_name(const minisql::PlanPtr& plan) const;
};

}  // namespace minidbms
