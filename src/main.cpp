#include "row.h"
#include "storage.h"

#include <cstring>
#include <iostream>

int main() {
    std::cout << "sizeof(Row) = " << sizeof(Row) << " bytes\n";

    std::string filepath = "./data/mydb.db";

    Row r1;
    r1.id = 1;
    std::strncpy(r1.name, "atharv", sizeof(r1.name));
    r1.age = 20;
    insert_row(r1, filepath);

    Row r2;
    r2.id = 2;
    std::strncpy(r2.name, "rahul", sizeof(r2.name));
    r2.age = 22;
    insert_row(r2, filepath);

    for (const auto& row : select_all(filepath)) {
        std::cout << row.id << " | " << row.name << " | " << row.age << "\n";
    }

    return 0;
}
