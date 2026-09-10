#pragma once
#include "minisql/ast.h"

namespace minisql {
class SemanticAnalyzer {
 public:
  explicit SemanticAnalyzer(const CatalogView& catalog) : catalog_(catalog) {}
  StmtPtr analyze(const StmtPtr& statement) const;
 private:
  const CatalogView& catalog_;
  ExprPtr bind_expression(const ExprPtr& expression, const TableSchema* schema) const;
  std::shared_ptr<const IdentifierExpr> bind_column(const IdentifierExpr& column,
                                                  const TableSchema& schema) const;
};
}  // namespace minisql
