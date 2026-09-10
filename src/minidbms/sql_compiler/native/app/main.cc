#include "minisql/format.h"
#include "session_catalog.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
using namespace minisql;
using app::SessionCatalog;

struct Options {
  std::string mode;
  std::string input;
  std::string output;
  bool trace{false};
  bool json{false};
  bool optimize{true};
};

Options options(int argc, char** argv) {
  Options result;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    auto value = [&]() -> std::string {
      if (++index == argc) throw std::invalid_argument("Missing value for " + flag);
      return argv[index];
    };
    if (flag == "--ll1" || flag == "--sql" || flag == "--file" || flag == "--help") {
      if (!result.mode.empty()) throw std::invalid_argument("Choose one of --ll1, --sql, --file, --help.");
      result.mode = flag;
      if (flag == "--sql" || flag == "--file") result.input = value();
    } else if (flag == "--trace") result.trace = true;
    else if (flag == "--no-optimize") result.optimize = false;
    else if (flag == "--output") result.output = value();
    else if (flag == "--format") {
      const auto format = value();
      if (format != "text" && format != "json") throw std::invalid_argument("--format must be text or json.");
      result.json = format == "json";
    } else throw std::invalid_argument("Unknown option: " + flag);
  }
  if (result.mode.empty() && (result.json || !result.output.empty()))
    throw std::invalid_argument("Interactive mode requires text output to the terminal.");
  return result;
}

void write_output(const Options& options, const std::string& text) {
  if (options.output.empty()) { std::cout << text << '\n'; return; }
  const auto destination = std::filesystem::u8path(options.output);
  if (destination.has_parent_path()) std::filesystem::create_directories(destination.parent_path());
  std::ofstream stream(destination, std::ios::binary);
  if (!stream || !(stream << text << '\n')) throw std::runtime_error("Cannot write output file: " + options.output);
}

std::string detail_text(const CompilationResult& detail, std::size_t index) {
  return "Statement " + std::to_string(index) + "\nToken\n" + format_tokens(detail.tokens) +
    "\nAST\n" + format_ast(detail.ast) + "\nSemantic: PASS\nBound AST\n" + format_ast(detail.bound_ast) +
    "\nPlan before\n" + format_plan(detail.plan_before) + "\nPlan after\n" + format_plan(detail.plan_after) + "\n";
}

bool process(const std::string& sql, SessionCatalog& catalog, const Options& options, std::string& rendered) {
  std::vector<CompilationResult> details;
  std::vector<std::string> trace;
  std::string diagnostic = "null";
  std::string diagnostic_text;
  try {
    const auto tokens = tokenize(sql);
    const auto parsed = LL1Parser().parse(tokens, options.trace);
    trace = parsed.trace;
    std::size_t start = 0;
    for (const auto& statement : parsed.statements) {
      auto detail = Compiler().compile_statement(statement, catalog, options.optimize);
      auto end = start;
      while (end < tokens.size() && tokens[end].type != ";") ++end;
      if (end < tokens.size()) ++end;
      detail.tokens.assign(tokens.begin() + static_cast<std::ptrdiff_t>(start), tokens.begin() + static_cast<std::ptrdiff_t>(end));
      start = end;
      if (const auto create = std::dynamic_pointer_cast<const CreateTablePlan>(detail.plan_after))
        catalog.register_schema(create->schema);
      details.push_back(std::move(detail));
    }
  } catch (const DBError& error) { diagnostic = error_json(error); diagnostic_text = error.what(); }
  std::ostringstream out;
  if (options.json) {
    out << "{\"mode\":\"compiler\",\"results\":[";
    for (std::size_t i = 0; i < details.size(); ++i) { if (i) out << ','; out << compilation_json(details[i]); }
    out << "],\"trace\":[";
    for (std::size_t i = 0; i < trace.size(); ++i) { if (i) out << ','; out << json_quote(trace[i]); }
    out << "],\"error\":" << diagnostic << '}';
  } else {
    out << "C++ LL(1) compiler: session schemas only; no row execution or persistence.\n\n";
    if (!trace.empty()) { out << "LL(1) trace\n"; for (const auto& line : trace) out << line << '\n'; out << '\n'; }
    for (std::size_t i = 0; i < details.size(); ++i) out << detail_text(details[i], i + 1) << '\n';
    if (!diagnostic_text.empty()) out << diagnostic_text << '\n';
  }
  rendered = out.str();
  return diagnostic == "null";
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleCP(CP_UTF8); SetConsoleOutputCP(CP_UTF8);
#endif
  try {
#ifdef _WIN32
    // Windows provides UTF-16 command lines; normalize to the library's UTF-8
    // contract so Chinese paths and SQL strings work independently of ACP.
    int wide_count = 0;
    auto wide_arguments = CommandLineToArgvW(GetCommandLineW(), &wide_count);
    if (!wide_arguments) throw std::runtime_error("Cannot read command line.");
    std::vector<std::string> utf8_arguments;
    for (int index = 0; index < wide_count; ++index) {
      const int length = WideCharToMultiByte(CP_UTF8, 0, wide_arguments[index], -1, nullptr, 0, nullptr, nullptr);
      std::string text(static_cast<std::size_t>(length), '\0');
      WideCharToMultiByte(CP_UTF8, 0, wide_arguments[index], -1, text.data(), length, nullptr, nullptr);
      if (!text.empty()) text.pop_back();
      utf8_arguments.push_back(std::move(text));
    }
    LocalFree(wide_arguments);
    std::vector<char*> utf8_argv;
    for (auto& text : utf8_arguments) utf8_argv.push_back(text.data());
    argc = wide_count;
    argv = utf8_argv.data();
#endif
    const auto args = options(argc, argv);
    if (args.mode == "--help") {
      std::cout << "MiniSQL C++ compiler (zby)\n"
        << "  --ll1 | --sql SQL | --file UTF8_FILE\n"
        << "  --trace --no-optimize --format text|json --output FILE\n"
        << "No input mode: one SQL submission per line, semicolon required; quit/exit closes.\n";
      return 0;
    }
    if (args.mode == "--ll1") {
      const auto text = LL1Parser().grammar().dump();
      write_output(args, args.json ? "{\"grammar\":" + json_quote(text) + "}" : text); return 0;
    }
    SessionCatalog catalog;
    if (!args.mode.empty()) {
      std::string sql = args.input;
      if (args.mode == "--file") {
        std::ifstream stream(std::filesystem::u8path(args.input), std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot open input file: " + args.input);
        sql.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        if (stream.bad()) throw std::runtime_error("Cannot read input file: " + args.input);
      }
      std::string rendered;
      const bool success = process(sql, catalog, args, rendered);
      write_output(args, rendered); return success ? 0 : 1;
    }
    std::cout << "C++ LL(1) compiler; session schemas only, no row execution or persistence.\n"
      << "One submission per line; multi-line scripts use --file. Type quit or exit to leave.\n";
    std::string line;
    while (std::cout << "Compiler > " && std::getline(std::cin, line)) {
      const auto first = line.find_first_not_of(" \t\r\n");
      const auto last = line.find_last_not_of(" \t\r\n");
      const auto command = first == std::string::npos ? "" : lower_ascii(line.substr(first, last - first + 1));
      if (command == "quit" || command == "exit") break;
      if (command.empty()) continue;
      std::string rendered;
      process(line, catalog, args, rendered);
      std::cout << rendered << '\n';
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "[CLI/ERROR]: " << error.what() << '\n'; return 2;
  }
}
