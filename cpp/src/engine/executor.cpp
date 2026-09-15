#include "minidbms/engine/executor.hpp"

#include "minidbms/engine/errors.hpp"

#include <algorithm>
#include <memory>
#include <set>

namespace minidbms {

Executor::Executor(CatalogManager& catalog, StorageEngine& rows)
    : catalog_(catalog), rows_(rows) {}

minisql::ExecutionResult Executor::execute(const minisql::PlanPtr& plan) {
    if (const auto create = std::dynamic_pointer_cast<const minisql::CreateTablePlan>(plan)) {
        catalog_.create_table(create->schema);
        return {{}, {}, 0, "table " + create->schema.table_name + " created"};
    }
    if (const auto insert = std::dynamic_pointer_cast<const minisql::InsertPlan>(plan)) {
        const auto schema = catalog_.get_schema(insert->table_name);
        if (insert->columns.size() != schema.columns.size() ||
            insert->values.size() != schema.columns.size()) {
            execution_error("VALUE_COUNT_MISMATCH", "INSERT values must match the table schema");
        }
        minisql::Row values;
        for (std::size_t index = 0; index < schema.columns.size(); ++index) {
            if (insert->columns[index] != schema.columns[index].name) {
                execution_error("VALUE_COUNT_MISMATCH", "INSERT columns are not in schema order");
            }
            values.push_back(minisql::evaluate_expression(insert->values[index], {}));
        }
        rows_.insert(insert->table_name, values);
        return {{}, {}, 1, "1 row inserted"};
    }
    if (const auto remove = std::dynamic_pointer_cast<const minisql::DeletePlan>(plan)) {
        if (minisql::lower_ascii(table_name(remove->child)) != minisql::lower_ascii(remove->table_name)) {
            execution_error("INVALID_PLAN", "DELETE child scans another table");
        }
        std::vector<RowRef> refs;
        for (const auto& row : scan(remove->child)) refs.push_back(row.first);
        const auto affected = rows_.erase(refs);
        return {{}, {}, affected, std::to_string(affected) + " row(s) deleted"};
    }
    if (std::dynamic_pointer_cast<const minisql::ProjectPlan>(plan) ||
        std::dynamic_pointer_cast<const minisql::FilterPlan>(plan) ||
        std::dynamic_pointer_cast<const minisql::SeqScanPlan>(plan)) {
        const auto name = table_name(plan);
        const auto schema = catalog_.get_schema(name);
        std::vector<std::string> columns;
        std::vector<std::size_t> indices;
        if (const auto project = std::dynamic_pointer_cast<const minisql::ProjectPlan>(plan)) {
            columns = project->columns;
            indices = project->column_indices;
        } else {
            for (std::size_t index = 0; index < schema.columns.size(); ++index) {
                columns.push_back(schema.columns[index].name);
                indices.push_back(index);
            }
        }
        if (columns.size() != indices.size()) execution_error("INVALID_PLAN", "projection metadata is inconsistent");
        for (std::size_t index = 0; index < columns.size(); ++index) {
            if (indices[index] >= schema.columns.size() || schema.columns[indices[index]].name != columns[index]) {
                execution_error("COLUMN_NOT_FOUND", "projection references a missing column");
            }
        }
        minisql::ExecutionResult result;
        result.columns = columns;
        const auto input = scan(std::dynamic_pointer_cast<const minisql::ProjectPlan>(plan)
                                    ? std::dynamic_pointer_cast<const minisql::ProjectPlan>(plan)->child
                                    : plan);
        for (const auto& item : input) {
            minisql::Row projected;
            for (const auto index : indices) projected.push_back(item.second[index]);
            result.rows.push_back(std::move(projected));
        }
        result.message = std::to_string(result.rows.size()) + " row(s)";
        return result;
    }
    execution_error("INVALID_PLAN", "unsupported execution plan");
}

std::vector<ScannedRow> Executor::scan(const minisql::PlanPtr& plan) {
    if (const auto sequence = std::dynamic_pointer_cast<const minisql::SeqScanPlan>(plan)) {
        return rows_.scan(sequence->table_name);
    }
    if (const auto filter = std::dynamic_pointer_cast<const minisql::FilterPlan>(plan)) {
        std::vector<ScannedRow> result;
        for (auto& item : scan(filter->child)) {
            const auto accepted = minisql::evaluate_expression(filter->predicate, item.second);
            if (!std::holds_alternative<bool>(accepted)) {
                execution_error("TYPE_MISMATCH", "WHERE expression must evaluate to BOOL");
            }
            if (std::get<bool>(accepted)) result.push_back(std::move(item));
        }
        return result;
    }
    execution_error("INVALID_PLAN", "scan plan must be SeqScan or Filter");
}

std::string Executor::table_name(const minisql::PlanPtr& plan) const {
    if (const auto sequence = std::dynamic_pointer_cast<const minisql::SeqScanPlan>(plan)) {
        return sequence->table_name;
    }
    if (const auto filter = std::dynamic_pointer_cast<const minisql::FilterPlan>(plan)) {
        return table_name(filter->child);
    }
    if (const auto project = std::dynamic_pointer_cast<const minisql::ProjectPlan>(plan)) {
        return table_name(project->child);
    }
    execution_error("INVALID_PLAN", "plan does not identify a table");
}

}  // namespace minidbms
