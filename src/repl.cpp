#include "repl.h"

#include "row.h"
#include "storage.h"

#include <iostream>
#include <string>

namespace {

// Phase 2: non-meta input is echoed.
// Phase 3+: this function will dispatch to the SQL parser/executor.
void execute_statement(const std::string& input, StorageEngine& engine) {
    (void)engine;
    std::cout << "You typed: " << input << "\n";
}

void print_help() {
    std::cout << "Meta commands:\n";
    std::cout << "  .exit     Exit the shell\n";
    std::cout << "  .help     Show this help\n";
    std::cout << "  .select   Print all rows from storage\n";
    std::cout << "  .stats    Print buffer pool / page stats\n";
    std::cout << "\n";
    std::cout << "Anything else is echoed for now (SQL parser comes in Phase 3).\n";
}

void print_rows(StorageEngine& engine) {
    const auto rows = engine.select_all();
    if (rows.empty()) {
        std::cout << "(empty table)\n";
        return;
    }

    std::cout << "id | name | age\n";
    std::cout << "---+------+----\n";
    for (const auto& row : rows) {
        std::cout << row.id << " | " << row.name << " | " << row.age << "\n";
    }
    std::cout << "(" << rows.size() << " rows)\n";
}

}  // namespace

void run_repl(StorageEngine& engine) {
    std::string input;

    std::cout << "ForgeDB interactive shell\n";
    std::cout << "Type .help for commands.\n\n";

    while (true) {
        std::cout << "mydb> ";
        if (!std::getline(std::cin, input)) {
            break;
        }

        // Meta-commands are handled before SQL — separate control path.
        if (input == ".exit") {
            break;
        }
        if (input == ".help") {
            print_help();
            continue;
        }
        if (input == ".select") {
            print_rows(engine);
            continue;
        }
        if (input == ".stats") {
            engine.print_stats();
            continue;
        }

        execute_statement(input, engine);
    }

    engine.flush();
    std::cout << "Bye.\n";
}
