#include "wzt/file_manager.hpp"

#include "wzt/errors.hpp"

#include <algorithm>
#include <system_error>

namespace wzt {
namespace {

void write_u32_le(PageData& target, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        target[offset + i] = static_cast<Byte>((value >> (i * 8)) & 0xffU);
    }
}

void write_u64_le(PageData& target, std::size_t offset, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        target[offset + i] = static_cast<Byte>((value >> (i * 8)) & 0xffULL);
    }
}

std::uint32_t read_u32_le(const PageData& source, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(source[offset + i]) << (i * 8);
    }
    return value;
}

std::uint64_t read_u64_le(const PageData& source, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(source[offset + i]) << (i * 8);
    }
    return value;
}

}  // namespace

FileManager::FileManager(std::filesystem::path db_path) : db_path_(std::move(db_path)) {
    try {
        const auto parent = db_path_.parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        const bool exists = std::filesystem::exists(db_path_);
        if (!exists) {
            std::ofstream creator(db_path_, std::ios::binary);
            if (!creator) throw StorageError("FILE_OPEN_FAILED", "cannot create database file");
        }
        file_.open(db_path_, std::ios::binary | std::ios::in | std::ios::out);
        if (!file_) throw StorageError("FILE_OPEN_FAILED", "cannot open database file");
        open_ = true;
        exists ? load_header() : persist_header();
    } catch (const StorageError&) {
        if (file_.is_open()) file_.close();
        open_ = false;
        throw;
    } catch (const std::exception& error) {
        if (file_.is_open()) file_.close();
        open_ = false;
        throw StorageError("FILE_OPEN_FAILED", error.what());
    }
}

FileManager::~FileManager() noexcept {
    try { close(); } catch (...) {}
}

std::uint64_t FileManager::page_count() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    return page_count_;
}

bool FileManager::is_allocated(PageId page_id) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    validate_page_id(page_id);
    return static_cast<std::uint64_t>(page_id) < page_count_ && bit_is_set(page_id);
}

PageId FileManager::allocate_page() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    PageId page_id = static_cast<PageId>(page_count_);
    for (std::uint64_t candidate = 0; candidate < page_count_; ++candidate) {
        if (!bit_is_set(static_cast<PageId>(candidate))) {
            page_id = static_cast<PageId>(candidate);
            break;
        }
    }
    if (page_id >= static_cast<PageId>(MAX_PAGES)) {
        throw StorageError("PAGE_LIMIT_EXCEEDED", "database page bitmap is full");
    }

    const bool extending = static_cast<std::uint64_t>(page_id) == page_count_;
    const auto old_page_count = page_count_;
    const auto old_file_size = HEADER_SIZE + old_page_count * PAGE_SIZE;
    try {
        PageData zeros{};
        file_.clear();
        file_.seekp(static_cast<std::streamoff>(page_offset(page_id)));
        file_.write(reinterpret_cast<const char*>(zeros.data()), PAGE_SIZE);
        if (!file_) throw StorageError("IO_ERROR", "failed to initialize allocated page");
        if (extending) ++page_count_;
        set_bit(page_id, true);
        persist_header();
        return page_id;
    } catch (...) {
        set_bit(page_id, false);
        page_count_ = old_page_count;
        if (extending) rollback_extension(old_file_size);
        throw;
    }
}

void FileManager::free_page(PageId page_id) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    require_allocated(page_id);
    try {
        PageData zeros{};
        file_.clear();
        file_.seekp(static_cast<std::streamoff>(page_offset(page_id)));
        file_.write(reinterpret_cast<const char*>(zeros.data()), PAGE_SIZE);
        if (!file_) throw StorageError("IO_ERROR", "failed to zero released page");
        set_bit(page_id, false);
        persist_header();
    } catch (...) {
        set_bit(page_id, true);
        throw;
    }
}

PageData FileManager::read_page(PageId page_id) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    require_allocated(page_id);
    PageData data{};
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(page_offset(page_id)));
    file_.read(reinterpret_cast<char*>(data.data()), PAGE_SIZE);
    const auto count = file_.gcount();
    if (count != static_cast<std::streamsize>(PAGE_SIZE)) {
        file_.clear();
        throw StorageError("CORRUPT_FILE", "page is truncated: expected 4096 bytes, got " + std::to_string(count));
    }
    return data;
}

void FileManager::write_page(PageId page_id, const Bytes& data) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    ensure_open();
    require_allocated(page_id);
    const auto normalized = normalize_page_data(data);
    file_.clear();
    file_.seekp(static_cast<std::streamoff>(page_offset(page_id)));
    file_.write(reinterpret_cast<const char*>(normalized.data()), PAGE_SIZE);
    if (!file_) throw StorageError("IO_ERROR", "failed to write page");
    sync_file();
}

void FileManager::close() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    if (!open_) return;
    sync_file();
    file_.close();
    if (file_.fail()) {
        open_ = false;
        throw StorageError("IO_ERROR", "failed to close database file");
    }
    open_ = false;
}

