#pragma once

#include "minisql/ast.h"
#include "minisql/lexer.h"
#include <map>
#include <set>

namespace minisql {

struct Production {
  std::string lhs;
  std::vector<std::string> rhs;  // Empty vector is the empty production.
};

using SymbolSet = std::set<std::string>;
using SetMap = std::map<std::string, SymbolSet>;
using PredictTable = std::map<std::string, std::map<std::string, std::size_t>>;

class Grammar {
 public:
  Grammar(std::vector<Production> productions, std::string start);
  const SetMap& first() const { return first_; }
  const SetMap& follow() const { return follow_; }
  const PredictTable& table() const { return table_; }
  const std::vector<Production>& productions() const { return productions_; }
  const std::string& start() const { return start_; }
  std::string dump() const;
 private:
  std::vector<Production> productions_;
  std::string start_;
  SetMap first_;
  SetMap follow_;
  PredictTable table_;
};

struct ParseResult {
  std::vector<StmtPtr> statements;
  std::vector<std::string> trace;
};

class LL1Parser {
 public:
  LL1Parser();
  const Grammar& grammar() const { return grammar_; }
  ParseResult parse(const std::vector<Token>& tokens, bool trace = false) const;
 private:
  Grammar grammar_;
};

}  // namespace minisql
