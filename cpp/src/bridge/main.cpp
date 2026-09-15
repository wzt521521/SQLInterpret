#include "minidbms/engine/database.hpp"

#include "minisql/format.h"
#include "minisql/lexer.h"
#include "wzt/errors.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    std::string operation;
    std::filesystem::path db;
    std::size_t buffer_capacity{8};
    std::string replacement_policy{"LRU"};
};

Options options(int argc, char** argv) {
    if (argc < 2) throw std::invalid_argument("expected execute, status or reset");
    Options result;
    result.operation = argv[1];
    for (int index = 2; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() -> std::string {
            if (++index >= argc) throw std::invalid_argument("missing value for " + flag);
            return argv[index];
        };
        if (flag == "--db") result.db = std::filesystem::u8path(value());
        else if (flag == "--buffer-capacity") result.buffer_capacity = static_cast<std::size_t>(std::stoull(value()));
        else if (flag == "--replacement-policy") result.replacement_policy = value();
        else throw std::invalid_argument("unknown bridge option: " + flag);
    }
    if (result.db.empty()) throw std::invalid_argument("--db is required");
    if (result.operation != "execute" && result.operation != "status" && result.operation != "reset") {
        throw std::invalid_argument("operation must be execute, status or reset");
    }
    return result;
}

std::string value_json(const minisql::Value& value) {
    if (std::holds_alternative<std::int64_t>(value)) return std::to_string(std::get<std::int64_t>(value));
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value) ? "true" : "false";
    return minisql::json_quote(std::get<std::string>(value));
}

std::string result_json(const minisql::ExecutionResult& result) {
    std::string output = "{\"columns\":[";
    for (std::size_t index = 0; index < result.columns.size(); ++index) {
        if (index != 0) output += ',';
        output += minisql::json_quote(result.columns[index]);
    }
    output += "],\"rows\":[";
    for (std::size_t row = 0; row < result.rows.size(); ++row) {
        if (row != 0) output += ',';
        output += '[';
        for (std::size_t column = 0; column < result.rows[row].size(); ++column) {
            if (column != 0) output += ',';
            output += value_json(result.rows[row][column]);
        }
        output += ']';
    }
    return output + "],\"affected_rows\":" + std::to_string(result.affected_rows) +
           ",\"message\":" + minisql::json_quote(result.message) + "}";
}

std::string stats_json(const wzt::BufferStats& stats) {
    return "{\"read_requests\":" + std::to_string(stats.read_requests) +
           ",\"cache_hits\":" + std::to_string(stats.cache_hits) +
           ",\"cache_misses\":" + std::to_string(stats.cache_misses) +
           ",\"evictions\":" + std::to_string(stats.evictions) +
           ",\"dirty_writes\":" + std::to_string(stats.dirty_writes) + "}";
}

std::string status_json(minidbms::Database& db, const Options& args) {
    std::string tables = "[";
    bool first_table = true;
    for (const auto& table : db.catalog().list_tables()) {
        if (!first_table) tables += ',';
        first_table = false;
        tables += "{\"name\":" + minisql::json_quote(table.schema.table_name) + ",\"columns\":[";
        for (std::size_t index = 0; index < table.schema.columns.size(); ++index) {
            if (index != 0) tables += ',';
            const auto& column = table.schema.columns[index];
            tables += "{\"name\":" + minisql::json_quote(column.name) + ",\"type\":" +
                      minisql::json_quote(minisql::type_name(column.data_type)) + "}";
        }
        tables += "],\"pages\":" + std::to_string(table.pages.size()) + "}";
    }
    tables += ']';
    return "{\"version\":\"0.2.0\",\"database\":" + minisql::json_quote(args.db.string()) +
           ",\"buffer_capacity\":" + std::to_string(args.buffer_capacity) +
           ",\"replacement_policy\":" + minisql::json_quote(wzt::policy_name(wzt::parse_policy(args.replacement_policy))) +
           ",\"stats\":" + stats_json(db.stats()) + ",\"tables\":" + tables + "}";
}

std::string backup_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y%m%d-%H%M%S");
    return output.str();
}

std::filesystem::path next_backup_path(const std::filesystem::path& database) {
    const auto timestamp = backup_timestamp();
    auto candidate = database;
    candidate += ".backup-" + timestamp;
    for (std::size_t suffix = 2; std::filesystem::exists(candidate); ++suffix) {
        candidate = database;
        candidate += ".backup-" + timestamp + "-" + std::to_string(suffix);
    }
    return candidate;
}

std::string reset_database(const Options& args) {
    std::size_t removed_tables = 0;
    {
        minidbms::Database current(args.db, args.buffer_capacity, args.replacement_policy);
        removed_tables = current.catalog().list_tables().size();
        if (removed_tables == 0) {
            return "{\"ok\":true,\"removed_tables\":0,\"backup\":null,\"status\":" +
                   status_json(current, args) + "}";
        }
        current.close();
    }

    const auto backup = next_backup_path(args.db);
    std::error_code move_error;
    std::filesystem::rename(args.db, backup, move_error);
    if (move_error) {
        throw std::runtime_error("cannot reset database: failed to create backup: " + move_error.message());
    }

    try {
        minidbms::Database fresh(args.db, args.buffer_capacity, args.replacement_policy);
        fresh.flush_all();
        return "{\"ok\":true,\"removed_tables\":" + std::to_string(removed_tables) +
               ",\"backup\":" + minisql::json_quote(backup.string()) +
               ",\"status\":" + status_json(fresh, args) + "}";
    } catch (...) {
        std::error_code cleanup_error;
        std::filesystem::remove(args.db, cleanup_error);
        std::error_code restore_error;
        std::filesystem::rename(backup, args.db, restore_error);
        if (restore_error) {
            throw std::runtime_error("database reset failed and the backup could not be restored: " +
                                     restore_error.message());
        }
        throw;
    }
}

