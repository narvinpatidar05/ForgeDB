#include "row_codec.h"

#include <cstring>

std::array<std::uint8_t, ROW_SIZE> serialize_row(const Row& row) {
    std::array<std::uint8_t, ROW_SIZE> buf{};

    // Copy each field individually so the on-disk layout is explicit and
    // does not depend on compiler struct padding or alignment.
    std::memcpy(buf.data() + ROW_ID_OFFSET, &row.id, sizeof(row.id));
    std::memcpy(buf.data() + ROW_NAME_OFFSET, row.name, sizeof(row.name));
    std::memcpy(buf.data() + ROW_AGE_OFFSET, &row.age, sizeof(row.age));

    return buf;
}

Row deserialize_row(const std::array<std::uint8_t, ROW_SIZE>& buf) {
    Row row{};

    std::memcpy(&row.id, buf.data() + ROW_ID_OFFSET, sizeof(row.id));
    std::memcpy(row.name, buf.data() + ROW_NAME_OFFSET, sizeof(row.name));
    std::memcpy(&row.age, buf.data() + ROW_AGE_OFFSET, sizeof(row.age));

    // name is not guaranteed null-terminated on disk — ensure it is in memory.
    row.name[sizeof(row.name) - 1] = '\0';

    return row;
}
