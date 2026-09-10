#include "minisql/ll1.h"

#include <algorithm>
#include <limits>
#include <sstream>

namespace minisql {
namespace {

constexpr const char* kEpsilon = "EPSILON";
constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kMaxExpressionDepth = 128;
constexpr std::size_t kMaxTraceLines = 20000;

bool merge_set(SymbolSet& target, const SymbolSet& source) {
  const auto old_size = target.size();
  target.insert(source.begin(), source.end());
  return target.size() != old_size;
}

// FIRST of an entire suffix, not just its first symbol. This is also used
// when computing FOLLOW and the SELECT set for a nullable production.
SymbolSet first_of(const std::vector<std::string>& sequence,
                   std::size_t begin, const SetMap& first) {
  SymbolSet result;
  for (std::size_t i = begin; i < sequence.size(); ++i) {
    const auto found = first.find(sequence[i]);
    if (found == first.end()) {
      result.insert(sequence[i]);
      return result;
    }
    for (const auto& symbol : found->second)
      if (symbol != kEpsilon) result.insert(symbol);
    if (!found->second.count(kEpsilon)) return result;
  }
  result.insert(kEpsilon);
  return result;
}

std::string production_text(const Production& production) {
  std::string text = production.lhs + " ->";
  if (production.rhs.empty()) return text + " EPSILON";
  for (const auto& symbol : production.rhs) text += " " + symbol;
  return text;
}

std::vector<Production> sql_productions() {
  return {
      {"Program", {"Statement", ";", "Program"}},
      {"Program", {}},
      {"Statement", {"Create"}},
      {"Statement", {"Insert"}},
      {"Statement", {"Select"}},
      {"Statement", {"Delete"}},
      {"Create", {"CREATE", "TABLE", "IDENTIFIER", "(", "ColumnDef", "ColumnDefTail", ")"}},
      {"ColumnDef", {"IDENTIFIER", "TypeName"}},
      {"ColumnDefTail", {",", "ColumnDef", "ColumnDefTail"}},
      {"ColumnDefTail", {}},
      {"TypeName", {"INT"}},
      {"TypeName", {"VARCHAR"}},
      {"TypeName", {"BOOL"}},
      {"TypeName", {"IDENTIFIER"}},
      {"Insert", {"INSERT", "INTO", "IDENTIFIER", "InsertColumns", "VALUES", "(", "Expr", "ExprListTail", ")"}},
      {"InsertColumns", {"(", "ColumnList", ")"}},
      {"InsertColumns", {}},
      {"Select", {"SELECT", "SelectList", "FROM", "IDENTIFIER", "WhereOpt"}},
      {"SelectList", {"*"}},
      {"SelectList", {"ColumnList"}},
      {"Delete", {"DELETE", "FROM", "IDENTIFIER", "WhereOpt"}},
      {"ColumnList", {"IDENTIFIER", "ColumnListTail"}},
      {"ColumnListTail", {",", "IDENTIFIER", "ColumnListTail"}},
      {"ColumnListTail", {}},
      {"WhereOpt", {"WHERE", "Expr"}},
      {"WhereOpt", {}},
      {"ExprListTail", {",", "Expr", "ExprListTail"}},
      {"ExprListTail", {}},
      {"Expr", {"And", "OrTail"}},
      {"OrTail", {"OR", "And", "OrTail"}},
      {"OrTail", {}},
      {"And", {"Not", "AndTail"}},
      {"AndTail", {"AND", "Not", "AndTail"}},
      {"AndTail", {}},
      {"Not", {"NOT", "Not"}},
      {"Not", {"Comparison"}},
      {"Comparison", {"Add", "ComparisonTail"}},
      {"ComparisonTail", {"CompareOp", "Add"}},
      {"ComparisonTail", {}},
      {"CompareOp", {"="}},
      {"CompareOp", {"!="}},
      {"CompareOp", {">"}},
      {"CompareOp", {">="}},
      {"CompareOp", {"<"}},
      {"CompareOp", {"<="}},
      {"Add", {"Multiply", "AddTail"}},
      {"AddTail", {"+", "Multiply", "AddTail"}},
      {"AddTail", {"-", "Multiply", "AddTail"}},
      {"AddTail", {}},
      {"Multiply", {"Unary", "MultiplyTail"}},
      {"MultiplyTail", {"*", "Unary", "MultiplyTail"}},
      {"MultiplyTail", {}},
      {"Unary", {"+", "Unary"}},
      {"Unary", {"-", "Unary"}},
      {"Unary", {"Primary"}},
      {"Primary", {"INTEGER"}},
      {"Primary", {"STRING"}},
      {"Primary", {"TRUE"}},
      {"Primary", {"FALSE"}},
      {"Primary", {"IDENTIFIER"}},
      {"Primary", {"(", "Expr", ")"}},
  };
}

// A flat concrete syntax tree. Children hold indexes, so a long Program or
// comma list neither requires recursive destruction nor copies a tail list.
struct SyntaxNode {
  std::string symbol;
  std::vector<std::size_t> children;
  std::size_t token{kNoIndex};
};

struct ExprValue {
  ExprPtr expression;
  std::size_t depth{0};
};

[[noreturn]] void too_complex(SourceLocation location) {
  throw DBError(Stage::Syntax, "EXPRESSION_TOO_COMPLEX",
                "Expression AST depth or parenthesis/unary nesting exceeds 128",
                location);
}

// Syntax recognition is already complete before this bottom-up pass runs.
// This class only executes AST construction actions over the flat CST; it
// never consumes tokens, chooses productions or recursively parses input.
class AstBuilder {
 public:
  AstBuilder(const std::vector<SyntaxNode>& nodes,
             const std::vector<Token>& tokens)
      : nodes_(nodes), tokens_(tokens), values_(nodes.size()) {}

