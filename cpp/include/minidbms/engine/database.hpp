#pragma once

#include "minidbms/engine/catalog_manager.hpp"
#include "minidbms/engine/executor.hpp"
#include "minidbms/engine/storage_engine.hpp"
#include "minisql/compiler.h"
#include "wzt/storage_manager.hpp"

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace minidbms {

class Database {
public:
    explicit Database(const std::filesystem::path& path,
                      std::size_t buffer_capacity = 16,
                      const std::string& replacement_policy = "LRU");
    ~Database() noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    std::vector<minisql::ExecutionResult> execute(const std::string& sql);
    [[nodiscard]] wzt::BufferStats stats();
    void flush_all();
    void close();
    void set_cache_log(std::ostream* output);

    [[nodiscard]] CatalogManager& catalog() noexcept { return catalog_; }
    [[nodiscard]] Executor& executor() noexcept { return executor_; }
    [[nodiscard]] minisql::Compiler& compiler() noexcept { return compiler_; }
    [[nodiscard]] wzt::StorageManager& storage() noexcept { return storage_; }

private:
    wzt::StorageManager storage_;
    CatalogManager catalog_;
    StorageEngine rows_;
    Executor executor_;
    minisql::Compiler compiler_;
    bool closed_{false};
};

}  // namespace minidbms
