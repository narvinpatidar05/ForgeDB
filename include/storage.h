#pragma once

#include "buffer_pool.h"
#include "page.h"
#include "page_manager.h"
#include "row.h"

#include <cstdint>
#include <string>
#include <vector>

// Top-level storage engine: coordinates buffer pool, page manager, and row codec.
// This is the layer SQL executors will eventually call into.
class StorageEngine {
public:
    explicit StorageEngine(std::string filepath, std::size_t buffer_pool_capacity = 64);

    void insert_row(const Row& row);
    std::vector<Row> select_all();

    void flush();
    void print_stats() const;

    std::uint32_t page_count() const;
    std::size_t max_rows_per_page() const;

private:
    PageManager page_manager_;
    BufferPool buffer_pool_;
    std::uint32_t current_page_id_;
};

void insert_row(const Row& row, const std::string& filepath);
std::vector<Row> select_all(const std::string& filepath);
