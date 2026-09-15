#pragma once

#include "wzt/file_manager.hpp"
#include "wzt/page.hpp"

#include <list>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace wzt {

enum class ReplacementPolicy { LRU, FIFO };
ReplacementPolicy parse_policy(std::string policy);
std::string policy_name(ReplacementPolicy policy);

class BufferPool {
public:
    BufferPool(std::size_t capacity, ReplacementPolicy policy, FileManager& file_manager);
    BufferPool(std::size_t capacity, const std::string& policy, FileManager& file_manager);

    [[nodiscard]] PageData read_page(PageId page_id);
    void write_page(PageId page_id, const Bytes& data);
    [[nodiscard]] PageData pin_page(PageId page_id);
    void unpin_page(PageId page_id);
    void free_page(PageId page_id);
    void flush_page(PageId page_id);
    void flush_all();
    [[nodiscard]] BufferStats stats() const;
    [[nodiscard]] std::vector<PageId> resident_page_ids() const;
    void set_log_stream(std::ostream* output);

private:
    struct CacheEntry {
        PageFrame frame;
        std::list<PageId>::iterator order_position;
    };

    std::size_t capacity_;
    ReplacementPolicy policy_;
    FileManager& file_manager_;
    std::list<PageId> order_;
    std::unordered_map<PageId, CacheEntry> frames_;
    BufferStats stats_{};
    std::ostream* log_output_{nullptr};
    mutable std::recursive_mutex mutex_;

    PageFrame& get_frame(PageId page_id, bool record_cache_result);
    void touch(PageId page_id);
    void evict_one();
    void flush_frame(PageFrame& frame);
    void log(const std::string& message) const;
};

}  // namespace wzt
