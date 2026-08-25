#pragma once

#include "row.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Explicit on-disk row size — never use sizeof(Row).
// Layout: id (4) + name (50) + age (4) = 58 bytes, no compiler padding.
inline constexpr std::size_t ROW_SIZE = sizeof(int) + 50 + sizeof(int);

// Field offsets in the on-disk byte layout.
inline constexpr std::size_t ROW_ID_OFFSET = 0;
inline constexpr std::size_t ROW_NAME_OFFSET = sizeof(int);
inline constexpr std::size_t ROW_AGE_OFFSET = sizeof(int) + 50;

// Serialize a Row into a fixed-size byte buffer (portable, padding-free).
std::array<std::uint8_t, ROW_SIZE> serialize_row(const Row& row);

// Deserialize a byte buffer back into a Row.
Row deserialize_row(const std::array<std::uint8_t, ROW_SIZE>& buf);
