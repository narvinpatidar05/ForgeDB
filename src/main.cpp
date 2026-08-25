#include "row_codec.h"
#include "storage.h"

#include <cstring>
#include <iostream>

int main() {
    std::cout << "=== ForgeDB Storage Engine Demo ===\n\n";

    std::cout << "sizeof(Row) = " << sizeof(Row) << " bytes (in-memory, includes padding)\n";
    std::cout << "ROW_SIZE    = " << ROW_SIZE << " bytes (on-disk, explicit layout)\n\n";

    const std::string filepath = "./data/mydb.db";

    StorageEngine engine(filepath, /*buffer_pool_capacity=*/2);

    std::cout << "Max rows per page: " << engine.max_rows_per_page() << "\n";
    std::cout << "Inserting 1000 rows...\n";

    for (int i = 1; i <= 1000; ++i) {
        Row row{};
        row.id = i;
        std::snprintf(row.name, sizeof(row.name), "user_%d", i);
        row.age = 20 + (i % 50);
        engine.insert_row(row);
    }

    engine.flush();

    std::cout << "\n--- After insert ---\n";
    engine.print_stats();

    const auto rows = engine.select_all();
    std::cout << "\nTotal rows read: " << rows.size() << "\n";
    std::cout << "First row:  " << rows.front().id << " | " << rows.front().name << " | "
              << rows.front().age << "\n";
    std::cout << "Last row:   " << rows.back().id << " | " << rows.back().name << " | "
              << rows.back().age << "\n";

    std::cout << "\n--- After select_all (cache exercised) ---\n";
    engine.print_stats();

    const std::uint32_t pages = engine.page_count();
    const std::size_t max_per_page = engine.max_rows_per_page();
    const double expected_pages = static_cast<double>(1000) / static_cast<double>(max_per_page);

    std::cout << "\n--- Verification ---\n";
    std::cout << "Expected ~" << expected_pages << " pages for 1000 rows\n";
    std::cout << "Actual pages: " << pages << "\n";
    std::cout << "Rows packed into " << pages << " pages (not 1000 separate writes)\n";

    if (pages > 1 && pages < 1000) {
        std::cout << "PASS: Multiple rows share pages.\n";
    } else {
        std::cout << "CHECK: Page packing may be unexpected.\n";
    }

    return 0;
}
