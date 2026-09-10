#pragma once

#include "minisql/ast.h"

namespace minisql {

struct PlanNode { virtual ~PlanNode() = default; };
using PlanPtr = std::shared_ptr<const PlanNode>;

struct CreateTablePlan final : PlanNode {
  TableSchema schema;
};
struct InsertPlan final : PlanNode {
  std::string table_name;
  std::vector<std::string> columns;
  std::vector<ExprPtr> values;
};
struct SeqScanPlan final : PlanNode {
  std::string table_name;
  TableSchema schema;
};
struct FilterPlan final : PlanNode {
  ExprPtr predicate;
  PlanPtr child;
};
struct ProjectPlan final : PlanNode {
  std::vector<std::string> columns;
  std::vector<std::size_t> column_indices;
  PlanPtr child;
};
struct DeletePlan final : PlanNode {
  std::string table_name;
  PlanPtr child;
};

PlanPtr build_plan(const StmtPtr& bound_statement);
PlanPtr optimize_plan(const PlanPtr& plan);
ExprPtr optimize_expression(const ExprPtr& expression);

}  // namespace minisql