std::string web_error_json(const minisql::DBError& error) {
    return "{\"stage\":" + minisql::json_quote(minisql::stage_name(error.stage)) +
           ",\"code\":" + minisql::json_quote(error.code) +
           ",\"message\":" + minisql::json_quote(error.message) +
           ",\"line\":" + std::to_string(error.location.line) +
           ",\"column\":" + std::to_string(error.location.column) +
           ",\"display\":" + minisql::json_quote(error.what()) + "}";
}

std::string web_error_json(const wzt::StorageError& error) {
    return "{\"stage\":\"STORAGE\",\"code\":" + minisql::json_quote(error.code()) +
           ",\"message\":" + minisql::json_quote(error.message()) +
           ",\"line\":null,\"column\":null,\"display\":" + minisql::json_quote(error.what()) + "}";
}

std::string statement_kind(const minisql::StmtPtr& statement) {
    if (std::dynamic_pointer_cast<const minisql::CreateTableStmt>(statement)) return "CreateTableStmt";
    if (std::dynamic_pointer_cast<const minisql::InsertStmt>(statement)) return "InsertStmt";
    if (std::dynamic_pointer_cast<const minisql::SelectStmt>(statement)) return "SelectStmt";
    return "DeleteStmt";
}

std::string category(const std::string& kind) {
    if (kind == "CreateTableStmt") return "schema";
    if (kind == "SelectStmt") return "query";
    return "mutation";
}

std::string execute(minidbms::Database& db, const Options& args, const std::string& sql) {
    const auto started = std::chrono::steady_clock::now();
    std::string statements = "[";
    std::string error = "null";
    bool first = true;
    try {
        const auto tokens = minisql::tokenize(sql);
        const auto parsed = db.compiler().prepare(sql);
        std::size_t token_start = 0;
        for (const auto& statement : parsed.statements) {
            auto detail = db.compiler().compile_statement(statement, db.catalog(), true);
            std::size_t token_end = token_start;
            while (token_end < tokens.size() && tokens[token_end].type != ";") ++token_end;
            if (token_end < tokens.size()) ++token_end;
            detail.tokens.assign(tokens.begin() + static_cast<std::ptrdiff_t>(token_start),
                                 tokens.begin() + static_cast<std::ptrdiff_t>(token_end));
            token_start = token_end;
            const auto result = db.executor().execute(detail.plan_after);
            const auto kind = statement_kind(statement);
            if (!first) statements += ',';
            first = false;
            statements += "{\"kind\":" + minisql::json_quote(kind) +
                ",\"category\":" + minisql::json_quote(category(kind)) +
                ",\"result\":" + result_json(result) +
                ",\"compiler\":{\"tokens\":" + minisql::tokens_json(detail.tokens) +
                ",\"ast\":" + minisql::json_quote(minisql::format_ast(detail.ast)) +
                ",\"bound_ast\":" + minisql::json_quote(minisql::format_ast(detail.bound_ast)) +
                ",\"plan_before\":" + minisql::json_quote(minisql::format_plan(detail.plan_before)) +
                ",\"plan_after\":" + minisql::json_quote(minisql::format_plan(detail.plan_after)) + "}}";
        }
    } catch (const minisql::DBError& caught) {
        error = web_error_json(caught);
    } catch (const wzt::StorageError& caught) {
        error = web_error_json(caught);
    }
    statements += ']';
    try { db.flush_all(); }
    catch (const minisql::DBError& caught) { if (error == "null") error = web_error_json(caught); }
    catch (const wzt::StorageError& caught) { if (error == "null") error = web_error_json(caught); }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::ostringstream duration;
    duration.precision(2);
    duration << std::fixed << elapsed;
    return "{\"ok\":" + std::string(error == "null" ? "true" : "false") +
           ",\"statements\":" + statements + ",\"error\":" + error +
           ",\"duration_ms\":" + duration.str() + ",\"status\":" + status_json(db, args) + "}";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto args = options(argc, argv);
        if (args.operation == "reset") {
            std::cout << reset_database(args) << '\n';
            return 0;
        }
        minidbms::Database db(args.db, args.buffer_capacity, args.replacement_policy);
        if (args.operation == "status") {
            std::cout << "{\"ok\":true," << status_json(db, args).substr(1) << '\n';
        } else {
            const std::string sql(std::istreambuf_iterator<char>(std::cin), {});
            std::cout << execute(db, args, sql) << '\n';
        }
        return 0;
    } catch (const minisql::DBError& error) {
        std::cout << "{\"ok\":false,\"error\":" << web_error_json(error) << "}\n";
    } catch (const wzt::StorageError& error) {
        std::cout << "{\"ok\":false,\"error\":" << web_error_json(error) << "}\n";
    } catch (const std::exception& error) {
        std::cout << "{\"ok\":false,\"error\":{\"stage\":\"INTERNAL\",\"code\":\"BRIDGE_FAILURE\",\"message\":"
                  << minisql::json_quote(error.what()) << "}}\n";
    }
    return 1;
}
