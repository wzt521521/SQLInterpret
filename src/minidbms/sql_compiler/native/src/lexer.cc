#include "minisql/lexer.h"
#include <set>

namespace minisql {
namespace {
bool letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool identifier(char c) { return letter(c) || digit(c); }
std::string upper(std::string value) {
  for (char& c : value) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return value;
}
const std::set<std::string> keywords = {
  "CREATE", "TABLE", "INSERT", "INTO", "VALUES", "SELECT", "FROM", "WHERE", "DELETE",
  "INT", "VARCHAR", "BOOL", "TRUE", "FALSE", "AND", "OR", "NOT"
};
const std::set<std::string> symbols = {"(", ")", ",", ";", "+", "-", "*", "=", "!=", ">", ">=", "<", "<="};

class Scanner {
 public:
  explicit Scanner(std::string_view text) : text_(text) {}
  std::vector<Token> run() {
    if (text_.size() > 1'000'000) fail("INPUT_TOO_LARGE", "SQL input exceeds 1000000 bytes.", {});
    validate_utf8();
    if (text_.substr(0, 3) == "\xef\xbb\xbf") position_ = 3;
    std::vector<Token> result;
    while (position_ < text_.size()) {
      char c = peek();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') { advance(); continue; }
      const auto start = position_;
      const auto location = location_;
      if (c == '-' && peek(1) == '-') {
        while (position_ < text_.size() && peek() != '\r' && peek() != '\n') advance();
        continue;
      }
      if (c == '/' && peek(1) == '*') {
        advance(); advance();
        while (position_ < text_.size() && !(peek() == '*' && peek(1) == '/')) advance();
        if (position_ == text_.size()) fail("UNTERMINATED_COMMENT", "Block comment requires */.", location);
        advance(); advance(); continue;
      }
      std::string type;
      std::string decoded;
      if (c == '\'') {
        advance();
        for (;;) {
          if (position_ == text_.size()) fail("UNTERMINATED_STRING", "String requires closing quote.", location);
          if (peek() == '\'') {
            advance();
            if (peek() != '\'') break;
            decoded += '\''; advance();
          } else { decoded += peek(); advance(); }
        }
        type = "STRING";
      } else if (digit(c)) {
        while (digit(peek())) advance();
        if (letter(peek()) || peek() == '.') {
          while (identifier(peek()) || peek() == '.') advance();
          fail("INVALID_NUMBER", "Only decimal integer literals are supported: " + std::string(text_.substr(start, position_ - start)), location);
        }
        if (position_ - start > 1000) fail("INTEGER_TOO_LONG", "Integer literals may contain at most 1000 digits.", location);
        type = "INTEGER";
      } else if (letter(c)) {
        while (identifier(peek())) advance();
        auto word = upper(std::string(text_.substr(start, position_ - start)));
        type = keywords.count(word) ? word : "IDENTIFIER";
      } else {
        std::string symbol(text_.substr(position_, 2));
        if (symbol.size() == 2 && symbols.count(symbol)) { advance(); advance(); }
        else {
          symbol = std::string(1, c);
          if (!symbols.count(symbol)) fail("ILLEGAL_CHARACTER", "Unsupported character at this position.", location);
          advance();
        }
        type = symbol;
      }
      const auto lexeme = std::string(text_.substr(start, position_ - start));
      if (type != "STRING") decoded = lexeme;
      result.push_back(Token{type, lexeme, location, decoded});
      if (result.size() > 100'000) fail("TOO_MANY_TOKENS", "SQL input exceeds 100000 tokens.", location);
    }
    result.push_back(Token{"EOF", "", location_, ""});
    return result;
  }
 private:
  [[noreturn]] static void fail(const std::string& code, const std::string& message, SourceLocation location) {
    throw DBError(Stage::Lexical, code, message, location);
  }
  char peek(std::size_t distance = 0) const {
    return position_ + distance < text_.size() ? text_[position_ + distance] : '\0';
  }
  void advance() {
    const auto c = static_cast<unsigned char>(text_[position_]);
    if (c == '\r') { ++location_.line; location_.column = 1; }
    else if (c == '\n') {
      if (position_ == 0 || text_[position_ - 1] != '\r') ++location_.line;
      location_.column = 1;
    } else if ((c & 0xc0U) != 0x80U) ++location_.column;
    ++position_;
  }
  void validate_utf8() {
    // Validate even comment/string bytes, so JSON and character-based columns are stable.
    if (text_.substr(0, 3) == "\xef\xbb\xbf") position_ = 3;
    while (position_ < text_.size()) {
      const auto c = static_cast<unsigned char>(peek());
      if (c == 0) fail("ILLEGAL_CHARACTER", "NUL is not accepted in SQL input.", location_);
      if (c < 0x80U) { advance(); continue; }
      std::size_t width = c >= 0xc2U && c <= 0xdfU ? 2 : c >= 0xe0U && c <= 0xefU ? 3 : c >= 0xf0U && c <= 0xf4U ? 4 : 0;
      bool valid = width && position_ + width <= text_.size();
      for (std::size_t i = 1; valid && i < width; ++i) valid = (static_cast<unsigned char>(peek(i)) & 0xc0U) == 0x80U;
      const auto second = static_cast<unsigned char>(peek(1));
      if ((c == 0xe0U && second < 0xa0U) || (c == 0xedU && second >= 0xa0U) ||
          (c == 0xf0U && second < 0x90U) || (c == 0xf4U && second >= 0x90U)) valid = false;
      if (!valid) fail("INVALID_UTF8", "SQL input must be valid UTF-8.", location_);
      for (std::size_t i = 0; i < width; ++i) advance();
    }
    position_ = 0; location_ = {};
  }
  std::string_view text_;
  std::size_t position_{0};
  SourceLocation location_;
};
}  // namespace

std::vector<Token> Lexer::tokenize() const { return Scanner(source_).run(); }
std::vector<Token> tokenize(std::string_view source) { return Lexer(source).tokenize(); }
}  // namespace minisql
