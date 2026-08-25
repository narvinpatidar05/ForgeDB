#include "storage.h"

#include "row_codec.h"

#include <iostream>
#include <stdexcept>

StorageEngine::StorageEngine(std::string filepath, std::size_t buffer_pool_capacity)
    : page_manager_(std::move(filepath)),
      buffer_pool_(page_manager_, buffer_pool_capacity),
      current_page_id_(0) {
    if (page_manager_.page_count() == 0) {
        current_page_id_ = buffer_pool_.new_page().page_id();
    } else {
        current_page_id_ = page_manager_.page_count() - 1;
    }
}

void StorageEngine::insert_row(const Row& row) {
    const auto row_bytes = serialize_row(row);

    Page& page = buffer_pool_.get_page(current_page_id_);
    if (!page.insert_row(row_bytes)) {
        Page& new_page = buffer_pool_.new_page();
        current_page_id_ = new_page.page_id();
        if (!new_page.insert_row(row_bytes)) {
            throw std::runtime_error("Row does not fit in a fresh page — check ROW_SIZE vs PAGE_SIZE");
        }
    }

    buffer_pool_.mark_dirty(current_page_id_);
}

std::vector<Row> StorageEngine::select_all() {
    std::vector<Row> rows;
    const std::uint32_t total_pages = page_manager_.page_count();

    for (std::uint32_t page_id = 0; page_id < total_pages; ++page_id) {
        Page& page = buffer_pool_.get_page(page_id);
        for (const auto& row_bytes : page.get_all_rows()) {
            rows.push_back(deserialize_row(row_bytes));
        }
    }

    return rows;
}

void StorageEngine::flush() {
    buffer_pool_.flush_all();
}

void StorageEngine::print_stats() const {
    std::cout << "Buffer pool hits:   " << buffer_pool_.hits() << "\n";
    std::cout << "Buffer pool misses: " << buffer_pool_.misses() << "\n";
    std::cout << "Cached pages:       " << buffer_pool_.cached_page_count() << "\n";
    std::cout << "Total pages on disk:" << page_manager_.page_count() << "\n";
}

std::uint32_t StorageEngine::page_count() const {
    return page_manager_.page_count();
}

std::size_t StorageEngine::max_rows_per_page() const {
    return Page::max_rows_per_page();
}

void insert_row(const Row& row, const std::string& filepath) {
    StorageEngine engine(filepath);
    engine.insert_row(row);
    engine.flush();
}

std::vector<Row> select_all(const std::string& filepath) {
    StorageEngine engine(filepath);
    return engine.select_all();
}
