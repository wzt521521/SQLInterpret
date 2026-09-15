#pragma once

#include "wzt/buffer.hpp"

#include <filesystem>
#include <mutex>
#include <ostream>
#include <string>

namespace wzt {

class StorageManager {
public:
    explicit StorageManager(
        std::filesystem::path db_path,
        std::size_t buffer_capacity = 16,
        const std::string& replacement_policy = "LRU");
    ~StorageManager() noexcept;
    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;

    PageId allocate_page();
    void free_page(PageId page_id);
    [[nodiscard]] PageData read_page(PageId page_id);
    void write_page(PageId page_id, const Bytes& data);
    void flush_page(PageId page_id);
    void flush_all();
    [[nodiscard]] BufferStats stats();
    void close();
    void set_log_stream(std::ostream* output);

private:
    FileManager file_manager_;
    BufferPool buffer_pool_;
    bool closed_{false};
    std::recursive_mutex mutex_;
    void ensure_open() const;
};

}  // namespace wzt
