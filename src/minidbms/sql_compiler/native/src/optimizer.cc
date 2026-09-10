#include "minisql/execution.h"

namespace minisql {
namespace {

bool boolean_literal(const ExprPtr& expression, bool value) {
  const auto literal = std::dynamic_pointer_cast<const LiteralExpr>(expression);
  return literal && literal->data_type == DataType::Bool &&
      std::holds_alternative<bool>(literal->value) && std::get<bool>(literal->value) == value;
}

ExprPtr make_literal(Value value, const Expr& original) {
  auto literal = std::make_shared<LiteralExpr>(std::move(value), original.location);
  literal->data_type = original.data_type;
  return literal;
}

ExprPtr fold_constant(const ExprPtr& expression) {
  try {
    return make_literal(evaluate_expression(expression, {}), *expression);
  } catch (const DBError& error) {
    // Checked arithmetic can fail when execution reaches this expression.
    // Keep the operation in the plan, preserving error timing and short circuit.
    if (error.stage == Stage::Execution && error.code == "INTEGER_OVERFLOW") return expression;
    throw;
  }
}

}  // namespace

ExprPtr optimize_expression(const ExprPtr& expression) {
  if (!expression || expression->data_type == DataType::Unknown) {
    throw DBError(Stage::Internal, "UNBOUND_EXPRESSION", "Optimizer requires a semantically bound expression.",
                  expression ? expression->location : SourceLocation{});
  }
  if (const auto unary = std::dynamic_pointer_cast<const UnaryExpr>(expression)) {
    auto result = std::make_shared<UnaryExpr>(*unary);
    result->operand = optimize_expression(unary->operand);
    if (std::dynamic_pointer_cast<const LiteralExpr>(result->operand)) return fold_constant(result);
    const auto inner = std::dynamic_pointer_cast<const UnaryExpr>(result->operand);
    if (unary->op == "NOT" && inner && inner->op == "NOT") return inner->operand;
    return result;
  }
  if (const auto binary = std::dynamic_pointer_cast<const BinaryExpr>(expression)) {
    auto result = std::make_shared<BinaryExpr>(*binary);
    result->left = optimize_expression(binary->left);
    // The executor evaluates from left to right. These rules remove only a
    // right operand that would never be evaluated by the original expression.
    if (binary->op == "AND" && boolean_literal(result->left, false)) {
      return make_literal(false, *binary);
    }
    if (binary->op == "OR" && boolean_literal(result->left, true)) {
      return make_literal(true, *binary);
    }
    result->right = optimize_expression(binary->right);
    if (std::dynamic_pointer_cast<const LiteralExpr>(result->left) &&
        std::dynamic_pointer_cast<const LiteralExpr>(result->right)) {
      return fold_constant(result);
    }
    if (binary->op == "AND") {
      if (boolean_literal(result->left, true)) return result->right;
      if (boolean_literal(result->right, true)) return result->left;
    }
    if (binary->op == "OR") {
      if (boolean_literal(result->left, false)) return result->right;
      if (boolean_literal(result->right, false)) return result->left;
    }
    // Do not apply p AND FALSE -> FALSE or p OR TRUE -> TRUE: p may overflow,
    // and removing its evaluation would change observable execution behavior.
    return result;
  }
  if (std::dynamic_pointer_cast<const LiteralExpr>(expression) ||
      std::dynamic_pointer_cast<const IdentifierExpr>(expression)) {
    return expression;  // All callers retain only shared_ptr<const Expr>.
  }
  throw DBError(Stage::Internal, "UNSUPPORTED_EXPRESSION", "Unsupported optimizer expression node.", expression->location);
}

PlanPtr optimize_plan(const PlanPtr& plan) {
  if (!plan) throw DBError(Stage::Internal, "UNSUPPORTED_PLAN", "Null plan node.");
  if (const auto filter = std::dynamic_pointer_cast<const FilterPlan>(plan)) {
    const auto predicate = optimize_expression(filter->predicate);
    const auto child = optimize_plan(filter->child);
    if (boolean_literal(predicate, true)) return child;
    auto result = std::make_shared<FilterPlan>();
    result->predicate = predicate;
    result->child = child;
    // Keep Filter[FALSE] so the execution interface needs no extra Empty node.
    return result;
  }
  if (const auto project = std::dynamic_pointer_cast<const ProjectPlan>(plan)) {
    auto result = std::make_shared<ProjectPlan>(*project);
    result->child = optimize_plan(project->child);
    return result;
  }
  if (const auto deletion = std::dynamic_pointer_cast<const DeletePlan>(plan)) {
    auto result = std::make_shared<DeletePlan>(*deletion);
    result->child = optimize_plan(deletion->child);
    return result;
  }
  if (const auto insert = std::dynamic_pointer_cast<const InsertPlan>(plan)) {
    auto result = std::make_shared<InsertPlan>(*insert);
    result->values.clear();
    for (const auto& value : insert->values) result->values.push_back(optimize_expression(value));
    return result;
  }
  if (std::dynamic_pointer_cast<const CreateTablePlan>(plan) ||
      std::dynamic_pointer_cast<const SeqScanPlan>(plan)) return plan;
  throw DBError(Stage::Internal, "UNSUPPORTED_PLAN", "Unsupported optimizer plan node.");
}

}  // namespace minisql
