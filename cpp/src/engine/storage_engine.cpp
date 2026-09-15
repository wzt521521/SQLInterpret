#include "minidbms/engine/storage_engine.hpp"

#include "minidbms/engine/errors.hpp"

#include <algorithm>

namespace minidbms {

StorageEngine::StorageEngine(wzt::StorageManager& storage, CatalogManager& catalog)
    : storage_(storage), catalog_(catalog) {}

RowRef StorageEngine::insert(const std::string& table_name, const minisql::Row& values) {
    const auto schema = catalog_.get_schema(table_name);
    const auto encoded = codec_.encode(schema, values);
    if (encoded.size() > RowPage::MAX_RECORD_SIZE) {
        execution_error("RECORD_TOO_LARGE", "record cannot fit in one data page");
    }
    for (const auto page_id : catalog_.page_ids(table_name)) {
        RowPage page(storage_.read_page(page_id));
        if (!page.can_fit(encoded.size())) continue;
        const auto slot_id = page.insert(encoded);
        storage_.write_page(page_id, wzt::Bytes(page.data().begin(), page.data().end()));
        return {schema.table_name, page_id, slot_id};
    }

    const auto page_id = storage_.allocate_page();
    try {
        auto page = RowPage::empty();
        const auto slot_id = page.insert(encoded);
        storage_.write_page(page_id, wzt::Bytes(page.data().begin(), page.data().end()));
        storage_.flush_page(page_id);
        catalog_.add_page(table_name, page_id);
        return {schema.table_name, page_id, slot_id};
    } catch (...) {
        try { storage_.free_page(page_id); } catch (...) {}
        throw;
    }
}

std::vector<ScannedRow> StorageEngine::scan(const std::string& table_name) {
    const auto schema = catalog_.get_schema(table_name);
    std::vector<ScannedRow> result;
    for (const auto page_id : catalog_.page_ids(table_name)) {
        const RowPage page(storage_.read_page(page_id));
        for (const auto& record : page.live_records()) {
            result.push_back({{schema.table_name, page_id, record.first},
                              codec_.decode(schema, record.second)});
        }
    }
    return result;
}

std::size_t StorageEngine::erase(const std::vector<RowRef>& refs) {
    std::size_t affected = 0;
    for (const auto& ref : refs) {
        const auto pages = catalog_.page_ids(ref.table_name);
        if (std::find(pages.begin(), pages.end(), ref.page_id) == pages.end()) {
            execution_error("ROW_NOT_FOUND", "row reference is not part of the table");
        }
        RowPage page(storage_.read_page(ref.page_id));
        if (page.erase(ref.slot_id)) {
            storage_.write_page(ref.page_id, wzt::Bytes(page.data().begin(), page.data().end()));
            ++affected;
        }
    }
    return affected;
}

}  // namespace minidbms
