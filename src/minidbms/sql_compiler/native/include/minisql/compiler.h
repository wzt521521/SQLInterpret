#pragma once
#include "minisql/execution.h"
#include "minisql/ll1.h"
#include "minisql/semantic.h"

namespace minisql {
struct CompilationResult {
  StmtPtr ast;
  StmtPtr bound_ast;
  PlanPtr plan_before;
  PlanPtr plan_after;
  std::vector<Token> tokens;
};

class Compiler {
 public:
  ParseResult prepare(const std::string& sql, bool trace = false) const;
  CompilationResult compile_statement(const StmtPtr& statement, const CatalogView& catalog, bool optimize = true) const;
  std::vector<CompilationResult> compile_detailed(const std::string& sql, const CatalogView& catalog, bool optimize = true) const;
  std::vector<PlanPtr> compile(const std::string& sql, const CatalogView& catalog, bool optimize = true) const;
  std::vector<ExecutionResult> compile_and_execute(const std::string& sql, const CatalogView& catalog,
                                                  Executor& executor, bool optimize = true) const;
};
}  // namespace minisql
