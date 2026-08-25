#include "page.h"

#include <cstring>
#include <stdexcept>

Page::Page() {
    PageHeader header{};
    header.free_space_offset = static_cast<std::uint32_t>(PAGE_HEADER_SIZE);
    header.slot_count = 0;
    header.lsn = 0;
    header.reserved = 0;
    write_header(header);
}

Page::Page(std::uint32_t page_id) : Page() {
    set_page_id(page_id);
}

PageHeader Page::read_header() const {
    PageHeader header{};
    std::memcpy(&header, data_.data(), PAGE_HEADER_SIZE);
    return header;
}

void Page::write_header(const PageHeader& header) {
    std::memcpy(data_.data(), &header, PAGE_HEADER_SIZE);
}

std::size_t Page::slot_offset(std::uint16_t slot_index) {
    return PAGE_SIZE - (static_cast<std::size_t>(slot_index + 1) * SLOT_ENTRY_SIZE);
}

std::uint16_t Page::read_slot(std::uint16_t slot_index) const {
    std::uint16_t offset = 0;
    const std::size_t pos = slot_offset(slot_index);
    std::memcpy(&offset, data_.data() + pos, SLOT_ENTRY_SIZE);
    return offset;
}

void Page::write_slot(std::uint16_t slot_index, std::uint16_t row_offset) {
    const std::size_t pos = slot_offset(slot_index);
    std::memcpy(data_.data() + pos, &row_offset, SLOT_ENTRY_SIZE);
}

bool Page::has_space_for_row() const {
    const PageHeader header = read_header();
    const std::size_t slot_bytes_after_insert =
        static_cast<std::size_t>(header.slot_count + 1) * SLOT_ENTRY_SIZE;
    const std::size_t row_end = header.free_space_offset + ROW_SIZE;
    return row_end + slot_bytes_after_insert <= PAGE_SIZE;
}

bool Page::insert_row(const std::array<std::uint8_t, ROW_SIZE>& row_bytes) {
    if (!has_space_for_row()) {
        return false;
    }

    PageHeader header = read_header();
    const std::uint16_t row_offset = static_cast<std::uint16_t>(header.free_space_offset);

    std::memcpy(data_.data() + header.free_space_offset, row_bytes.data(), ROW_SIZE);

    write_slot(header.slot_count, row_offset);

    header.free_space_offset += static_cast<std::uint32_t>(ROW_SIZE);
    header.slot_count += 1;
    write_header(header);

    return true;
}

std::optional<std::array<std::uint8_t, ROW_SIZE>> Page::get_row(std::uint16_t slot_index) const {
    const PageHeader header = read_header();
    if (slot_index >= header.slot_count) {
        return std::nullopt;
    }

    const std::uint16_t row_offset = read_slot(slot_index);
    std::array<std::uint8_t, ROW_SIZE> row_bytes{};
    std::memcpy(row_bytes.data(), data_.data() + row_offset, ROW_SIZE);
    return row_bytes;
}

std::vector<std::array<std::uint8_t, ROW_SIZE>> Page::get_all_rows() const {
    std::vector<std::array<std::uint8_t, ROW_SIZE>> rows;
    const PageHeader header = read_header();

    rows.reserve(header.slot_count);
    for (std::uint16_t i = 0; i < header.slot_count; ++i) {
        if (auto row = get_row(i)) {
            rows.push_back(*row);
        }
    }
    return rows;
}

std::uint16_t Page::slot_count() const {
    return read_header().slot_count;
}

std::uint32_t Page::page_id() const {
    return read_header().page_id;
}

void Page::set_page_id(std::uint32_t id) {
    PageHeader header = read_header();
    header.page_id = id;
    write_header(header);
}

const std::array<std::uint8_t, PAGE_SIZE>& Page::raw_bytes() const {
    return data_;
}

void Page::load_from_bytes(const std::array<std::uint8_t, PAGE_SIZE>& bytes) {
    data_ = bytes;
}

std::size_t Page::max_rows_per_page() {
    // Each row costs ROW_SIZE bytes + one slot entry at the page tail.
    return (PAGE_SIZE - PAGE_HEADER_SIZE) / (ROW_SIZE + SLOT_ENTRY_SIZE);
}
