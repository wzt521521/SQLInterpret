#pragma once

#include "minisql/common.h"
#include <string_view>

namespace minisql {

struct Token {
  std::string type;
  std::string lexeme;
  SourceLocation location;
  std::string decoded;  // Decoded string literal; other kinds retain lexeme.
};

class Lexer {
 public:
  explicit Lexer(std::string_view source) : source_(source) {}
  std::vector<Token> tokenize() const;
 private:
  // Own the input so a lexer constructed from a temporary string stays valid.
  std::string source_;
};

std::vector<Token> tokenize(std::string_view source);

}  // namespace minisql
