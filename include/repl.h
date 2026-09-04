#pragma once

class StorageEngine;

// Interactive shell — mydb> prompt.
// Meta-commands (.exit, .help) are handled here, not by the SQL parser.
void run_repl(StorageEngine& engine);
