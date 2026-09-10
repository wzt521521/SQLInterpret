#include "minisql/format.h"
#include <iomanip>
#include <sstream>

namespace minisql {
namespace {
template <typename Container, typename Transform>
std::string join(const Container& values, const std::string& separator, Transform transform) {
  std::string output;
  bool first = true;
  for (const auto& value : values) { if (!first) output += separator; first = false; output += transform(value); }
  return output;
}
std::string location_json(SourceLocation location) {
  return "{\"line\":" + std::to_string(location.line) + ",\"column\":" + std::to_string(location.column) + "}";
}
std::string value_json(const Value& value) {
  if (const auto* text = std::get_if<std::string>(&value)) return json_quote(*text);
  if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? "true" : "false";
  return std::to_string(std::get<std::int64_t>(value));
}
std::string strings_json(const std::vector<std::string>& strings) {
  return "[" + join(strings, ",", [](const auto& s) { return json_quote(s); }) + "]";
}
std::string schema_json(const TableSchema& schema) {
  return "{\"table_name\":" + json_quote(schema.table_name) + ",\"columns\":[" +
    join(schema.columns, ",", [](const ColumnDef& c) { return "{\"name\":" + json_quote(c.name) + ",\"data_type\":" + json_quote(type_name(c.data_type)) + "}"; }) + "]}";
}
std::string plan_text(const PlanPtr& plan, std::size_t depth) {
  std::string label;
  PlanPtr child;
  if (const auto p = std::dynamic_pointer_cast<const CreateTablePlan>(plan))
    label = "CreateTable[" + p->schema.table_name + " (" + join(p->schema.columns, ", ", [](const ColumnDef& c) { return c.name + " " + type_name(c.data_type); }) + ")]";
  else if (const auto p = std::dynamic_pointer_cast<const InsertPlan>(plan))
    label = "Insert[" + p->table_name + "; " + join(p->columns, ", ", [](const auto& s) { return s; }) + "; " + join(p->values, ", ", format_expression) + "]";
  else if (const auto p = std::dynamic_pointer_cast<const SeqScanPlan>(plan)) label = "SeqScan[" + p->table_name + "]";
  else if (const auto p = std::dynamic_pointer_cast<const FilterPlan>(plan)) { label = "Filter[" + format_expression(p->predicate) + "]"; child = p->child; }
  else if (const auto p = std::dynamic_pointer_cast<const ProjectPlan>(plan)) { label = "Project[" + join(p->columns, ", ", [](const auto& s) { return s; }) + "]"; child = p->child; }
  else if (const auto p = std::dynamic_pointer_cast<const DeletePlan>(plan)) { label = "Delete[" + p->table_name + "]"; child = p->child; }
  else throw DBError(Stage::Internal, "INVALID_PLAN", "Unknown plan node.");
  return std::string(depth * 2, ' ') + label + (child ? "\n" + plan_text(child, depth + 1) : "");
}
}  // namespace

std::string json_quote(const std::string& value) {
  std::ostringstream stream;
  stream << '"';
  for (unsigned char c : value) {
    switch (c) {
      case '"': stream << "\\\""; break;
      case '\\': stream << "\\\\"; break;
      case '\n': stream << "\\n"; break;
      case '\r': stream << "\\r"; break;
      case '\t': stream << "\\t"; break;
      default:
        if (c < 0x20U) stream << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c);
        else stream << static_cast<char>(c);
    }
  }
  stream << '"'; return stream.str();
}

std::string format_expression(const ExprPtr& expression) {
  if (!expression) return "null";
  if (const auto e = std::dynamic_pointer_cast<const IdentifierExpr>(expression)) return e->name;
  if (const auto e = std::dynamic_pointer_cast<const LiteralExpr>(expression)) return e->integer_text.empty() ? value_text(e->value) : e->integer_text;
  if (const auto e = std::dynamic_pointer_cast<const UnaryExpr>(expression)) return "(" + e->op + " " + format_expression(e->operand) + ")";
  if (const auto e = std::dynamic_pointer_cast<const BinaryExpr>(expression)) return "(" + format_expression(e->left) + " " + e->op + " " + format_expression(e->right) + ")";
  throw DBError(Stage::Internal, "INVALID_EXPRESSION", "Unknown expression node.");
}

std::string expression_json(const ExprPtr& expression) {
  if (!expression) return "null";
  std::string fields;
  if (const auto e = std::dynamic_pointer_cast<const IdentifierExpr>(expression)) {
    fields = "\"node\":\"IdentifierExpr\",\"name\":" + json_quote(e->name) + ",\"table_name\":" +
      json_quote(e->table_name) + ",\"column_index\":" + (e->column_index ? std::to_string(*e->column_index) : "null");
  } else if (const auto e = std::dynamic_pointer_cast<const LiteralExpr>(expression)) {
    fields = "\"node\":\"LiteralExpr\",\"value\":" + (e->integer_text.empty() ? value_json(e->value) : "null");
    if (!e->integer_text.empty()) fields += ",\"integer_text\":" + json_quote(e->integer_text);
  } else if (const auto e = std::dynamic_pointer_cast<const UnaryExpr>(expression)) {
    fields = "\"node\":\"UnaryExpr\",\"op\":" + json_quote(e->op) + ",\"operand\":" + expression_json(e->operand);
  } else if (const auto e = std::dynamic_pointer_cast<const BinaryExpr>(expression)) {
    fields = "\"node\":\"BinaryExpr\",\"op\":" + json_quote(e->op) + ",\"left\":" + expression_json(e->left) + ",\"right\":" + expression_json(e->right);
  } else throw DBError(Stage::Internal, "INVALID_EXPRESSION", "Unknown expression node.");
  return "{" + fields + ",\"data_type\":" + json_quote(type_name(expression->data_type)) + ",\"location\":" + location_json(expression->location) + "}";
}

