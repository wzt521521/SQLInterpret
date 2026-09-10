#include "minisql/semantic.h"

#include <limits>
#include <set>
#include <sstream>

namespace minisql {
namespace {

[[noreturn]] void semantic_error(const std::string& code, const std::string& message,
                                 SourceLocation location = {}) {
  throw DBError(Stage::Semantic, code, message, location);
}

DataType literal_type(const Value& value) {
  if (std::holds_alternative<std::int64_t>(value)) return DataType::Int;
  if (std::holds_alternative<std::string>(value)) return DataType::Varchar;
  if (std::holds_alternative<bool>(value)) return DataType::Bool;
  semantic_error("UNSUPPORTED_TYPE", "Unsupported literal type.");
}

// Accumulate an unsigned magnitude with a checked bound. Never convert a value
// greater than INT64_MAX to int64_t; INT64_MIN is handled explicitly below.
std::int64_t parse_integer(const std::string& text, bool negative,
                           SourceLocation location) {
  const auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  const std::uint64_t limit = max + (negative ? std::uint64_t{1} : std::uint64_t{0});
  std::uint64_t magnitude = 0;
  if (text.empty()) semantic_error("INTEGER_OUT_OF_RANGE", "Invalid integer literal.", location);
  for (const char ch : text) {
    if (ch < '0' || ch > '9') {
      semantic_error("INTEGER_OUT_OF_RANGE", "Invalid integer literal.", location);
    }
    const auto digit = static_cast<std::uint64_t>(ch - '0');
    if (magnitude > (limit - digit) / 10) {
      semantic_error("INTEGER_OUT_OF_RANGE", "Integer literal is outside the signed 64-bit range.", location);
    }
    magnitude = magnitude * 10 + digit;
  }
  if (negative && magnitude == max + std::uint64_t{1}) {
    return std::numeric_limits<std::int64_t>::min();
  }
  const auto positive = static_cast<std::int64_t>(magnitude);
  return negative ? -positive : positive;
}

bool is_comparison(const std::string& op) {
  return op == "=" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

}  // namespace

std::shared_ptr<const IdentifierExpr> SemanticAnalyzer::bind_column(
    const IdentifierExpr& column, const TableSchema& schema) const {
  for (std::size_t index = 0; index < schema.columns.size(); ++index) {
    const auto& definition = schema.columns[index];
    if (definition.name != column.name) continue;
    if (definition.data_type != DataType::Int && definition.data_type != DataType::Varchar) {
      semantic_error("UNSUPPORTED_TYPE", "Catalog columns must use INT or VARCHAR.", column.location);
    }
    auto bound = std::make_shared<IdentifierExpr>(column);
    bound->table_name = schema.table_name;
    bound->column_index = index;
    bound->data_type = definition.data_type;
    return bound;
  }
  semantic_error("COLUMN_NOT_FOUND", "Column '" + column.name + "' does not exist in table '" +
                 schema.table_name + "'.", column.location);
}

ExprPtr SemanticAnalyzer::bind_expression(const ExprPtr& expression,
                                           const TableSchema* schema) const {
  if (!expression) semantic_error("UNSUPPORTED_EXPRESSION", "Null expression node.");
  if (const auto identifier = std::dynamic_pointer_cast<const IdentifierExpr>(expression)) {
    if (!schema) {
      semantic_error("COLUMN_NOT_ALLOWED", "INSERT VALUES requires constant expressions; column references are not allowed.",
                     expression->location);
    }
    return bind_column(*identifier, *schema);
  }
  if (const auto literal = std::dynamic_pointer_cast<const LiteralExpr>(expression)) {
    auto bound = std::make_shared<LiteralExpr>(*literal);
    if (!literal->integer_text.empty()) {
      bound->value = parse_integer(literal->integer_text, false, literal->location);
      bound->integer_text.clear();
    }
    bound->data_type = literal_type(bound->value);
    return bound;
  }
  if (const auto unary = std::dynamic_pointer_cast<const UnaryExpr>(expression)) {
    // SQL spells INT64_MIN as a minus token followed by a magnitude that does
    // not itself fit in int64_t. Resolve that one signed literal atomically.
    if (unary->op == "-") {
      const auto literal = std::dynamic_pointer_cast<const LiteralExpr>(unary->operand);
      if (literal && !literal->integer_text.empty()) {
        const auto value = parse_integer(literal->integer_text, true, literal->location);
        if (value == std::numeric_limits<std::int64_t>::min()) {
          auto bound = std::make_shared<LiteralExpr>(value, unary->location);
          bound->data_type = DataType::Int;
          return bound;
        }
      }
    }
    const auto operand = bind_expression(unary->operand, schema);
    const auto required = unary->op == "NOT" ? DataType::Bool : DataType::Int;
    if (unary->op != "NOT" && unary->op != "+" && unary->op != "-") {
      semantic_error("UNSUPPORTED_OPERATOR", "Unsupported operator '" + unary->op + "'.", unary->location);
    }
    if (operand->data_type != required) {
      semantic_error("TYPE_MISMATCH", "Operator '" + unary->op + "' requires " + type_name(required) + ".", unary->location);
    }
    auto bound = std::make_shared<UnaryExpr>(*unary);
    bound->operand = operand;
    bound->data_type = required;
    return bound;
  }
  if (const auto binary = std::dynamic_pointer_cast<const BinaryExpr>(expression)) {
    // Bind both operands before optimization; even unreachable expressions
    // must have valid names and types.
    const auto left = bind_expression(binary->left, schema);
    const auto right = bind_expression(binary->right, schema);
    const auto left_type = left->data_type;
    const auto right_type = right->data_type;
    bool valid = false;
    DataType result = DataType::Bool;
    if (binary->op == "AND" || binary->op == "OR") {
      valid = left_type == DataType::Bool && right_type == DataType::Bool;
    } else if (binary->op == "+" || binary->op == "-" || binary->op == "*") {
      valid = left_type == DataType::Int && right_type == DataType::Int;
      result = DataType::Int;
    } else if (is_comparison(binary->op)) {
      valid = left_type == right_type &&
          (left_type == DataType::Int || left_type == DataType::Varchar ||
           (left_type == DataType::Bool && (binary->op == "=" || binary->op == "!=")));
    } else {
      semantic_error("UNSUPPORTED_OPERATOR", "Unsupported operator '" + binary->op + "'.", binary->location);
    }
    if (!valid) {
      semantic_error("TYPE_MISMATCH", "Operator '" + binary->op + "' cannot be applied to " +
                     type_name(left_type) + " and " + type_name(right_type) + ".", binary->location);
    }
    auto bound = std::make_shared<BinaryExpr>(*binary);
    bound->left = left;
    bound->right = right;
    bound->data_type = result;
    return bound;
  }
  semantic_error("UNSUPPORTED_EXPRESSION", "Unsupported expression node.", expression->location);
}

StmtPtr SemanticAnalyzer::analyze(const StmtPtr& statement) const {
  if (!statement) semantic_error("UNSUPPORTED_STATEMENT", "Null statement node.");
  if (const auto create = std::dynamic_pointer_cast<const CreateTableStmt>(statement)) {
    if (catalog_.table_exists(create->table_name)) {
      semantic_error("TABLE_ALREADY_EXISTS", "Table '" + create->table_name + "' already exists.", create->table_location);
    }
    auto schema = std::make_shared<TableSchema>();
    schema->table_name = create->table_name;
    std::set<std::string> names;
    for (const auto& column : create->columns) {
      if (!names.insert(column.name).second) {
        semantic_error("DUPLICATE_COLUMN", "Duplicate column '" + column.name + "'.", column.location);
      }
      const auto type = lower_ascii(column.type_name);
      if (type != "int" && type != "varchar") {
        semantic_error("UNSUPPORTED_TYPE", "Unsupported column type '" + column.type_name +
                       "'; expected INT or VARCHAR.", column.type_location);
      }
      schema->columns.push_back({column.name, type == "int" ? DataType::Int : DataType::Varchar});
    }
    if (schema->columns.empty()) semantic_error("MISSING_COLUMN", "CREATE TABLE requires at least one column.", create->location);
    auto bound = std::make_shared<CreateTableStmt>(*create);
    bound->schema = schema;
    return bound;
  }

  const auto insert = std::dynamic_pointer_cast<const InsertStmt>(statement);
  const auto select = std::dynamic_pointer_cast<const SelectStmt>(statement);
  const auto deletion = std::dynamic_pointer_cast<const DeleteStmt>(statement);
  if (!insert && !select && !deletion) {
    semantic_error("UNSUPPORTED_STATEMENT", "Unsupported statement node.", statement->location);
  }
  if (!catalog_.table_exists(statement->table_name)) {
    semantic_error("TABLE_NOT_FOUND", "Table '" + statement->table_name + "' does not exist.", statement->table_location);
  }
  // get_schema returns by value: the bound statement owns its schema snapshot.
  const auto schema = std::make_shared<const TableSchema>(catalog_.get_schema(statement->table_name));
  if (insert) {
    auto bound = std::make_shared<InsertStmt>(*insert);
    bound->schema = schema;
    bound->columns.clear();
    bound->values.clear();
    auto columns = insert->columns;
    if (!insert->explicit_columns) {
      columns.clear();
      for (const auto& column : schema->columns) {
        columns.push_back(std::make_shared<IdentifierExpr>(column.name, insert->table_location));
      }
    }
    std::set<std::string> seen;
    for (const auto& column : columns) {
      if (!column) semantic_error("COLUMN_NOT_FOUND", "Null INSERT column.", insert->location);
      if (!seen.insert(column->name).second) {
        semantic_error("DUPLICATE_COLUMN", "Duplicate INSERT column '" + column->name + "'.", column->location);
      }
      bound->columns.push_back(bind_column(*column, *schema));
    }
    if (bound->columns.size() != insert->values.size()) {
      semantic_error("VALUE_COUNT_MISMATCH", "INSERT has " + std::to_string(bound->columns.size()) +
                     " columns but " + std::to_string(insert->values.size()) + " values.", insert->location);
    }
    std::ostringstream missing;
    bool has_missing = false;
    for (const auto& column : schema->columns) {
      if (seen.count(column.name) != 0) continue;
      if (has_missing) missing << ", ";
      missing << column.name;
      has_missing = true;
    }
    if (has_missing) {
      semantic_error("MISSING_COLUMN", "INSERT must provide all columns (no NULL/defaults); missing: " +
                     missing.str() + ".", insert->location);
    }
    for (std::size_t index = 0; index < insert->values.size(); ++index) {
      const auto value = bind_expression(insert->values[index], nullptr);
      const auto& column = bound->columns[index];
      if (value->data_type != column->data_type) {
        semantic_error("TYPE_MISMATCH", schema->table_name + "." + column->name + " expects " +
                       type_name(column->data_type) + ", but " + type_name(value->data_type) + " found.", value->location);
      }
      bound->values.push_back(value);
    }
    return bound;
  }

  const auto original_where = select ? select->where : deletion->where;
  const auto where = original_where ? bind_expression(original_where, schema.get()) : nullptr;
  if (where && where->data_type != DataType::Bool) {
    semantic_error("TYPE_MISMATCH", "WHERE requires a BOOL expression.", where->location);
  }
  if (select) {
    auto bound = std::make_shared<SelectStmt>(*select);
    bound->schema = schema;
    bound->where = where;
    bound->columns.clear();
    if (select->select_all) {
      for (const auto& column : schema->columns) {
        bound->columns.push_back(bind_column(IdentifierExpr(column.name, select->location), *schema));
      }
    } else {
      for (const auto& column : select->columns) {
        if (!column) semantic_error("COLUMN_NOT_FOUND", "Null SELECT column.", select->location);
        bound->columns.push_back(bind_column(*column, *schema));
      }
    }
    return bound;
  }
  auto bound = std::make_shared<DeleteStmt>(*deletion);
  bound->schema = schema;
  bound->where = where;
  return bound;
}

}  // namespace minisql
