// Private subprocess protocol used by the Python adapter. SQL is never rebuilt
// from Catalog values: schema metadata is transported as length-prefixed UTF-8.
#include "minisql/format.h"
#include "session_catalog.h"
#include <iostream>
#include <limits>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace minisql;
namespace {
std::size_t number() {
  std::string line;
  if (!std::getline(std::cin, line) || line.empty() || line.size() > 10 ||
      line.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error("Invalid bridge length");
  const auto value = std::stoull(line);
  if (value > 16'000'000) throw std::runtime_error("Bridge request too large");
  return static_cast<std::size_t>(value);
}
std::string string() {
  std::string value(number(), '\0');
  if (!std::cin.read(value.data(), static_cast<std::streamsize>(value.size())))
    throw std::runtime_error("Truncated bridge request");
  return value;
}
DataType type() {
  const auto name = string();
  if (name == "INT") return DataType::Int;
  if (name == "VARCHAR") return DataType::Varchar;
  if (name == "BOOL") return DataType::Bool;
  return DataType::Unknown;
}
Value value() {
  const auto kind = type();
  const auto text = string();
  if (kind == DataType::Int) return static_cast<std::int64_t>(std::stoll(text));
  if (kind == DataType::Bool) return text == "true";
  if (kind == DataType::Varchar) return text;
  throw std::runtime_error("Invalid scalar type");
}
ExprPtr expression(std::size_t depth = 0) {
  if (depth > 128) throw std::runtime_error("Bridge expression too deep");
  const auto kind = string();
  const auto data_type = type();
  const SourceLocation location{number(), number()};
  std::shared_ptr<Expr> result;
  if (kind == "LiteralExpr") result = std::make_shared<LiteralExpr>(value(), location);
  else if (kind == "IdentifierExpr") {
    auto id = std::make_shared<IdentifierExpr>(string(), location);
    id->table_name = string();
    id->column_index = number();
    result = id;
  } else if (kind == "UnaryExpr") {
    const auto op = string();
    result = std::make_shared<UnaryExpr>(op, expression(depth + 1), location);
  } else if (kind == "BinaryExpr") {
    const auto op = string();
    const auto left = expression(depth + 1);
    result = std::make_shared<BinaryExpr>(op, left, expression(depth + 1), location);
  } else throw std::runtime_error("Invalid expression node");
  result->data_type = data_type;
  return result;
}
std::vector<std::string> strings() {
  std::vector<std::string> result;
  const auto count = number();
  for (std::size_t i = 0; i < count; ++i) result.push_back(string());
  return result;
}
TableSchema schema() {
  TableSchema result;
  result.table_name = string();
  const auto count = number();
  for (std::size_t i = 0; i < count; ++i) {
    const auto name = string();
    result.columns.push_back({name, type()});
  }
  return result;
}
PlanPtr plan(std::size_t depth = 0) {
  if (depth > 128) throw std::runtime_error("Bridge plan too deep");
  const auto kind = string();
  if (kind == "CreateTablePlan") {
    auto p = std::make_shared<CreateTablePlan>(); p->schema = schema(); return p;
  }
  if (kind == "InsertPlan") {
    auto p = std::make_shared<InsertPlan>(); p->table_name = string(); p->columns = strings();
    const auto count = number();
    for (std::size_t i = 0; i < count; ++i) p->values.push_back(expression());
    return p;
  }
  if (kind == "SeqScanPlan") {
    auto p = std::make_shared<SeqScanPlan>(); p->table_name = string(); return p;
  }
  if (kind == "FilterPlan") {
    auto p = std::make_shared<FilterPlan>(); p->predicate = expression(); p->child = plan(depth + 1); return p;
  }
  if (kind == "ProjectPlan") {
    auto p = std::make_shared<ProjectPlan>(); p->columns = strings(); p->child = plan(depth + 1); return p;
  }
  if (kind == "DeletePlan") {
    auto p = std::make_shared<DeletePlan>(); p->table_name = string(); p->child = plan(depth + 1); return p;
  }
  throw std::runtime_error("Invalid plan node");
}
std::string run(const std::string& operation) {
  if (operation == "optimize") return plan_json(optimize_plan(plan()));
  if (operation == "evaluate") {
    const auto expr = expression();
    Row row;
    const auto count = number();
    for (std::size_t i = 0; i < count; ++i) row.push_back(value());
    auto literal = std::make_shared<LiteralExpr>(evaluate_expression(expr, row), expr->location);
    literal->data_type = expr->data_type;
    return expression_json(literal);
  }
  const auto sql = string();
  const auto tokens = tokenize(sql);
  if (operation == "tokens") return tokens_json(tokens);
  const auto parsed = LL1Parser().parse(tokens);
  if (operation == "parse") {
    std::string output = "[";
    for (const auto& statement : parsed.statements) {
      if (output.size() > 1) output += ',';
      output += ast_json(statement);
    }
    return output + "]";
  }
  if (operation != "compile") throw std::runtime_error("Unknown bridge operation");
  app::SessionCatalog catalog;
  const auto count = number();
  for (std::size_t i = 0; i < count; ++i) catalog.register_schema(schema());
  const bool optimize = number() != 0;
  std::string output = "[";
  for (const auto& detail : Compiler().compile_detailed(sql, catalog, optimize)) {
    if (output.size() > 1) output += ',';
    output += compilation_json(detail);
  }
  return output + "]";
}
}  // namespace
int main(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  try {
    if (argc != 2) throw std::runtime_error("Expected bridge operation");
    const auto result = run(argv[1]);
    std::cout << "{\"result\":" << result << ",\"error\":null}";
    return 0;
  } catch (const DBError& error) {
    std::cout << "{\"result\":null,\"error\":" << error_json(error) << '}';
    return 1;
  } catch (const std::exception& error) {
    std::cout << "{\"result\":null,\"error\":" << error_json(DBError(
        Stage::Internal, "BRIDGE_FAILURE", error.what())) << '}';
    return 1;
  }
}
