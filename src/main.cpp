#include "repl.h"
#include "row_codec.h"
#include "storage.h"

#include <iostream>

int main() {
    const std::string filepath = "./data/mydb.db";
    StorageEngine engine(filepath);

    std::cout << "ROW_SIZE = " << ROW_SIZE << " bytes (on-disk wire format)\n";
    std::cout << "Max rows per page: " << engine.max_rows_per_page() << "\n\n";

    run_repl(engine);
    return 0;
}
