#include "minidbms/engine/database.hpp"

#include "minisql/common.h"
#include "wzt/errors.hpp"
#include "wzt/page.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path db{"minidb.db"};
    std::filesystem::path file;
    std::size_t buffer_capacity{16};
    std::string replacement_policy{"LRU"};
    bool stats{false};
    bool cache_log{false};
    bool help{false};
    bool version{false};
};

std::size_t positive_size(const std::string& text) {
    std::size_t used = 0;
    const auto value = std::stoull(text, &used);
    if (used != text.size() || value == 0) throw std::invalid_argument("buffer capacity must be positive");
    return static_cast<std::size_t>(value);
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() -> std::string {
            if (++index >= argc) throw std::invalid_argument("missing value for " + flag);
            return argv[index];
        };
        if (flag == "--db") options.db = std::filesystem::u8path(value());
        else if (flag == "--file") options.file = std::filesystem::u8path(value());
        else if (flag == "--buffer-capacity") options.buffer_capacity = positive_size(value());
        else if (flag == "--replacement-policy") options.replacement_policy = value();
        else if (flag == "--stats") options.stats = true;
        else if (flag == "--cache-log") options.cache_log = true;
        else if (flag == "--help" || flag == "-h") options.help = true;
        else if (flag == "--version") options.version = true;
        else throw std::invalid_argument("unknown option: " + flag);
    }
    return options;
}

std::pair<std::vector<std::string>, std::string> split_complete_sql(const std::string& text) {
    std::vector<std::string> complete;
    std::size_t start = 0;
    std::string state = "normal";
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char current = text[index];
        const char following = index + 1 < text.size() ? text[index + 1] : '\0';
        if (state == "normal") {
            if (current == '\'') state = "string";
            else if (current == '-' && following == '-') { state = "line_comment"; ++index; }
            else if (current == '/' && following == '*') { state = "block_comment"; ++index; }
            else if (current == ';') {
                complete.push_back(text.substr(start, index - start + 1));
                start = index + 1;
            }
        } else if (state == "string") {
            if (current == '\'' && following == '\'') ++index;
            else if (current == '\'') state = "normal";
        } else if (state == "line_comment") {
            if (current == '\r' || current == '\n') state = "normal";
        } else if (state == "block_comment" && current == '*' && following == '/') {
            state = "normal";
            ++index;
        }
    }
    return {std::move(complete), text.substr(start)};
}

std::string result_text(const minisql::ExecutionResult& result) {
    std::ostringstream output;
    if (!result.columns.empty()) {
        for (std::size_t index = 0; index < result.columns.size(); ++index) {
            if (index != 0) output << " | ";
            output << result.columns[index];
        }
        output << '\n';
        for (const auto& row : result.rows) {
            for (std::size_t index = 0; index < row.size(); ++index) {
                if (index != 0) output << " | ";
                if (std::holds_alternative<std::string>(row[index])) {
                    output << std::get<std::string>(row[index]);
                } else if (std::holds_alternative<bool>(row[index])) {
                    output << (std::get<bool>(row[index]) ? "true" : "false");
                } else {
                    output << std::get<std::int64_t>(row[index]);
                }
            }
            output << '\n';
        }
    }
    output << (result.message.empty() ? std::to_string(result.affected_rows) + " row(s) affected" : result.message);
    return output.str();
}

bool run_sql(minidbms::Database& db, const std::string& sql) {
    try {
        for (const auto& result : db.execute(sql)) std::cout << result_text(result) << '\n';
        return true;
    } catch (const minisql::DBError& error) {
        std::cerr << minisql::stage_name(error.stage) << ':' << error.code
                  << " at line " << error.location.line << ", column " << error.location.column
                  << ": " << error.message << '\n';
    } catch (const wzt::StorageError& error) {
        std::cerr << error.what() << '\n';
    }
    return false;
}

bool run_text(minidbms::Database& db, const std::string& text) {
    auto framed = split_complete_sql(text);
    bool succeeded = true;
    for (const auto& statement : framed.first) succeeded = run_sql(db, statement) && succeeded;
    if (framed.second.find_first_not_of(" \t\r\n") != std::string::npos) {
        succeeded = run_sql(db, framed.second) && succeeded;
    }
    return succeeded;
}

void print_stats(minidbms::Database& db) {
    std::cout << wzt::stats_to_string(db.stats()) << '\n';
}

void interactive(minidbms::Database& db) {
    std::string pending;
    std::string line;
    while (true) {
        std::cout << (pending.empty() ? "MiniDB > " : "    ... > ") << std::flush;
        if (!std::getline(std::cin, line)) {
            if (!pending.empty()) run_sql(db, pending);
            std::cout << '\n';
            return;
        }
        if (pending.empty() && (line == "exit" || line == "quit")) return;
        if (pending.empty() && line == "stats") { print_stats(db); continue; }
        pending += line + '\n';
        auto framed = split_complete_sql(pending);
        for (const auto& statement : framed.first) run_sql(db, statement);
        pending = std::move(framed.second);
        if (pending.find_first_not_of(" \t\r\n") == std::string::npos) pending.clear();
    }
}

void help() {
    std::cout << "MiniDBMS C++\n"
              << "  --db PATH                  database file\n"
              << "  --file PATH                execute a UTF-8 SQL file\n"
              << "  --buffer-capacity N        cached page count\n"
              << "  --replacement-policy NAME LRU or FIFO\n"
              << "  --cache-log                print buffer events\n"
              << "  --stats                    print final statistics\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        if (options.help) { help(); return 0; }
        if (options.version) { std::cout << "MiniDBMS C++ 0.2.0\n"; return 0; }
        minidbms::Database db(options.db, options.buffer_capacity, options.replacement_policy);
        if (options.cache_log) db.set_cache_log(&std::cerr);
        bool succeeded = true;
        if (options.file.empty()) interactive(db);
        else {
            std::ifstream input(options.file, std::ios::binary);
            if (!input) throw std::runtime_error("cannot open SQL file");
            succeeded = run_text(db, std::string(std::istreambuf_iterator<char>(input), {}));
        }
        if (options.stats) print_stats(db);
        db.close();
        return succeeded ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