  std::vector<StmtPtr> build() {
    // Each child's index is greater than its parent's. Reverse index order
    // therefore provides a nonrecursive postorder traversal.
    for (std::size_t i = nodes_.size(); i-- > 0;) build_expression(i);
    std::vector<StmtPtr> result;
    std::size_t program = 0;
    while (!nodes_[program].children.empty()) {
      result.push_back(build_statement(child(child(program, 0), 0)));
      program = child(program, 2);
    }
    return result;
  }

 private:
  std::size_t child(std::size_t node, std::size_t index) const {
    return nodes_[node].children[index];
  }
  const Token& token(std::size_t node) const {
    return tokens_[nodes_[node].token];
  }
  ExprValue binary(const Token& op, ExprValue left, ExprValue right) const {
    const auto depth = 1 + std::max(left.depth, right.depth);
    if (depth > kMaxExpressionDepth) too_complex(op.location);
    return {std::make_shared<BinaryExpr>(op.type, std::move(left.expression),
                                        std::move(right.expression), op.location), depth};
  }
  ExprValue unary(const Token& op, ExprValue operand) const {
    const auto depth = 1 + operand.depth;
    if (depth > kMaxExpressionDepth) too_complex(op.location);
    return {std::make_shared<UnaryExpr>(op.type, std::move(operand.expression),
                                       op.location), depth};
  }
  void build_expression(std::size_t node) {
    const auto& current = nodes_[node];
    const auto& symbol = current.symbol;
    if (symbol == "Primary") {
      const auto& first = token(child(node, 0));
      if (first.type == "(") {
        values_[node] = values_[child(node, 1)];
      } else if (first.type == "IDENTIFIER") {
        values_[node] = {std::make_shared<IdentifierExpr>(
                             lower_ascii(first.lexeme), first.location), 1};
      } else if (first.type == "INTEGER") {
        // Preserve magnitude text so semantic analysis can accept INT64_MIN
        // under a unary minus without first overflowing a positive int64.
        values_[node] = {std::make_shared<LiteralExpr>(
                             std::int64_t{0}, first.location, first.lexeme), 1};
      } else if (first.type == "STRING") {
        values_[node] = {std::make_shared<LiteralExpr>(first.decoded, first.location), 1};
      } else {
        values_[node] = {std::make_shared<LiteralExpr>(first.type == "TRUE", first.location), 1};
      }
    } else if (symbol == "Unary" || symbol == "Not") {
      values_[node] = current.children.size() == 1
          ? values_[child(node, 0)]
          : unary(token(child(node, 0)), values_[child(node, 1)]);
    } else if (symbol == "Expr" || symbol == "And" ||
               symbol == "Add" || symbol == "Multiply") {
      auto value = values_[child(node, 0)];
      auto tail = child(node, 1);
      // The grammar tail is right-recursive; this left fold deliberately
      // constructs ((a - b) - c), preserving SQL arithmetic associativity.
      while (!nodes_[tail].children.empty()) {
        value = binary(token(child(tail, 0)), std::move(value),
                       values_[child(tail, 1)]);
        tail = child(tail, 2);
      }
      values_[node] = std::move(value);
    } else if (symbol == "Comparison") {
      auto value = values_[child(node, 0)];
      const auto tail = child(node, 1);
      if (!nodes_[tail].children.empty())
        value = binary(token(child(child(tail, 0), 0)), std::move(value),
                       values_[child(tail, 1)]);
      values_[node] = std::move(value);
    }
  }

