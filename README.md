# ForgeDB

**ForgeDB** is a relational database engine written in C++, built from scratch to explore how systems like **MySQL/InnoDB** store data on disk, parse SQL, build indexes, and guarantee durability.

The project is organized as a layered storage engine — disk I/O at the bottom, query processing at the top — following the same architectural patterns used by production databases.

| | |
|---|---|
| **Language** | C++17 |
| **Build** | CMake / g++ |

---

## Table of Contents

- [High-Level Design (HLD)](#high-level-design-hld)
- [Low-Level Design (LLD)](#low-level-design-lld)
- [End-to-End Runtime Flow](#end-to-end-runtime-flow)
- [Disk Layout](#disk-layout)
- [Pages & Disk Seeks](#pages--disk-seeks)
- [Project Structure](#project-structure)
- [Build & Run](#build--run)
- [Architecture Roadmap](#architecture-roadmap)
- [References](#references)

---

## High-Level Design (HLD)

ForgeDB follows a **layered architecture** — the same pattern used by MySQL, PostgreSQL, and SQLite.

```mermaid
flowchart TB
    subgraph Client["Client Layer"]
        CLI["REPL Shell<br/>mydb&gt; prompt"]
    end

    subgraph Query["Query Layer"]
        Parser["SQL Parser<br/>Lexer + AST"]
        Executor["Query Executor<br/>INSERT / SELECT / WHERE"]
        Optimizer["Query Optimizer"]
    end

    subgraph Storage["Storage Layer"]
        Index["B+ Tree Index"]
        Txn["Transaction Manager<br/>ACID / WAL"]
        Buffer["Buffer Pool<br/>RAM Cache"]
        PageMgr["Page Manager<br/>4KB–16KB blocks"]
    end

    subgraph Disk["Disk Layer"]
        IO["Disk I/O<br/>insert_row / select_all"]
        File["data/mydb.db<br/>Binary file"]
    end

    CLI --> Parser
    Parser --> Executor
    Executor --> Optimizer
    Optimizer --> Index
    Optimizer --> PageMgr
    Index --> PageMgr
    Txn --> PageMgr
    Buffer --> PageMgr
    PageMgr --> IO
    IO --> File
```

### Component Responsibilities

| Layer | Component | Responsibility |
|-------|-----------|----------------|
| Client | REPL | Interactive shell and meta-commands (`.exit`, `.help`) |
| Query | SQL Parser | Tokenize and parse SQL into an AST |
| Query | Executor | Execute INSERT, SELECT, and WHERE against storage |
| Storage | Page Manager | Manage fixed-size pages and slotted row format |
| Storage | B+ Tree Index | O(log n) lookups by key |
| Storage | Transaction Manager | ACID guarantees, WAL, crash recovery |
| Storage | Buffer Pool | Cache hot pages in RAM |
| Disk | Disk I/O | Raw byte read/write to the database file |

---

## Low-Level Design (LLD)

### Module Diagram

```mermaid
classDiagram
    class Row {
        +int id
        +char name[50]
        +int age
    }

    class Storage {
        +insert_row(row, filepath) void
        +select_all(filepath) vector~Row~
    }

    class Main {
        +main() int
    }

    Main --> Storage : calls
    Storage --> Row : reads/writes bytes
```

### Row Schema

The storage layer uses a fixed-size row format. Each row is a plain C++ struct serialized as raw bytes on disk.

```cpp
struct Row {
    int id;          // 4 bytes
    char name[50];   // 50 bytes (fixed — truncates beyond 50 chars)
    int age;         // 4 bytes
};
// Total: sizeof(Row) = 60 bytes (includes compiler alignment padding)
```

### Storage API

| Function | Input | Output | Disk Operation |
|----------|-------|--------|----------------|
| `insert_row(row, path)` | `Row` struct | void | Append `sizeof(Row)` raw bytes to end of file |
| `select_all(path)` | file path | `vector<Row>` | Read file in `sizeof(Row)` chunks from start to end |

Serialization uses `reinterpret_cast` to copy the in-memory struct directly to disk with no format conversion. This is the simplest possible persistence model and the foundation that higher layers build on.

---

## End-to-End Runtime Flow

### Insert Flow

```mermaid
sequenceDiagram
    participant User
    participant main as main.cpp
    participant RAM as Memory (Row struct)
    participant storage as storage.cpp
    participant Disk as data/mydb.db

    User->>main: ./build/mydb
    main->>main: Create Row r1 (id=1, name="atharv", age=20)
    main->>RAM: Row struct in memory (60 bytes)
    main->>storage: insert_row(r1, "./data/mydb.db")
    storage->>Disk: ofstream (binary | append)
    storage->>Disk: write 60 raw bytes at EOF
    Note over Disk: Row persisted

    main->>main: Create Row r2 (id=2, name="rahul", age=22)
    main->>storage: insert_row(r2, "./data/mydb.db")
    storage->>Disk: append 60 more bytes
```

### Select Flow

```mermaid
sequenceDiagram
    participant main as main.cpp
    participant storage as storage.cpp
    participant Disk as data/mydb.db
    participant RAM as vector<Row>
    participant User

    main->>storage: select_all("./data/mydb.db")
    storage->>Disk: ifstream (binary), open from start
    loop Until EOF
        storage->>Disk: read sizeof(Row) bytes
        Disk-->>storage: raw bytes
        storage->>RAM: reinterpret as Row, push to vector
    end
    storage-->>main: vector<Row>
    main->>User: print id | name | age for each row
```

### Full Program Flow

```mermaid
flowchart LR
    A["./build/mydb"] --> B["main()"]
    B --> C["Print sizeof(Row)"]
    C --> D["Build Row in RAM"]
    D --> E["insert_row()"]
    E --> F["Build Row in RAM"]
    F --> G["insert_row()"]
    G --> H["select_all()"]
    H --> I["Print all rows"]
    I --> J["exit 0"]

    E --> K["storage.cpp<br/>binary append"]
    G --> K
    H --> L["storage.cpp<br/>binary read loop"]
    K --> M["data/mydb.db"]
    L --> M
```

---

## Disk Layout

Rows are stored as a **flat, sequential sequence of fixed-size records** — one row immediately after another with no page headers or metadata.

```
data/mydb.db
┌──────────────────────────────────────────────────────────────────────┐
│ Row 0 (60 bytes)          │ Row 1 (60 bytes)          │ Row 2 ...    │
├───────────────────────────┼───────────────────────────┼──────────────┤
│ id=1                      │ id=2                      │              │
│ name="atharv" (50 bytes)  │ name="rahul"  (50 bytes)  │              │
│ age=20                    │ age=22                    │              │
└───────────────────────────┴───────────────────────────┴──────────────┘
  offset 0                    offset 60                   offset 120

Random access:
  Row N is at byte offset = N × sizeof(Row)
  file.seekg(N * sizeof(Row))
```

### Hex Dump Example

```bash
xxd data/mydb.db
```

```
00000000: 0100 0000 6174 6861 7276 ... 1400 0000   ← id=1, "atharv", age=20
0000003c: 0200 0000 7261 6875 6c ... 1600 0000   ← id=2, "rahul",  age=22
```

- `0100 0000` → little-endian integer `1` (id)
- `6174 6861 7276` → ASCII `"atharv"`
- `1400 0000` → little-endian integer `20` (age)

Opening the file in a text editor shows non-printable characters, confirming the data is stored in binary format rather than plain text or CSV.

---

## Pages & Disk Seeks

Production storage engines organize disk data into **pages** rather than writing rows directly to a flat file. Understanding pages and seeks explains why real databases are structured the way they are.

### What is a Page?

A **page** (also called a **block**) is a fixed-size chunk of disk that the storage engine reads or writes as a single unit.

```mermaid
flowchart TB
    subgraph File["Table File / Tablespace"]
        P0["Page 0<br/>16 KB"]
        P1["Page 1<br/>16 KB"]
        P2["Page 2<br/>16 KB"]
        P3["Page N<br/>16 KB"]
    end

    subgraph Page0Detail["Inside Page 0 (InnoDB Slotted Page)"]
        H["Page Header<br/>LSN, free space, slot count"]
        S["Slot Array<br/>row offsets"]
        R1["Row 1"]
        R2["Row 2"]
        R3["Row 3"]
    end

    P0 --> Page0Detail
```

| Engine | Default Page Size | Notes |
|--------|-------------------|-------|
| **InnoDB (MySQL)** | 16 KB | Slotted page format, multiple variable-length rows per page |
| **SQLite** | 4 KB | B-tree pages (leaf vs internal node types) |
| **PostgreSQL** | 8 KB | Heap pages and separate index page files |

**Why pages exist:**

1. **Disk I/O granularity** — The OS and disk hardware operate on blocks (typically 4 KB). Reading one page equals one I/O operation regardless of how many rows are inside.
2. **Buffer pool efficiency** — Cache entire pages in RAM and reuse them without re-reading from disk.
3. **Space management** — Track free space and insert or delete rows within a page without rewriting the whole file.

### What is a Disk Seek?

A **seek** is the physical movement of the disk read/write head to a new location on the platter (HDD), or the latency of accessing a new flash block (SSD).

```mermaid
flowchart LR
    subgraph WithoutPages["Without Pages — 100 row lookups"]
        direction TB
        S1["Seek → Read Row 1"]
        S2["Seek → Read Row 2"]
        S3["Seek → Read Row 3"]
        SN["... × 100 seeks"]
    end

    subgraph WithPages["With Pages — 100 rows in 1 page"]
        direction TB
        P["1 Seek → Read 16 KB Page"]
        E["Extract all 100 rows from RAM"]
    end

    WithoutPages --> Cost1["~100 × seek latency<br/>~5–10 ms each on HDD<br/>= 500–1000 ms total"]
    WithPages --> Cost2["~1 × seek latency<br/>+ CPU to scan page<br/>= ~5–10 ms total"]
```

| Operation | Seek Count (HDD) | Typical Latency |
|-----------|------------------|-----------------|
| Sequential read (full scan) | 0 extra seeks | ~100 MB/s throughput |
| Random read — 1 row, no pages | 1 seek per row | ~5–10 ms per row |
| Random read — 100 rows, no pages | 100 seeks | ~500–1000 ms |
| Random read — 100 rows in 1 page | 1 seek | ~5–10 ms |

> **Key insight:** Random I/O is seek-bound, not bandwidth-bound. Pages amortize seek cost by bundling many rows into a single I/O operation.

### Flat File vs Page-Based Storage

```mermaid
flowchart TB
    subgraph FlatFile["Flat File Storage"]
        A1["insert_row()"] --> B1["Append bytes to EOF"]
        C1["select_all()"] --> D1["Sequential read entire file"]
        E1["Point lookup"] --> F1["Full file scan O(n)"]
    end

    subgraph PageBased["Page-Based Storage (InnoDB)"]
        A2["INSERT"] --> B2["Find page with free space"]
        B2 --> C2["Write row into page slot"]
        D2["Point lookup"] --> E2["B+ Tree index lookup"]
        E2 --> F2["Read target page O(log n)"]
    end
```

| Aspect | Flat File | Page-Based + Index |
|--------|-----------|-------------------|
| Write | Append to EOF | Insert into page slot |
| Full scan | Read every byte O(n) | Scan all pages O(n) — same complexity, better I/O pattern |
| Point lookup | Full scan O(n) | B+ Tree → one page read O(log n) |
| Seeks per 1000 lookups | ~1000 (worst case) | ~1–3 (index + data page) |
| Space efficiency | Fixed bytes per row | Variable-length rows, less waste |

---

## Project Structure

```
ForgeDB/
├── include/
│   ├── row.h                 # Table schema (Row struct)
│   └── storage.h             # Storage API declarations
├── src/
│   ├── main.cpp              # Entry point
│   └── storage.cpp           # Disk I/O (insert_row, select_all)
├── data/
│   ├── .gitkeep
│   └── mydb.db               # Created at runtime (gitignored)
├── tests/
├── build/                    # Compiled binary (gitignored)
├── CMakeLists.txt
├── LEARNINGS.md              # DB concepts and developer notes
└── README.md
```

---

## Build & Run

### Prerequisites

- C++17 compiler (`g++` or `clang++`)
- CMake 3.10+ (optional)

### Build with g++

```bash
g++ -std=c++17 -Iinclude src/main.cpp src/storage.cpp -o build/mydb
```

### Build with CMake

```bash
mkdir -p build && cd build
cmake ..
cmake --build .
cd ..
```

### Run

```bash
./build/mydb
```

**Expected output:**

```
sizeof(Row) = 60 bytes
1 | atharv | 20
2 | rahul | 22
```

### Verify Binary Persistence

```bash
# Inspect raw bytes on disk
xxd data/mydb.db

# Fresh start (delete existing data)
rm data/mydb.db && ./build/mydb
```

> Running the program multiple times appends rows to the existing file. Delete `data/mydb.db` to start with a clean database.

---

## Architecture Roadmap

The engine is built incrementally, one layer at a time:

| Layer | Feature |
|-------|---------|
| Disk I/O | Row storage on disk — binary file read/write |
| REPL | Interactive shell with `mydb>` prompt |
| SQL Parser | Lexer, tokenizer, and AST generation |
| Query Executor | INSERT, SELECT, WHERE |
| Page Manager | Fixed-size pages and slotted row format |
| B+ Tree Index | O(log n) key lookups |
| Transaction Manager | WAL and basic ACID guarantees |
| Buffer Pool | In-memory page cache |

---

## References

- [Let's Build a Simple Database (cstack/db_tutorial)](https://github.com/cstack/db_tutorial)
- [Database Internals — Alex Petrov (O'Reilly)](https://www.databass.dev/)
- [Designing Data-Intensive Applications — Martin Kleppmann](https://dataintensive.net/)
- [CMU 15-445 Database Systems (Andy Pavlo)](https://www.youtube.com/playlist?list=PLSE8ODhjZXmbZO-avfQ2lUhHbI5Q5vOSm)
- [MySQL InnoDB Architecture](https://dev.mysql.com/doc/refman/8.0/en/innodb-architecture.html)
- [InnoDB Physical Structure](https://dev.mysql.com/doc/refman/8.0/en/innodb-physical-structure.html)

---

## License

Educational project built for learning. See [LEARNINGS.md](./LEARNINGS.md) for detailed database concepts and developer notes.
