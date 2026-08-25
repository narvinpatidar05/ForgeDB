#include "page_manager.h"

#include <fstream>

PageManager::PageManager(std::string filepath) : filepath_(std::move(filepath)) {}

Page PageManager::read_page(std::uint32_t page_id) {
    std::ifstream file(filepath_, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open database file for reading: " + filepath_);
    }

    file.seekg(static_cast<std::streamoff>(page_id) * static_cast<std::streamoff>(PAGE_SIZE));

    std::array<std::uint8_t, PAGE_SIZE> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()), PAGE_SIZE);

    if (file.gcount() != static_cast<std::streamoff>(PAGE_SIZE)) {
        throw std::runtime_error("Failed to read full page from disk: page_id=" + std::to_string(page_id));
    }

    Page page;
    page.load_from_bytes(bytes);
    return page;
}

void PageManager::write_page(std::uint32_t page_id, const Page& page) {
    std::fstream file(filepath_, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
        // Create file if it doesn't exist yet.
        std::ofstream create(filepath_, std::ios::binary);
        create.close();
        file.open(filepath_, std::ios::binary | std::ios::in | std::ios::out);
    }

    file.seekp(static_cast<std::streamoff>(page_id) * static_cast<std::streamoff>(PAGE_SIZE));
    const auto& bytes = page.raw_bytes();
    file.write(reinterpret_cast<const char*>(bytes.data()), PAGE_SIZE);
}

std::uint32_t PageManager::allocate_new_page() {
    const std::uint32_t new_id = page_count();
    Page page(new_id);
    write_page(new_id, page);
    return new_id;
}

std::uint32_t PageManager::page_count() const {
    std::ifstream file(filepath_, std::ios::binary | std::ios::ate);
    if (!file) {
        return 0;
    }

    const auto file_size = file.tellg();
    if (file_size <= 0) {
        return 0;
    }

    return static_cast<std::uint32_t>(file_size / PAGE_SIZE);
}

const std::string& PageManager::filepath() const {
    return filepath_;
}
