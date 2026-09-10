#pragma once
#include "minisql/compiler.h"

namespace minisql {
std::string json_quote(const std::string& value);
std::string format_expression(const ExprPtr& expression);
std::string format_plan(const PlanPtr& plan);
std::string format_ast(const StmtPtr& statement);
std::string format_tokens(const std::vector<Token>& tokens);
std::string expression_json(const ExprPtr& expression);
std::string ast_json(const StmtPtr& statement);
std::string plan_json(const PlanPtr& plan);
std::string tokens_json(const std::vector<Token>& tokens);
std::string compilation_json(const CompilationResult& result);
std::string error_json(const DBError& error);
}  // namespace minisql
