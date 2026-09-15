#pragma once

#include "wzt/page.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace wzt {

class FileManager {
public:
    inline static constexpr std::size_t HEADER_SIZE = PAGE_SIZE;
    inline static constexpr std::uint32_t FORMAT_VERSION = 1;
    inline static constexpr std::size_t HEADER_PREFIX_SIZE = 8 + 4 + 8;
    inline static constexpr std::size_t BITMAP_SIZE = HEADER_SIZE - HEADER_PREFIX_SIZE;
    inline static constexpr std::size_t MAX_PAGES = BITMAP_SIZE * 8;

    explicit FileManager(std::filesystem::path db_path);
    ~FileManager() noexcept;
    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;

    [[nodiscard]] std::uint64_t page_count();
    [[nodiscard]] bool is_allocated(PageId page_id);
    PageId allocate_page();
    void free_page(PageId page_id);
    [[nodiscard]] PageData read_page(PageId page_id);
    void write_page(PageId page_id, const Bytes& data);
    void close();
    static PageData normalize_page_data(const Bytes& data);

private:
    inline static constexpr std::array<Byte, 8> MAGIC{
        'M', 'D', 'B', 'M', 'S', 'P', 'G', '1'
    };

    std::filesystem::path db_path_;
    std::fstream file_;
    bool open_{false};
    std::uint64_t page_count_{0};
    std::array<Byte, BITMAP_SIZE> allocation_bitmap_{};
    mutable std::recursive_mutex mutex_;

    void load_header();
    void persist_header();
    void sync_file();
    void ensure_open() const;
    void require_allocated(PageId page_id) const;
    static void validate_page_id(PageId page_id);
    [[nodiscard]] bool bit_is_set(PageId page_id) const;
    void set_bit(PageId page_id, bool allocated);
    [[nodiscard]] bool has_bits_beyond_page_count() const;
    [[nodiscard]] static std::uint64_t page_offset(PageId page_id);
    void rollback_extension(std::uint64_t old_file_size) noexcept;
};

}  // namespace wzt
