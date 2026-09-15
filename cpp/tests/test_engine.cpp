#include "minidbms/engine/database.hpp"
#include "minidbms/engine/record.hpp"

#include "minisql/common.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

class TempDirectory {
public:
    TempDirectory() {
        path_ = fs::temp_directory_path() /
            ("minidb-engine-test-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        fs::create_directories(path_);
    }
    ~TempDirectory() { std::error_code ignored; fs::remove_all(path_, ignored); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("assertion failed: " + message);
}

template <typename Operation>
void expect_db_error(const std::string& code, Operation operation) {
    try { operation(); }
    catch (const minisql::DBError& error) {
        require(error.code == code, "unexpected DB error " + error.code);
        return;
    }
    throw std::runtime_error("expected DBError " + code);
}

void test_record_and_row_page() {
    const minisql::TableSchema schema{"student", {
        {"id", minisql::DataType::Int}, {"name", minisql::DataType::Varchar}
    }};
    minidbms::RecordCodec codec;
    const minisql::Row values{std::int64_t{1}, std::string("中;文")};
    const auto encoded = codec.encode(schema, values);
    require(codec.decode(schema, encoded) == values, "record round trip");
    auto page = minidbms::RowPage::empty();
    const auto first = page.insert(encoded);
    page.insert(codec.encode(schema, {std::int64_t{2}, std::string("Bob")}));
    require(page.live_records().size() == 2, "two live rows");
    require(page.erase(first) && !page.erase(first), "tombstone is idempotent");
    require(minidbms::RowPage(page.data()).live_records().size() == 1, "row page restart");
}

void test_sql_pipeline_and_restart() {
    TempDirectory temporary;
    const auto path = temporary.path() / "e2e.db";
    {
        minidbms::Database db(path, 2, "FIFO");
        const auto results = db.execute(
            "CREATE TABLE student(id INT, name VARCHAR, age INT);"
            "INSERT INTO student VALUES(1,'Alice',20);"
            "INSERT INTO student VALUES(2,'Bob',17);"
            "INSERT INTO student VALUES(3,'Carol',22);"
            "SELECT id,name FROM student WHERE age>=18;"
            "DELETE FROM student WHERE id=1;"
            "SELECT * FROM student;");
        require(results.size() == 7, "seven statements executed");
        require(results[4].rows.size() == 2, "filter result");
        require(std::get<std::int64_t>(results[4].rows[0][0]) == 1, "first projected id");
        require(results[5].affected_rows == 1, "one row deleted");
        require(results[6].rows.size() == 2, "delete is visible");
    }
    {
        minidbms::Database db(path, 2, "LRU");
        const auto result = db.execute("SELECT * FROM student;").front();
        require(result.rows.size() == 2, "rows survive restart");
        require(std::get<std::string>(result.rows[0][1]) == "Bob", "row ordering survives restart");
        require(db.catalog().get_schema("STUDENT").columns.size() == 3, "catalog survives restart");
    }
}

void test_cross_page_and_errors() {
    TempDirectory temporary;
    minidbms::Database db(temporary.path() / "cross.db", 2, "LRU");
    db.execute("CREATE TABLE items(id INT, name VARCHAR);");
    const std::string long_text(700, 'x');
    for (int index = 0; index < 24; ++index) {
        db.execute("INSERT INTO items VALUES(" + std::to_string(index) + ",'" + long_text + "');");
    }
    require(db.catalog().page_ids("items").size() >= 4, "records span pages");
    require(db.execute("SELECT id FROM items;").front().rows.size() == 24, "cross-page scan");
    require(db.execute("DELETE FROM items WHERE id>=12 AND id<18;").front().affected_rows == 6,
            "cross-page delete");
    expect_db_error("COLUMN_NOT_FOUND", [&] { db.execute("SELECT missing FROM items;"); });
    expect_db_error("TABLE_ALREADY_EXISTS", [&] { db.execute("CREATE TABLE items(id INT);"); });
}

void test_catalog_multi_page() {
    TempDirectory temporary;
    const auto path = temporary.path() / "catalog.db";
    {
        minidbms::Database db(path, 2);
        for (int index = 0; index < 90; ++index) {
            db.execute("CREATE TABLE table_" + std::to_string(index) + "(id INT, description VARCHAR);");
        }
        require(db.catalog().list_tables().size() == 90, "catalog table count");
    }
    minidbms::Database reopened(path, 2);
    require(reopened.catalog().table_exists("table_89"), "multi-page catalog survives restart");
}

void test_delete_all_and_failed_insert_cleanup() {
    TempDirectory temporary;
    minidbms::Database db(temporary.path() / "limits.db", 2);
    db.execute("CREATE TABLE t(id INT, value VARCHAR); INSERT INTO t VALUES(1,'a'); INSERT INTO t VALUES(2,'b');");
    require(db.execute("DELETE FROM t;").front().affected_rows == 2, "delete without WHERE");
    require(db.execute("SELECT * FROM t;").front().rows.empty(), "deleted rows are skipped");

    db.execute("CREATE TABLE huge(value VARCHAR);");
    const std::string oversized(5000, 'x');
    expect_db_error("RECORD_TOO_LARGE", [&] {
        db.execute("INSERT INTO huge VALUES('" + oversized + "');");
    });
    require(db.catalog().page_ids("huge").empty(), "failed insert does not publish a data page");
}

void test_catalog_and_data_corruption() {
    TempDirectory temporary;
    const auto catalog_path = temporary.path() / "bad-catalog.db";
    {
        minidbms::Database db(catalog_path);
        db.execute("CREATE TABLE t(id INT);");
    }
    {
        wzt::StorageManager storage(catalog_path);
        storage.write_page(0, wzt::bytes_from_string("invalid catalog root"));
    }
    expect_db_error("CORRUPT_CATALOG", [&] { minidbms::Database invalid(catalog_path); });

    const auto data_path = temporary.path() / "bad-data.db";
    wzt::PageId data_page = 0;
    {
        minidbms::Database db(data_path);
        db.execute("CREATE TABLE t(id INT); INSERT INTO t VALUES(1);");
        data_page = db.catalog().page_ids("t").front();
    }
    {
        wzt::StorageManager storage(data_path);
        storage.write_page(data_page, wzt::bytes_from_string("invalid row page"));
    }
    expect_db_error("CORRUPT_PAGE", [&] {
        minidbms::Database db(data_path);
        db.execute("SELECT * FROM t;");
    });
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"record and row page", test_record_and_row_page},
        {"SQL pipeline and restart", test_sql_pipeline_and_restart},
        {"cross page and errors", test_cross_page_and_errors},
        {"multi-page catalog", test_catalog_multi_page},
        {"delete all and failed insert cleanup", test_delete_all_and_failed_insert_cleanup},
        {"catalog and data corruption", test_catalog_and_data_corruption},
    };
    for (const auto& test : tests) {
        try { test.second(); std::cout << "[PASS] " << test.first << '\n'; }
        catch (const std::exception& error) {
            std::cerr << "[FAIL] " << test.first << " -> " << error.what() << '\n';
            return 1;
        }
    }
    return 0;
}
