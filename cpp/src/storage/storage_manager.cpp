#include "wzt/storage_manager.hpp"

#include "wzt/errors.hpp"

namespace wzt {

StorageManager::StorageManager(std::filesystem::path db_path, std::size_t buffer_capacity,
                               const std::string& replacement_policy)
    : file_manager_(std::move(db_path)),
      buffer_pool_(buffer_capacity, replacement_policy, file_manager_) {}

StorageManager::~StorageManager() noexcept {
    try { close(); } catch (...) {}
}

PageId StorageManager::allocate_page() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    return file_manager_.allocate_page();
}

void StorageManager::free_page(PageId page_id) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    buffer_pool_.free_page(page_id);
}

PageData StorageManager::read_page(PageId page_id) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    return buffer_pool_.read_page(page_id);
}

void StorageManager::write_page(PageId page_id, const Bytes& data) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    buffer_pool_.write_page(page_id, data);
}

void StorageManager::flush_page(PageId page_id) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    buffer_pool_.flush_page(page_id);
}

void StorageManager::flush_all() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    buffer_pool_.flush_all();
}

BufferStats StorageManager::stats() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    return buffer_pool_.stats();
}

void StorageManager::close() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    if (closed_) return;
    buffer_pool_.flush_all();
    file_manager_.close();
    closed_ = true;
}

void StorageManager::set_log_stream(std::ostream* output) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    buffer_pool_.set_log_stream(output);
}

void StorageManager::ensure_open() const {
    if (closed_) throw StorageError("STORAGE_CLOSED", "storage manager is closed");
}

}  // namespace wzt