  std::shared_ptr<const IdentifierExpr> identifier(std::size_t node) const {
    const auto& source = token(node);
    return std::make_shared<IdentifierExpr>(lower_ascii(source.lexeme), source.location);
  }
  std::vector<std::shared_ptr<const IdentifierExpr>> columns(std::size_t node) const {
    std::vector<std::shared_ptr<const IdentifierExpr>> result;
    result.push_back(identifier(child(node, 0)));
    auto tail = child(node, 1);
    while (!nodes_[tail].children.empty()) {
      result.push_back(identifier(child(tail, 1)));
      tail = child(tail, 2);
    }
    return result;
  }
  ColumnSpec column_spec(std::size_t node) const {
    const auto& name = token(child(node, 0));
    const auto& type = token(child(child(node, 1), 0));
    return {lower_ascii(name.lexeme), type.lexeme, name.location, type.location};
  }
  void common(Statement& statement, std::size_t node,
              std::size_t table_index) const {
    const auto& table = token(child(node, table_index));
    statement.location = token(child(node, 0)).location;
    statement.table_name = lower_ascii(table.lexeme);
    statement.table_location = table.location;
  }
  ExprPtr where(std::size_t node) const {
    return nodes_[node].children.empty() ? nullptr
                                       : values_[child(node, 1)].expression;
  }
  StmtPtr build_statement(std::size_t node) const {
    const auto& symbol = nodes_[node].symbol;
    if (symbol == "Create") {
      auto statement = std::make_shared<CreateTableStmt>();
      common(*statement, node, 2);
      statement->columns.push_back(column_spec(child(node, 4)));
      auto tail = child(node, 5);
      while (!nodes_[tail].children.empty()) {
        statement->columns.push_back(column_spec(child(tail, 1)));
        tail = child(tail, 2);
      }
      return statement;
    }
    if (symbol == "Insert") {
      auto statement = std::make_shared<InsertStmt>();
      common(*statement, node, 2);
      const auto optional = child(node, 3);
      statement->explicit_columns = !nodes_[optional].children.empty();
      if (statement->explicit_columns) statement->columns = columns(child(optional, 1));
      statement->values.push_back(values_[child(node, 6)].expression);
      auto tail = child(node, 7);
      while (!nodes_[tail].children.empty()) {
        statement->values.push_back(values_[child(tail, 1)].expression);
        tail = child(tail, 2);
      }
      return statement;
    }
    if (symbol == "Select") {
      auto statement = std::make_shared<SelectStmt>();
      common(*statement, node, 3);
      const auto selection = child(child(node, 1), 0);
      statement->select_all = nodes_[selection].symbol == "*";
      if (!statement->select_all) statement->columns = columns(selection);
      statement->where = where(child(node, 4));
      return statement;
    }
    auto statement = std::make_shared<DeleteStmt>();
    common(*statement, node, 2);
    statement->where = where(child(node, 3));
    return statement;
  }

