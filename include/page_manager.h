#pragma once

#include "page.h"

#include <cstdint>
#include <string>

// Owns all disk I/O in units of full 16KB pages.
// The database file is a contiguous sequence of pages:
//   page N lives at file offset N * PAGE_SIZE
class PageManager {
public:
    explicit PageManager(std::string filepath);

    Page read_page(std::uint32_t page_id);
    void write_page(std::uint32_t page_id, const Page& page);

    // Extends the file by one page and returns the new page's id.
    std::uint32_t allocate_new_page();

    std::uint32_t page_count() const;
    const std::string& filepath() const;

private:
    std::string filepath_;
};
