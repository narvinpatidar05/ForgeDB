#include "storage.h"

#include <fstream>

void insert_row(const Row& row, const std::string& filepath) {
    std::ofstream file(filepath, std::ios::binary | std::ios::app);
    file.write(reinterpret_cast<const char*>(&row), sizeof(Row));
}

std::vector<Row> select_all(const std::string& filepath) {
    std::vector<Row> rows;
    std::ifstream file(filepath, std::ios::binary);
    Row row;
    while (file.read(reinterpret_cast<char*>(&row), sizeof(Row))) {
        rows.push_back(row);
    }
    return rows;
}
