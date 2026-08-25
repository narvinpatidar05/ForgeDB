#pragma once

#include "row_codec.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// InnoDB-style 16KB page — the fundamental disk I/O unit for the storage engine.
inline constexpr std::size_t PAGE_SIZE = 16384;
inline constexpr std::size_t PAGE_HEADER_SIZE = 16;
inline constexpr std::size_t SLOT_ENTRY_SIZE = sizeof(std::uint16_t);

// Page header stored at the start of every page on disk.
// LSN is reserved for WAL integration later; unused for now.
struct PageHeader {
    std::uint32_t page_id;
    std::uint32_t free_space_offset;
    std::uint16_t slot_count;
    std::uint16_t lsn;
    std::uint32_t reserved;  // pad header to exactly PAGE_HEADER_SIZE
};

static_assert(sizeof(PageHeader) == PAGE_HEADER_SIZE);

// A single 16KB page with slotted-page row layout.
//
// Physical layout:
//   [Header][Row data grows →][free space][← slot array at page end]
//
// Why slotted pages: rows can be inserted/deleted without shifting all
// subsequent data — the slot array maps logical row index to byte offset.
class Page {
public:
    Page();
    explicit Page(std::uint32_t page_id);

    // Insert a serialized row. Returns false if the page has no room left.
    bool insert_row(const std::array<std::uint8_t, ROW_SIZE>& row_bytes);

    // Read row at logical slot index (0 = first inserted row on this page).
    std::optional<std::array<std::uint8_t, ROW_SIZE>> get_row(std::uint16_t slot_index) const;

    std::vector<std::array<std::uint8_t, ROW_SIZE>> get_all_rows() const;

    bool has_space_for_row() const;
    std::uint16_t slot_count() const;
    std::uint32_t page_id() const;
    void set_page_id(std::uint32_t id);

    const std::array<std::uint8_t, PAGE_SIZE>& raw_bytes() const;
    void load_from_bytes(const std::array<std::uint8_t, PAGE_SIZE>& bytes);

    // Max rows this page can hold given ROW_SIZE and slot entry overhead.
    static std::size_t max_rows_per_page();

private:
    PageHeader read_header() const;
    void write_header(const PageHeader& header);

    // Slot i is stored at a fixed offset from the page end (index 0 = closest to EOF).
    static std::size_t slot_offset(std::uint16_t slot_index);
    std::uint16_t read_slot(std::uint16_t slot_index) const;
    void write_slot(std::uint16_t slot_index, std::uint16_t row_offset);

    std::array<std::uint8_t, PAGE_SIZE> data_{};
};
