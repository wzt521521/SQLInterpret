#include "wzt/buffer.hpp"

#include "wzt/errors.hpp"

#include <algorithm>
#include <cctype>

namespace wzt {

ReplacementPolicy parse_policy(std::string policy) {
    std::transform(policy.begin(), policy.end(), policy.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    if (policy == "LRU") return ReplacementPolicy::LRU;
    if (policy == "FIFO") return ReplacementPolicy::FIFO;
    throw StorageError("UNKNOWN_REPLACEMENT_POLICY", "replacement policy must be LRU or FIFO");
}

std::string policy_name(ReplacementPolicy policy) {
    return policy == ReplacementPolicy::LRU ? "LRU" : "FIFO";
}

BufferPool::BufferPool(std::size_t capacity, ReplacementPolicy policy, FileManager& file_manager)
    : capacity_(capacity), policy_(policy), file_manager_(file_manager) {
    if (capacity_ == 0) {
        throw StorageError("INVALID_BUFFER_CAPACITY", "buffer capacity must be positive");
    }
}

BufferPool::BufferPool(std::size_t capacity, const std::string& policy, FileManager& file_manager)
    : BufferPool(capacity, parse_policy(policy), file_manager) {}

PageData BufferPool::read_page(PageId page_id) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ++stats_.read_requests;
    return get_frame(page_id, true).data;
}

void BufferPool::write_page(PageId page_id, const Bytes& data) {
    const auto normalized = FileManager::normalize_page_data(data);
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    auto& frame = get_frame(page_id, false);
    frame.data = normalized;
    frame.dirty = true;
    touch(page_id);
}

PageData BufferPool::pin_page(PageId page_id) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ++stats_.read_requests;
    auto& frame = get_frame(page_id, true);
    ++frame.pin_count;
    return frame.data;
}

void BufferPool::unpin_page(PageId page_id) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    const auto found = frames_.find(page_id);
    if (found == frames_.end()) throw StorageError("PAGE_NOT_CACHED", "page is not cached");
    if (found->second.frame.pin_count == 0) throw StorageError("PAGE_NOT_PINNED", "page is not pinned");
    --found->second.frame.pin_count;
}

void BufferPool::free_page(PageId page_id) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    const auto found = frames_.find(page_id);
    if (found != frames_.end() && found->second.frame.pin_count != 0) {
        throw StorageError("PAGE_PINNED", "cannot free a pinned page");
    }
    file_manager_.free_page(page_id);
    if (found != frames_.end()) {
        order_.erase(found->second.order_position);
        frames_.erase(found);
    }
}

void BufferPool::flush_page(PageId page_id) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    const auto found = frames_.find(page_id);
    if (found == frames_.end()) {
        static_cast<void>(file_manager_.read_page(page_id));
        return;
    }
    flush_frame(found->second.frame);
}

void BufferPool::flush_all() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    for (auto& item : frames_) flush_frame(item.second.frame);
}

BufferStats BufferPool::stats() const {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    return stats_;
}

std::vector<PageId> BufferPool::resident_page_ids() const {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    return {order_.begin(), order_.end()};
}

void BufferPool::set_log_stream(std::ostream* output) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    log_output_ = output;
}

PageFrame& BufferPool::get_frame(PageId page_id, bool record_cache_result) {
    const auto found = frames_.find(page_id);
    if (found != frames_.end()) {
        if (record_cache_result) ++stats_.cache_hits;
        touch(page_id);
        log("buffer hit page=" + std::to_string(page_id));
        return found->second.frame;
    }
    if (record_cache_result) ++stats_.cache_misses;
    log("buffer miss page=" + std::to_string(page_id));
    const auto page_data = file_manager_.read_page(page_id);
    if (frames_.size() >= capacity_) evict_one();
    order_.push_back(page_id);
    const auto position = std::prev(order_.end());
    PageFrame frame{page_id, page_data, false, 0};
    auto inserted = frames_.emplace(page_id, CacheEntry{std::move(frame), position});
    if (!inserted.second) throw StorageError("IO_ERROR", "failed to insert buffer frame");
    return inserted.first->second.frame;
}

void BufferPool::touch(PageId page_id) {
    if (policy_ != ReplacementPolicy::LRU) return;
    auto& entry = frames_.at(page_id);
    order_.splice(order_.end(), order_, entry.order_position);
    entry.order_position = std::prev(order_.end());
}

void BufferPool::evict_one() {
    auto victim = order_.end();
    for (auto it = order_.begin(); it != order_.end(); ++it) {
        if (frames_.at(*it).frame.pin_count == 0) {
            victim = it;
            break;
        }
    }
    if (victim == order_.end()) throw StorageError("NO_EVICTABLE_PAGE", "all cached pages are pinned");
    const PageId victim_id = *victim;
    auto& frame = frames_.at(victim_id).frame;
    const bool dirty = frame.dirty;
    flush_frame(frame);
    frames_.erase(victim_id);
    order_.erase(victim);
    ++stats_.evictions;
    log("buffer evict page=" + std::to_string(victim_id) + " policy=" + policy_name(policy_) +
        " dirty=" + (dirty ? "true" : "false"));
}

void BufferPool::flush_frame(PageFrame& frame) {
    if (!frame.dirty) return;
    file_manager_.write_page(frame.page_id, Bytes(frame.data.begin(), frame.data.end()));
    frame.dirty = false;
    ++stats_.dirty_writes;
    log("buffer flush dirty page=" + std::to_string(frame.page_id));
}

void BufferPool::log(const std::string& message) const {
    if (log_output_ != nullptr) *log_output_ << message << '\n';
}

}  // namespace wzt