  const std::vector<SyntaxNode>& nodes_;
  const std::vector<Token>& tokens_;
  std::vector<ExprValue> values_;
};

}  // namespace

Grammar::Grammar(std::vector<Production> productions, std::string start)
    : productions_(std::move(productions)), start_(std::move(start)) {
  for (const auto& production : productions_) {
    if (production.lhs.empty() || production.lhs == kEpsilon || production.lhs == "EOF")
      throw DBError(Stage::Internal, "INVALID_GRAMMAR", "Invalid nonterminal name");
    first_[production.lhs];
    follow_[production.lhs];
    table_[production.lhs];
    for (const auto& symbol : production.rhs)
      if (symbol.empty() || symbol == kEpsilon)
        throw DBError(Stage::Internal, "INVALID_GRAMMAR", "Use an empty RHS for EPSILON");
  }
  if (!first_.count(start_))
    throw DBError(Stage::Internal, "INVALID_GRAMMAR", "Start symbol has no production");

  bool changed;
  do {
    changed = false;
    for (const auto& production : productions_)
      changed = merge_set(first_[production.lhs], first_of(production.rhs, 0, first_)) || changed;
  } while (changed);

  follow_[start_].insert("EOF");
  do {
    changed = false;
    for (const auto& production : productions_) {
      for (std::size_t i = 0; i < production.rhs.size(); ++i) {
        auto found = follow_.find(production.rhs[i]);
        if (found == follow_.end()) continue;
        auto suffix_first = first_of(production.rhs, i + 1, first_);
        const bool nullable = suffix_first.erase(kEpsilon) != 0;
        changed = merge_set(found->second, suffix_first) || changed;
        if (nullable) changed = merge_set(found->second, follow_[production.lhs]) || changed;
      }
    }
  } while (changed);

  for (std::size_t i = 0; i < productions_.size(); ++i) {
    const auto& production = productions_[i];
    auto select = first_of(production.rhs, 0, first_);
    if (select.erase(kEpsilon)) merge_set(select, follow_[production.lhs]);
    for (const auto& terminal : select) {
      auto& row = table_[production.lhs];
      const auto previous = row.find(terminal);
      if (previous != row.end())
        throw DBError(Stage::Internal, "GRAMMAR_CONFLICT",
                      "LL(1) conflict at [" + production.lhs + ", " + terminal + "]: " +
                          production_text(productions_[previous->second]) + " / " +
                          production_text(production));
      row.emplace(terminal, i);
    }
  }
}

std::string Grammar::dump() const {
  std::ostringstream out;
  out << "LL(1) grammar: " << first_.size() << " nonterminals, "
      << productions_.size() << " productions; no prediction conflicts\n";
  out << "Start: " << start_ << "; EPSILON = empty production; EOF = end of input\n\n";
  for (std::size_t i = 0; i < productions_.size(); ++i)
    out << '[' << i << "] " << production_text(productions_[i]) << '\n';
  const std::pair<const char*, const SetMap*> set_kinds[] = {
      {"FIRST", &first_}, {"FOLLOW", &follow_}};
  for (const auto& set_kind : set_kinds) {
    out << '\n';
    for (const auto& entry : *set_kind.second) {
      out << set_kind.first << '(' << entry.first << ") = {";
      bool comma = false;
      for (const auto& symbol : entry.second) {
        if (comma) out << ", ";
        out << symbol;
        comma = true;
      }
      out << "}\n";
    }
  }
  out << "\nPredictive parsing table (missing entries are syntax errors):\n";
  for (const auto& row : table_)
    for (const auto& entry : row.second)
      out << "M[" << row.first << ", " << entry.first << "] = "
          << entry.second << " : " << production_text(productions_[entry.second]) << '\n';
  return out.str();
}

LL1Parser::LL1Parser() : grammar_(sql_productions(), "Program") {}

ParseResult LL1Parser::parse(const std::vector<Token>& tokens, bool trace) const {
  if (tokens.empty() || tokens.back().type != "EOF")
    throw DBError(Stage::Internal, "INVALID_TOKEN_STREAM", "Token stream must end with EOF");
  struct StackEntry {
    std::string symbol;
    std::size_t node;
    std::size_t nesting;
  };
  std::vector<SyntaxNode> nodes{{grammar_.start(), {}, kNoIndex}};
  std::vector<StackEntry> stack{{"EOF", kNoIndex, 0}, {grammar_.start(), 0, 0}};
  ParseResult result;
  std::size_t cursor = 0;
  auto record = [&](const std::string& action) {
    if (!trace || result.trace.size() > kMaxTraceLines) return;
    if (result.trace.size() == kMaxTraceLines) {
      result.trace.push_back("[trace truncated after 20000 steps; parsing continues]");
      return;
    }
    std::ostringstream line;
    line << "stack(top first)=[";
    // A trace must not turn a linear parser into quadratic output on a long
    // expression. Show the nearest 24 stack symbols and the full stack size.
    std::size_t shown = 0;
    for (auto it = stack.rbegin(); it != stack.rend() && shown < 24; ++it, ++shown) {
      if (shown) line << ' ';
      line << it->symbol;
    }
    if (stack.size() > shown) line << " ... (" << stack.size() << " symbols)";
    const auto& lookahead = tokens[cursor];
    line << "] lookahead=" << lookahead.type << "@" << lookahead.location.line
         << ':' << lookahead.location.column << " action=" << action;
    result.trace.push_back(line.str());
  };
  auto unexpected = [&](std::vector<std::string> expected) {
    const auto& actual = tokens[cursor];
    throw DBError(Stage::Syntax, "UNEXPECTED_TOKEN", "Unexpected token while parsing SQL",
                  actual.location, actual.type == "EOF" ? "EOF" : actual.lexeme,
                  std::move(expected));
  };
  while (!stack.empty()) {
    if (cursor >= tokens.size())
      throw DBError(Stage::Internal, "INVALID_TOKEN_STREAM", "Unexpected end of token stream");
    const auto entry = stack.back();
    const auto row = grammar_.table().find(entry.symbol);
    if (row == grammar_.table().end()) {
      if (tokens[cursor].type != entry.symbol) unexpected({entry.symbol});
      record("match " + entry.symbol);
      stack.pop_back();
      if (entry.node != kNoIndex) nodes[entry.node].token = cursor;
      ++cursor;
      continue;
    }
    const auto cell = row->second.find(tokens[cursor].type);
    if (cell == row->second.end()) {
      std::vector<std::string> expected;
      for (const auto& available : row->second) expected.push_back(available.first);
      unexpected(std::move(expected));
    }
    const auto& production = grammar_.productions()[cell->second];
    auto nesting = entry.nesting;
    const bool nested =
        (entry.symbol == "Primary" && !production.rhs.empty() && production.rhs.front() == "(") ||
        ((entry.symbol == "Unary" || entry.symbol == "Not") && production.rhs.size() == 2);
    if (nested && ++nesting > kMaxExpressionDepth) too_complex(tokens[cursor].location);
    record("[" + std::to_string(cell->second) + "] " + production_text(production));
    stack.pop_back();
    std::vector<std::size_t> children;
    children.reserve(production.rhs.size());
    for (const auto& symbol : production.rhs) {
      children.push_back(nodes.size());
      nodes.push_back({symbol, {}, kNoIndex});
    }
    nodes[entry.node].children = std::move(children);
    for (std::size_t i = production.rhs.size(); i-- > 0;)
      stack.push_back({production.rhs[i], nodes[entry.node].children[i], nesting});
  }
  if (cursor != tokens.size())
    throw DBError(Stage::Syntax, "UNEXPECTED_TOKEN", "Tokens after EOF are not allowed",
                  tokens[cursor].location, tokens[cursor].lexeme, {});
  result.statements = AstBuilder(nodes, tokens).build();
  return result;
}

}  // namespace minisql
