#pragma once

#include "row.h"
#include <string>
#include <vector>

void insert_row(const Row& row, const std::string& filepath);
std::vector<Row> select_all(const std::string& filepath);
