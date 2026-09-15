#include "wzt/page.hpp"

#include <sstream>

namespace wzt {

Bytes bytes_from_string(const std::string& text) {
    return Bytes(text.begin(), text.end());
}

std::string page_prefix_as_string(const PageData& page) {
    auto end = page.end();
    while (end != page.begin() && *(end - 1) == 0) --end;
    return std::string(page.begin(), end);
}

std::string stats_to_string(const BufferStats& stats) {
    std::ostringstream out;
    out << "{read_requests: " << stats.read_requests
        << ", cache_hits: " << stats.cache_hits
        << ", cache_misses: " << stats.cache_misses
        << ", evictions: " << stats.evictions
        << ", dirty_writes: " << stats.dirty_writes << '}';
    return out.str();
}

}  // namespace wzt