PageData FileManager::normalize_page_data(const Bytes& data) {
    if (data.size() > PAGE_SIZE) {
        throw StorageError("PAGE_DATA_TOO_LARGE", "page data exceeds 4096 bytes");
    }
    PageData normalized{};
    std::copy(data.begin(), data.end(), normalized.begin());
    return normalized;
}

void FileManager::load_header() {
    ensure_open();
    const auto actual_size = std::filesystem::file_size(db_path_);
    if (actual_size < HEADER_SIZE) {
        throw StorageError("CORRUPT_FILE", "database file is smaller than its header");
    }
    PageData header{};
    file_.clear();
    file_.seekg(0);
    file_.read(reinterpret_cast<char*>(header.data()), HEADER_SIZE);
    if (file_.gcount() != static_cast<std::streamsize>(HEADER_SIZE)) {
        file_.clear();
        throw StorageError("CORRUPT_FILE", "database header is truncated");
    }
    if (!std::equal(MAGIC.begin(), MAGIC.end(), header.begin())) {
        throw StorageError("CORRUPT_FILE", "database file magic does not match");
    }
    const auto version = read_u32_le(header, 8);
    const auto stored_page_count = read_u64_le(header, 12);
    if (version != FORMAT_VERSION) {
        throw StorageError("UNSUPPORTED_FORMAT", "database format version is not supported");
    }
    if (stored_page_count > MAX_PAGES) {
        throw StorageError("CORRUPT_FILE", "page count exceeds header capacity");
    }
    const auto expected_size = HEADER_SIZE + stored_page_count * PAGE_SIZE;
    if (actual_size != expected_size) {
        throw StorageError("CORRUPT_FILE", "database file size does not match page count");
    }
    page_count_ = stored_page_count;
    std::copy(header.begin() + static_cast<std::ptrdiff_t>(HEADER_PREFIX_SIZE),
              header.end(), allocation_bitmap_.begin());
    if (has_bits_beyond_page_count()) {
        throw StorageError("CORRUPT_FILE", "bitmap references pages outside the file");
    }
}

void FileManager::persist_header() {
    ensure_open();
    PageData header{};
    std::copy(MAGIC.begin(), MAGIC.end(), header.begin());
    write_u32_le(header, 8, FORMAT_VERSION);
    write_u64_le(header, 12, page_count_);
    std::copy(allocation_bitmap_.begin(), allocation_bitmap_.end(),
              header.begin() + static_cast<std::ptrdiff_t>(HEADER_PREFIX_SIZE));
    file_.clear();
    file_.seekp(0);
    file_.write(reinterpret_cast<const char*>(header.data()), HEADER_SIZE);
    if (!file_) throw StorageError("IO_ERROR", "failed to persist file header");
    sync_file();
}

void FileManager::sync_file() {
    ensure_open();
    file_.flush();
    if (!file_) throw StorageError("IO_ERROR", "failed to flush database file");
}

void FileManager::ensure_open() const {
    if (!open_) throw StorageError("STORAGE_CLOSED", "database file is closed");
}

void FileManager::require_allocated(PageId page_id) const {
    validate_page_id(page_id);
    if (static_cast<std::uint64_t>(page_id) >= page_count_) {
        throw StorageError("PAGE_NOT_FOUND", "page does not exist");
    }
    if (!bit_is_set(page_id)) throw StorageError("PAGE_FREED", "page has been released");
}

void FileManager::validate_page_id(PageId page_id) {
    if (page_id < 0) throw StorageError("INVALID_PAGE_ID", "page id cannot be negative");
}

bool FileManager::bit_is_set(PageId page_id) const {
    const auto value = static_cast<std::size_t>(page_id);
    return (allocation_bitmap_[value / 8] & static_cast<Byte>(1U << (value % 8))) != 0;
}

void FileManager::set_bit(PageId page_id, bool allocated) {
    const auto value = static_cast<std::size_t>(page_id);
    const auto mask = static_cast<Byte>(1U << (value % 8));
    if (allocated) allocation_bitmap_[value / 8] |= mask;
    else allocation_bitmap_[value / 8] &= static_cast<Byte>(~mask);
}

bool FileManager::has_bits_beyond_page_count() const {
    for (std::size_t page = static_cast<std::size_t>(page_count_); page < MAX_PAGES; ++page) {
        if (bit_is_set(static_cast<PageId>(page))) return true;
    }
    return false;
}

std::uint64_t FileManager::page_offset(PageId page_id) {
    return HEADER_SIZE + static_cast<std::uint64_t>(page_id) * PAGE_SIZE;
}

void FileManager::rollback_extension(std::uint64_t old_file_size) noexcept {
    try {
        file_.clear();
        file_.flush();
        file_.close();
        open_ = false;
        std::filesystem::resize_file(db_path_, old_file_size);
        file_.open(db_path_, std::ios::binary | std::ios::in | std::ios::out);
        open_ = static_cast<bool>(file_);
    } catch (...) {}
}

}  // namespace wzt
