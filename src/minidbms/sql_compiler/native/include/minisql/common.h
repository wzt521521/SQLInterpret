#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace minisql {

struct SourceLocation {
  std::size_t line{1};
  std::size_t column{1};
};

enum class Stage { Lexical, Syntax, Semantic, Execution, Internal };
std::string stage_name(Stage stage);

class DBError : public std::runtime_error {
 public:
  DBError(Stage stage, std::string code, std::string message,
          SourceLocation location = {}, std::string actual = {},
          std::vector<std::string> expected = {});
  Stage stage;
  std::string code;
  std::string message;
  SourceLocation location;
  std::string actual;
  std::vector<std::string> expected;
};

enum class DataType { Unknown, Int, Varchar, Bool };
std::string type_name(DataType type);
using Value = std::variant<std::int64_t, std::string, bool>;
using Row = std::vector<Value>;
std::string value_text(const Value& value);
std::string lower_ascii(std::string text);

struct ColumnDef {
  std::string name;
  DataType data_type{DataType::Unknown};
};

struct TableSchema {
  std::string table_name;
  std::vector<ColumnDef> columns;
};

class CatalogView {
 public:
  virtual ~CatalogView() = default;
  virtual bool table_exists(const std::string& table_name) const = 0;
  virtual TableSchema get_schema(const std::string& table_name) const = 0;
};

struct ExecutionResult {
  std::vector<std::string> columns;
  std::vector<Row> rows;
  std::size_t affected_rows{0};
  std::string message;
};

}  // namespace minisql
