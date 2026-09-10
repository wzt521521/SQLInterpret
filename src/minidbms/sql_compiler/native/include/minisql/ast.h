#pragma once

#include "minisql/common.h"
#include <memory>

namespace minisql {

struct Expr {
  explicit Expr(SourceLocation location = {}) : location(location) {}
  virtual ~Expr() = default;
  SourceLocation location;
  DataType data_type{DataType::Unknown};
};
using ExprPtr = std::shared_ptr<const Expr>;

struct IdentifierExpr final : Expr {
  IdentifierExpr(std::string name, SourceLocation location = {})
      : Expr(location), name(std::move(name)) {}
  std::string name;
  std::string table_name;
  std::optional<std::size_t> column_index;
};

struct LiteralExpr final : Expr {
  LiteralExpr(Value value, SourceLocation location = {}, std::string integer_text = {})
      : Expr(location), value(std::move(value)), integer_text(std::move(integer_text)) {}
  Value value;
  // Integer source text is resolved in semantic analysis, including INT64_MIN.
  std::string integer_text;
};

struct UnaryExpr final : Expr {
  UnaryExpr(std::string op, ExprPtr operand, SourceLocation location = {})
      : Expr(location), op(std::move(op)), operand(std::move(operand)) {}
  std::string op;
  ExprPtr operand;
};

struct BinaryExpr final : Expr {
  BinaryExpr(std::string op, ExprPtr left, ExprPtr right, SourceLocation location = {})
      : Expr(location), op(std::move(op)), left(std::move(left)), right(std::move(right)) {}
  std::string op;
  ExprPtr left;
  ExprPtr right;
};

struct ColumnSpec {
  std::string name;
  std::string type_name;
  SourceLocation location;
  SourceLocation type_location;
};

struct Statement {
  virtual ~Statement() = default;
  SourceLocation location;
  std::string table_name;
  SourceLocation table_location;
  std::shared_ptr<const TableSchema> schema;
};
using StmtPtr = std::shared_ptr<const Statement>;

struct CreateTableStmt final : Statement {
  std::vector<ColumnSpec> columns;
};

struct InsertStmt final : Statement {
  bool explicit_columns{false};
  std::vector<std::shared_ptr<const IdentifierExpr>> columns;
  std::vector<ExprPtr> values;
};

struct SelectStmt final : Statement {
  bool select_all{false};
  std::vector<std::shared_ptr<const IdentifierExpr>> columns;
  ExprPtr where;
};

struct DeleteStmt final : Statement {
  ExprPtr where;
};

}  // namespace minisql
