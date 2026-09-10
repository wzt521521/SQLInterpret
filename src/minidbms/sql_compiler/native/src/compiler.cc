#include "minisql/compiler.h"

namespace minisql {
ParseResult Compiler::prepare(const std::string& sql, bool trace) const {
  return LL1Parser().parse(tokenize(sql), trace);
}

CompilationResult Compiler::compile_statement(const StmtPtr& statement, const CatalogView& catalog, bool optimize) const {
  const auto bound = SemanticAnalyzer(catalog).analyze(statement);
  const auto before = build_plan(bound);
  return {statement, bound, before, optimize ? optimize_plan(before) : before, {}};
}

std::vector<CompilationResult> Compiler::compile_detailed(const std::string& sql, const CatalogView& catalog, bool optimize) const {
  const auto tokens = tokenize(sql);
  const auto parsed = LL1Parser().parse(tokens);
  std::vector<CompilationResult> results;
  std::size_t start = 0;
  for (const auto& statement : parsed.statements) {
    auto detail = compile_statement(statement, catalog, optimize);
    std::size_t end = start;
    while (end < tokens.size() && tokens[end].type != ";") ++end;
    if (end < tokens.size()) ++end;
    detail.tokens.assign(tokens.begin() + static_cast<std::ptrdiff_t>(start), tokens.begin() + static_cast<std::ptrdiff_t>(end));
    start = end;
    results.push_back(std::move(detail));
  }
  return results;
}

std::vector<PlanPtr> Compiler::compile(const std::string& sql, const CatalogView& catalog, bool optimize) const {
  std::vector<PlanPtr> plans;
  for (const auto& result : compile_detailed(sql, catalog, optimize)) plans.push_back(result.plan_after);
  return plans;
}

std::vector<ExecutionResult> Compiler::compile_and_execute(const std::string& sql, const CatalogView& catalog,
                                                          Executor& executor, bool optimize) const {
  const auto parsed = prepare(sql);
  std::vector<ExecutionResult> results;
  for (const auto& statement : parsed.statements) {
    // The next bind occurs only after execute has successfully updated the real Catalog.
    results.push_back(executor.execute(compile_statement(statement, catalog, optimize).plan_after));
  }
  return results;
}
}  // namespace minisql
