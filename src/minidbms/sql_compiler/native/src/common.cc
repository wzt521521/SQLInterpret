#include "minisql/common.h"
#include <sstream>

namespace minisql {

std::string stage_name(Stage stage) {
  switch (stage) {
    case Stage::Lexical: return "LEXICAL";
    case Stage::Syntax: return "SYNTAX";
    case Stage::Semantic: return "SEMANTIC";
    case Stage::Execution: return "EXECUTION";
    case Stage::Internal: return "INTERNAL";
  }
  return "INTERNAL";
}

static std::string error_text(Stage stage, const std::string& code,
                              const std::string& message, SourceLocation location,
                              const std::string& actual, const std::vector<std::string>& expected) {
  auto text = "[" + stage_name(stage) + "/" + code + "] at line " +
         std::to_string(location.line) + ", column " + std::to_string(location.column) + ": " + message;
  if (!actual.empty()) text += " Actual: '" + actual + "'.";
  if (!expected.empty()) {
    text += " Expected:";
    for (const auto& symbol : expected) text += " '" + symbol + "'";
    text += ".";
  }
  return text;
}

DBError::DBError(Stage s, std::string c, std::string m, SourceLocation loc,
                 std::string a, std::vector<std::string> e)
    : std::runtime_error(error_text(s, c, m, loc, a, e)), stage(s), code(std::move(c)),
      message(std::move(m)), location(loc), actual(std::move(a)), expected(std::move(e)) {}

std::string type_name(DataType type) {
  switch (type) {
    case DataType::Int: return "INT";
    case DataType::Varchar: return "VARCHAR";
    case DataType::Bool: return "BOOL";
    case DataType::Unknown: return "UNBOUND";
  }
  return "UNBOUND";
}

std::string value_text(const Value& value) {
  if (const auto* integer = std::get_if<std::int64_t>(&value)) return std::to_string(*integer);
  if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? "TRUE" : "FALSE";
  std::string result = "'";
  for (char character : std::get<std::string>(value)) {
    result += character;
    if (character == '\'') result += '\'';
  }
  return result + "'";
}

std::string lower_ascii(std::string text) {
  for (char& character : text) {
    if (character >= 'A' && character <= 'Z') character = static_cast<char>(character + ('a' - 'A'));
  }
  return text;
}

}  // namespace minisql
