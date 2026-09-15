#include "minidbms/engine/database.hpp"

#include "wzt/errors.hpp"

namespace minidbms {

Database::Database(const std::filesystem::path& path, std::size_t buffer_capacity,
                   const std::string& replacement_policy)
    : storage_(path, buffer_capacity, replacement_policy),
      catalog_(storage_),
      rows_(storage_, catalog_),
      executor_(catalog_, rows_) {}

Database::~Database() noexcept {
    try { close(); } catch (...) {}
}

std::vector<minisql::ExecutionResult> Database::execute(const std::string& sql) {
    if (closed_) throw wzt::StorageError("STORAGE_CLOSED", "database is closed");
    return compiler_.compile_and_execute(sql, catalog_, executor_);
}

wzt::BufferStats Database::stats() {
    if (closed_) throw wzt::StorageError("STORAGE_CLOSED", "database is closed");
    return storage_.stats();
}

void Database::flush_all() {
    if (closed_) throw wzt::StorageError("STORAGE_CLOSED", "database is closed");
    storage_.flush_all();
}

void Database::close() {
    if (closed_) return;
    storage_.close();
    closed_ = true;
}

void Database::set_cache_log(std::ostream* output) {
    if (closed_) throw wzt::StorageError("STORAGE_CLOSED", "database is closed");
    storage_.set_log_stream(output);
}

}  // namespace minidbms