std::string ast_json(const StmtPtr& statement) {
  if (!statement) return "null";
  std::string fields;
  if (const auto s = std::dynamic_pointer_cast<const CreateTableStmt>(statement)) {
    fields = "\"node\":\"CreateTableStmt\",\"columns\":[" + join(s->columns, ",", [](const ColumnSpec& c) {
      return "{\"name\":" + json_quote(c.name) + ",\"type_name\":" + json_quote(c.type_name) + ",\"location\":" + location_json(c.location) + ",\"type_location\":" + location_json(c.type_location) + "}";
    }) + "]";
  } else if (const auto s = std::dynamic_pointer_cast<const InsertStmt>(statement)) {
    fields = "\"node\":\"InsertStmt\",\"explicit_columns\":" + std::string(s->explicit_columns ? "true" : "false") +
      ",\"columns\":[" + join(s->columns, ",", [](const auto& c) { return expression_json(c); }) + "]" +
      ",\"values\":[" + join(s->values, ",", expression_json) + "]";
  } else if (const auto s = std::dynamic_pointer_cast<const SelectStmt>(statement)) {
    fields = "\"node\":\"SelectStmt\",\"select_all\":" + std::string(s->select_all ? "true" : "false") +
      ",\"columns\":[" + join(s->columns, ",", [](const auto& c) { return expression_json(c); }) + "]" + ",\"where\":" + expression_json(s->where);
  } else if (const auto s = std::dynamic_pointer_cast<const DeleteStmt>(statement)) {
    fields = "\"node\":\"DeleteStmt\",\"where\":" + expression_json(s->where);
  } else throw DBError(Stage::Internal, "INVALID_AST", "Unknown statement node.");
  return "{" + fields + ",\"table_name\":" + json_quote(statement->table_name) + ",\"location\":" + location_json(statement->location) +
    ",\"table_location\":" + location_json(statement->table_location) + ",\"schema\":" + (statement->schema ? schema_json(*statement->schema) : "null") + "}";
}

std::string plan_json(const PlanPtr& plan) {
  if (const auto p = std::dynamic_pointer_cast<const CreateTablePlan>(plan)) return "{\"node\":\"CreateTablePlan\",\"schema\":" + schema_json(p->schema) + "}";
  if (const auto p = std::dynamic_pointer_cast<const InsertPlan>(plan)) return "{\"node\":\"InsertPlan\",\"table_name\":" + json_quote(p->table_name) + ",\"columns\":" + strings_json(p->columns) + ",\"values\":[" + join(p->values, ",", expression_json) + "]}";
  if (const auto p = std::dynamic_pointer_cast<const SeqScanPlan>(plan)) return "{\"node\":\"SeqScanPlan\",\"table_name\":" + json_quote(p->table_name) + ",\"schema\":" + schema_json(p->schema) + "}";
  if (const auto p = std::dynamic_pointer_cast<const FilterPlan>(plan)) return "{\"node\":\"FilterPlan\",\"predicate\":" + expression_json(p->predicate) + ",\"child\":" + plan_json(p->child) + "}";
  if (const auto p = std::dynamic_pointer_cast<const ProjectPlan>(plan)) return "{\"node\":\"ProjectPlan\",\"columns\":" + strings_json(p->columns) + ",\"column_indices\":[" + join(p->column_indices, ",", [](std::size_t i) { return std::to_string(i); }) + "],\"child\":" + plan_json(p->child) + "}";
  if (const auto p = std::dynamic_pointer_cast<const DeletePlan>(plan)) return "{\"node\":\"DeletePlan\",\"table_name\":" + json_quote(p->table_name) + ",\"child\":" + plan_json(p->child) + "}";
  throw DBError(Stage::Internal, "INVALID_PLAN", "Unknown plan node.");
}

std::string tokens_json(const std::vector<Token>& tokens) {
  return "[" + join(tokens, ",", [](const Token& token) {
    return "{\"type\":" + json_quote(token.type) + ",\"lexeme\":" + json_quote(token.lexeme) + ",\"line\":" + std::to_string(token.location.line) + ",\"column\":" + std::to_string(token.location.column) + "}";
  }) + "]";
}
std::string compilation_json(const CompilationResult& result) {
  return "{\"tokens\":" + tokens_json(result.tokens) + ",\"ast\":" + ast_json(result.ast) + ",\"bound_ast\":" + ast_json(result.bound_ast) +
    ",\"semantic\":\"PASS\",\"plan_before\":" + plan_json(result.plan_before) + ",\"plan_after\":" + plan_json(result.plan_after) + "}";
}
std::string error_json(const DBError& error) {
  return "{\"stage\":" + json_quote(stage_name(error.stage)) + ",\"code\":" + json_quote(error.code) + ",\"message\":" + json_quote(error.message) +
    ",\"location\":" + location_json(error.location) + ",\"actual\":" + json_quote(error.actual) + ",\"expected\":" + strings_json(error.expected) + "}";
}
std::string format_tokens(const std::vector<Token>& tokens) {
  return join(tokens, "\n", [](const Token& t) { return t.type + "\t" + json_quote(t.lexeme) + "\t(" + std::to_string(t.location.line) + "," + std::to_string(t.location.column) + ")"; });
}
std::string format_plan(const PlanPtr& plan) { return plan_text(plan, 0); }
std::string format_ast(const StmtPtr& statement) { return ast_json(statement); }
}  // namespace minisql
