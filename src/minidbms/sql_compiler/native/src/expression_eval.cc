#include "minisql/execution.h"

#include <algorithm>
#include <limits>

namespace minisql {
namespace {

[[noreturn]] void execution_error(const std::string& code, const std::string& message,
                                  SourceLocation location) {
  throw DBError(Stage::Execution, code, message, location);
}

DataType value_type(const Value& value) {
  if (std::holds_alternative<std::int64_t>(value)) return DataType::Int;
  if (std::holds_alternative<std::string>(value)) return DataType::Varchar;
  if (std::holds_alternative<bool>(value)) return DataType::Bool;
  return DataType::Unknown;
}

void require_type(const Value& value, DataType required, SourceLocation location) {
  if (value_type(value) != required) {
    execution_error("TYPE_MISMATCH", "Expression requires " + type_name(required) + ".", location);
  }
}

std::int64_t checked_negate(std::int64_t value, SourceLocation location) {
  if (value == std::numeric_limits<std::int64_t>::min()) {
    execution_error("INTEGER_OVERFLOW", "Signed 64-bit integer arithmetic overflow.", location);
  }
  return -value;
}

std::int64_t checked_arithmetic(const std::string& op, std::int64_t left,
                                std::int64_t right, SourceLocation location) {
  constexpr auto min = std::numeric_limits<std::int64_t>::min();
  constexpr auto max = std::numeric_limits<std::int64_t>::max();
  bool overflow = false;
  if (op == "+") {
    overflow = (right > 0 && left > max - right) || (right < 0 && left < min - right);
    if (!overflow) return left + right;
  } else if (op == "-") {
    overflow = (right > 0 && left < min + right) || (right < 0 && left > max + right);
    if (!overflow) return left - right;
  } else if (op == "*") {
    if (left == 0 || right == 0) return std::int64_t{0};
    // Divide the bounds instead of computing an overflowing intermediate.
    // Every divisor below is nonzero and none can trigger INT64_MIN / -1.
    if (left > 0) {
      overflow = right > 0 ? left > max / right : right < min / left;
    } else {
      overflow = right > 0 ? left < min / right : left < max / right;
    }
    if (!overflow) return left * right;
  } else {
    execution_error("UNSUPPORTED_OPERATOR", "Unsupported arithmetic operator '" + op + "'.", location);
  }
  execution_error("INTEGER_OVERFLOW", "Signed 64-bit integer arithmetic overflow.", location);
}

bool compare_order(const std::string& op, int order, SourceLocation location) {
  if (op == "=") return order == 0;
  if (op == "!=") return order != 0;
  if (op == "<") return order < 0;
  if (op == "<=") return order <= 0;
  if (op == ">") return order > 0;
  if (op == ">=") return order >= 0;
  execution_error("UNSUPPORTED_OPERATOR", "Unsupported comparison operator '" + op + "'.", location);
}

// Compare unsigned UTF-8 bytes explicitly, independent of plain-char signedness.
int compare_bytes(const std::string& left, const std::string& right) {
  const auto length = std::min(left.size(), right.size());
  for (std::size_t index = 0; index < length; ++index) {
    const auto a = static_cast<unsigned char>(left[index]);
    const auto b = static_cast<unsigned char>(right[index]);
    if (a < b) return -1;
    if (a > b) return 1;
  }
  return left.size() < right.size() ? -1 : left.size() > right.size() ? 1 : 0;
}

Value evaluate_node(const ExprPtr& expression, const Row& row) {
  if (const auto literal = std::dynamic_pointer_cast<const LiteralExpr>(expression)) {
    if (!literal->integer_text.empty()) {
      execution_error("UNBOUND_EXPRESSION", "Integer literal has not passed semantic analysis.", literal->location);
    }
    return literal->value;
  }
  if (const auto identifier = std::dynamic_pointer_cast<const IdentifierExpr>(expression)) {
    if (!identifier->column_index || identifier->table_name.empty()) {
      execution_error("UNBOUND_EXPRESSION", "Column has not been bound to a table and row index.", identifier->location);
    }
    if (*identifier->column_index >= row.size()) {
      execution_error("INVALID_ROW", "Bound column index is outside the supplied full row.", identifier->location);
    }
    const auto& value = row[*identifier->column_index];
    if (value_type(value) != identifier->data_type) {
      execution_error("INVALID_ROW", "Row value type does not match the bound column type.", identifier->location);
    }
    return value;
  }
  if (const auto unary = std::dynamic_pointer_cast<const UnaryExpr>(expression)) {
    const auto operand = evaluate_expression(unary->operand, row);
    if (unary->op == "NOT") {
      require_type(operand, DataType::Bool, unary->location);
      return !std::get<bool>(operand);
    }
    if (unary->op == "+" || unary->op == "-") {
      require_type(operand, DataType::Int, unary->location);
      const auto value = std::get<std::int64_t>(operand);
      return unary->op == "+" ? value : checked_negate(value, unary->location);
    }
    execution_error("UNSUPPORTED_OPERATOR", "Unsupported unary operator '" + unary->op + "'.", unary->location);
  }
  if (const auto binary = std::dynamic_pointer_cast<const BinaryExpr>(expression)) {
    // Do not evaluate the right operand before deciding whether it is needed.
    const auto left = evaluate_expression(binary->left, row);
    if (binary->op == "AND" || binary->op == "OR") {
      require_type(left, DataType::Bool, binary->location);
      const bool left_bool = std::get<bool>(left);
      if (binary->op == "AND" && !left_bool) return false;
      if (binary->op == "OR" && left_bool) return true;
      const auto right = evaluate_expression(binary->right, row);
      require_type(right, DataType::Bool, binary->location);
      return std::get<bool>(right);
    }
    const auto right = evaluate_expression(binary->right, row);
    if (binary->op == "+" || binary->op == "-" || binary->op == "*") {
      require_type(left, DataType::Int, binary->location);
      require_type(right, DataType::Int, binary->location);
      return checked_arithmetic(binary->op, std::get<std::int64_t>(left),
                                std::get<std::int64_t>(right), binary->location);
    }
    const auto type = value_type(left);
    if (type == DataType::Unknown || type != value_type(right)) {
      execution_error("TYPE_MISMATCH", "Comparison operands must have the same supported type.", binary->location);
    }
    if (type == DataType::Int) {
      const auto a = std::get<std::int64_t>(left);
      const auto b = std::get<std::int64_t>(right);
      return compare_order(binary->op, a < b ? -1 : a > b ? 1 : 0, binary->location);
    }
    if (type == DataType::Varchar) {
      return compare_order(binary->op, compare_bytes(std::get<std::string>(left), std::get<std::string>(right)),
                           binary->location);
    }
    if (binary->op != "=" && binary->op != "!=") {
      execution_error("TYPE_MISMATCH", "BOOL supports only equality and inequality comparisons.", binary->location);
    }
    const bool same = std::get<bool>(left) == std::get<bool>(right);
    return binary->op == "=" ? same : !same;
  }
  execution_error("UNSUPPORTED_EXPRESSION", "Unsupported expression node.", expression->location);
}

}  // namespace

Value evaluate_expression(const ExprPtr& expression, const Row& row) {
  if (!expression) execution_error("UNBOUND_EXPRESSION", "Null expression node.", {});
  if (expression->data_type == DataType::Unknown) {
    execution_error("UNBOUND_EXPRESSION", "Expression has not passed semantic type checking.", expression->location);
  }
  const auto result = evaluate_node(expression, row);
  require_type(result, expression->data_type, expression->location);
  return result;
}

}  // namespace minisql
