#include "minisql/plan.h"
#include <map>

namespace minisql {
PlanPtr build_plan(const StmtPtr& statement) {
  if (!statement || !statement->schema) throw DBError(Stage::Semantic, "UNBOUND_AST", "Run semantic analysis before planning.");
  const auto& schema = *statement->schema;
  if (std::dynamic_pointer_cast<const CreateTableStmt>(statement)) {
    auto plan = std::make_shared<CreateTablePlan>(); plan->schema = schema; return plan;
  }
  if (const auto insert = std::dynamic_pointer_cast<const InsertStmt>(statement)) {
    if (insert->columns.size() != insert->values.size()) throw DBError(Stage::Semantic, "UNBOUND_AST", "Invalid bound INSERT.", statement->location);
    std::map<std::string, ExprPtr> values;
    for (std::size_t i = 0; i < insert->columns.size(); ++i) {
      const auto& column = insert->columns[i];
      if (!column || !column->column_index || *column->column_index >= schema.columns.size() ||
          !insert->values[i] || insert->values[i]->data_type == DataType::Unknown)
        throw DBError(Stage::Semantic, "UNBOUND_AST", "INSERT columns or values are unbound.", statement->location);
      values.emplace(insert->columns[i]->name, insert->values[i]);
    }
    auto plan = std::make_shared<InsertPlan>(); plan->table_name = statement->table_name;
    for (const auto& column : schema.columns) {
      const auto found = values.find(column.name);
      if (found == values.end()) throw DBError(Stage::Semantic, "UNBOUND_AST", "INSERT is missing a bound value.", statement->location);
      plan->columns.push_back(column.name); plan->values.push_back(found->second);
    }
    return plan;
  }
  const auto select = std::dynamic_pointer_cast<const SelectStmt>(statement);
  const auto remove = std::dynamic_pointer_cast<const DeleteStmt>(statement);
  if (!select && !remove) throw DBError(Stage::Semantic, "UNSUPPORTED_STATEMENT", "Unknown statement node.", statement->location);
  auto scan = std::make_shared<SeqScanPlan>(); scan->table_name = statement->table_name; scan->schema = schema;
  PlanPtr child = scan;
  const auto where = select ? select->where : remove->where;
  if (where) {
    if (where->data_type != DataType::Bool)
      throw DBError(Stage::Semantic, "UNBOUND_AST", "WHERE must be a bound BOOL expression.", where->location);
    auto filter = std::make_shared<FilterPlan>(); filter->predicate = where; filter->child = child; child = filter;
  }
  if (remove) {
    auto plan = std::make_shared<DeletePlan>(); plan->table_name = statement->table_name; plan->child = child; return plan;
  }
  auto project = std::make_shared<ProjectPlan>(); project->child = child;
  for (const auto& column : select->columns) {
    if (!column || !column->column_index || *column->column_index >= schema.columns.size())
      throw DBError(Stage::Semantic, "UNBOUND_AST", "SELECT columns are unbound.", statement->location);
    project->columns.push_back(column->name); project->column_indices.push_back(*column->column_index);
  }
  return project;
}
}  // namespace minisql
