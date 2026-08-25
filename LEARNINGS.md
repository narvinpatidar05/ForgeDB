# ForgeDB — Key Learnings & Takeaways

Phase-wise notes jahan hum concepts, decisions, aur gotchas capture karte hain.
Har phase complete hone ke baad yahan append karo.

---

## DB Developer Fundamentals (Har Phase Mein Yaad Rakhna)

Yeh concepts poore project ke liye relevant hain — sirf Phase 1 ke nahi.

### Storage Engine Stack (Bottom → Top)

Real database mein layers hoti hain. Hum abhi **sabse neeche** se shuru kar rahe hain:

```
┌─────────────────────────────────────┐
│  SQL Parser + Query Optimizer       │  ← Phase 3+
├─────────────────────────────────────┤
│  Executor (INSERT/SELECT/WHERE)     │  ← Phase 4+
├─────────────────────────────────────┤
│  Index Layer (B+ Tree)              │  ← Phase 6+
├─────────────────────────────────────┤
│  Transaction Manager (ACID)         │  ← Phase 7+
├─────────────────────────────────────┤
│  Buffer Pool (RAM cache)            │  ← ✅ Done (LRU cache)
├─────────────────────────────────────┤
│  Page Manager (fixed-size pages)    │  ← ✅ Done (16KB slotted pages)
├─────────────────────────────────────┤
│  Row Codec (wire format)            │  ← ✅ Done (field-by-field, 58 bytes)
├─────────────────────────────────────┤
│  Disk I/O (read/write bytes)        │  ← ✅ Done (page-level I/O)
└─────────────────────────────────────┘
         ↕
    Physical Disk (data/mydb.db — sequence of 16KB pages)
```

**Takeaway:** Storage engine sirf "file read/write" nahi hai — uske upar pages, caching, indexing, transactions, concurrency sab build hota hai. Ab humne **InnoDB-style storage core ka skeleton** build kiya hai: pages + slotted layout + buffer pool.

---

### Concepts Jo DB Developer Ko Pata Hone Chahiye

#### 1. Persistence vs Durability

| Term | Meaning | ForgeDB Phase 1 |
|------|---------|-----------------|
| **Persistence** | Data RAM se survive kare, disk pe rahe | ✅ `mydb.db` file mein data rehta hai program band hone ke baad |
| **Durability** | Committed data crash/power loss ke baad bhi safe rahe | ❌ Abhi nahi — `file.write()` ke baad OS buffer flush guarantee nahi, WAL nahi |

**Real engines:** InnoDB **WAL (Write-Ahead Log)** use karta hai — pehle log disk pe likho, phir data page update karo. Crash pe log se recover karo.

---

#### 2. Row vs Page vs File

| Unit | Size | Purpose |
|------|------|---------|
| **Row (Tuple)** | Variable/fixed bytes | Ek record — ek user, ek order |
| **Page (Block)** | Fixed (usually 4KB–16KB) | Disk I/O ka minimum unit — OS ek baar mein poora page read/write karta hai |
| **File / Tablespace** | GB–TB | Pages ka collection — ek table ya database |

**ForgeDB abhi:** Rows **16KB pages** ke andar slotted format mein store hote hain. File = contiguous sequence of pages.

**Real engines:**
- **InnoDB:** 16KB pages, har page mein multiple rows (slotted page format)
- **SQLite:** 4KB pages, B-tree pages alag type ke hote hain (leaf vs internal)
- **PostgreSQL:** 8KB pages, heap pages + index pages alag

**Kyun pages?** Disk seek slow hai (~ms). Ek baar 16KB page read karke usme se 100 rows nikalna, 100 alag seeks se ek row padhne se **bahut faster** hai.

---

#### 2b. Pages — Deep Dive

**Page kya hai?**

Disk pe data ek continuous byte stream hai, lekin storage engine usse **fixed-size chunks** mein organize karta hai. Ek page = disk I/O ka minimum unit.

```
Table File (e.g. users.ibd)
┌──────────────┬──────────────┬──────────────┬──────────────┐
│   Page 0     │   Page 1     │   Page 2     │   Page N     │
│   16 KB      │   16 KB      │   16 KB      │   16 KB      │
└──────────────┴──────────────┴──────────────┴──────────────┘
```

**InnoDB Slotted Page (real engine) — andar kya hota hai:**

```
┌─────────────────────────────────────────────────────────────┐
│ Page Header: LSN, checksum, free space pointer, slot count  │
├─────────────────────────────────────────────────────────────┤
│ Slot Array (grows →): [offset_row3, offset_row1, ...]     │
├─────────────────────────────────────────────────────────────┤
│                    Free Space                               │
├─────────────────────────────────────────────────────────────┤
│ Row 1 data  │  Row 2 data  │  Row 3 data  (grows ←)       │
└─────────────────────────────────────────────────────────────┘
```

- **Slot array** batati hai har row page ke andar kahan start hoti hai
- Rows **variable-length** ho sakti hain — slot update hoti hai, poori file rewrite nahi
- Delete pe row mark hoti hai "deleted", space reuse ho sakta hai
- **ForgeDB (current):** 16KB slotted pages — slot array + row data, InnoDB-inspired

**Page sizes by engine:**

| Engine | Page Size | Why that size |
|--------|-----------|---------------|
| SQLite | 4 KB | Matches common OS block size, good for embedded |
| PostgreSQL | 8 KB | Balance of I/O efficiency and memory |
| InnoDB | 16 KB | Optimized for SSD/HDD sequential throughput |
| ForgeDB (current) | 16 KB | Matches InnoDB default |

**Buffer Pool connection:**

Pages sirf disk pe nahi — **RAM mein bhi cache** hote hain (Buffer Pool, Phase 8):

```
Query needs Row X
    ↓
Is Page containing Row X in Buffer Pool (RAM)?
    ├── YES → read from RAM (nanoseconds)
    └── NO  → read entire Page from disk (milliseconds), cache it
```

Page layer ke bina buffer pool ka koi matlab nahi — isliye pehle pages, phir cache.

---

#### 2c. Disk Seeks — Deep Dive

**Seek kya hai?**

Jab disk head (HDD) ya flash controller (SSD) **nayi location** pe data padhne ke liye move/access karta hai — us delay ko **seek latency** kehte hain.

```
HDD Disk Platter (top view):

        ┌─────────────────────────┐
        │  Track 0 (outer)        │
        │    Track 1              │
        │      Track 2            │  ← Read head yahan hai
        │        ● Row A          │
        │                         │
        │              ● Row B    │  ← Row B padhne ke liye
        └─────────────────────────┘     head ko move karna = SEEK
                                          (~5–10 ms on HDD)
```

**Seek vs Sequential Read:**

| Access Pattern | What Happens | Speed (HDD) |
|----------------|--------------|-------------|
| **Sequential scan** | Head same track pe rehta hai, continuous read | ~100 MB/s |
| **Random access** | Head har baar nayi track pe jump karta hai | ~100–200 IOPS (seek-bound) |
| **1 seek + read 16KB page** | Ek jump, phir 16KB continuous | Amortized over 100+ rows |

**Example — 1000 row lookups:**

```
Without pages/index (ForgeDB Phase 1 style):
  1000 rows × 1 seek each = 1000 seeks
  1000 × ~5ms = ~5000ms = 5 seconds  😱

With pages (100 rows per 16KB page):
  1000 rows / 100 per page = 10 pages
  10 seeks × ~5ms = ~50ms  ✅

With B+ Tree index (Phase 6):
  Tree height ~3 → 3 page reads to find row
  3 × ~5ms = ~15ms  ✅✅
```

**IOPS (Input/Output Operations Per Second):**

- HDD: ~100–200 random IOPS (seek limited)
- SATA SSD: ~50,000–100,000 IOPS
- NVMe SSD: ~500,000+ IOPS

Seek concept HDD pe zyada painful tha — SSD pe bhi **random vs sequential** ka farq hai, bas seek time microseconds mein hai instead of milliseconds.

**ForgeDB Phase 1 mein seeks:**

```cpp
// select_all() — sequential read, actually GOOD for I/O
while (file.read(..., sizeof(Row)))  // head move nahi, continuous read

// Hypothetical SELECT WHERE id=5 (Phase 1):
// Poori file scan — CPU bound for small files,
// but on 1M rows = 1M × 60 bytes read sequentially
// Still O(n) even if sequential — indexing chahiye
```

**Key takeaway:**

> Pages = **I/O efficiency** (kam seeks)
> Indexes = **lookup efficiency** (kam data padhna)
> Dono alag problems solve karte hain. Phase 1 mein na pages hain na indexes — isliye sirf demo-scale data ke liye OK hai.

---

#### 3. Fixed-Size vs Variable-Length Rows

| Approach | Pros | Cons | Used By |
|----------|------|------|---------|
| **Fixed-size** (humara) | Random access O(1): `offset = n * row_size` | Wasted space (name[50] mein "Ali" = 47 bytes waste) | Early systems, some embedded DBs |
| **Variable-length** | Space efficient | Row location track karna padta hai (offset table / slotted page) | InnoDB, PostgreSQL, SQLite |

**ForgeDB:** Fixed-size `Row` struct — `char name[50]` chahe "A" ho ya "Atharv", disk pe 50 bytes hi lega.

**InnoDB:** Har row ka header hota hai + variable columns. NULL bitmap, overflow pages for large TEXT/BLOB.

---

#### 4. Serialization & Endianness

**Pehle (Phase 1):** `reinterpret_cast` se poora struct disk pe — padding bug, `sizeof(Row)=60` vs actual 58 bytes data.

**Ab (row_codec):** Field-by-field `memcpy` — explicit wire format:
```
Offset 0:  id   (4 bytes)
Offset 4:  name (50 bytes)
Offset 54: age  (4 bytes)
ROW_SIZE = 58 (never use sizeof(Row) on disk)
```

**Abhi bhi ignore kiya (future work):**
- **Endianness:** Native byte order — cross-platform ke liye explicit little-endian encode chahiye
- **Version header:** File ke start mein format version number nahi hai abhi

**Real engines:** Apna **binary format spec** define karte hain — byte-by-byte layout documented hota hai, endianness fixed hoti hai, version number file header mein hota hai.

**SQLite example:** File ke pehle 16 bytes mein magic string `"SQLite format 3\000"` + page size + schema version.

---

#### 5. Sequential Scan vs Index Lookup

| Operation | ForgeDB Phase 1 | With Index (Phase 6+) |
|-----------|-----------------|----------------------|
| `SELECT *` | Poori file padho O(n) | Poori file/table scan — same |
| `SELECT WHERE id=5` | Poori file padho O(n) | B+ Tree se direct page O(log n) |

**Takeaway:** Bina index ke har query **full table scan** hai. 1 million rows pe yeh unacceptable hai — isliye indexing critical hai.

---

#### 6. ACID (Abhi Humare Paas Kuch Nahi)

| Property | Meaning | ForgeDB | InnoDB |
|----------|---------|---------|--------|
| **A**tomicity | All-or-nothing | ❌ | ✅ Undo logs |
| **C**onsistency | Valid state always | ❌ | ✅ Constraints + FK |
| **I**solation | Concurrent txs don't interfere | ❌ | ✅ MVCC + locks |
| **D**urability | Committed = permanent | ❌ | ✅ WAL + fsync |

Phase 1 mein ACID ka koi piece nahi — yeh normal hai, hum layer-by-layer build karenge.

---

## Famous Storage Engines — Kaise Kaam Karte Hain

### InnoDB (MySQL default engine)

```
Table → Tablespace (.ibd file)
         └── Pages (16KB each)
               └── Rows (slotted — variable size)
         └── B+ Tree indexes (separate pages)
         └── Undo log (MVCC + rollback)
         └── Redo log (WAL — crash recovery)
         └── Buffer Pool (RAM — hot pages cached)
```

**Row storage:** Slotted page format — page ke andar slot array hoti hai jo batati hai row kahan start hoti hai. Row move/delete ho sakti hai bina poori file rewrite kiye.

**Key ideas hum seekhenge:**
- Page-based I/O (Phase 5)
- B+ Tree indexing (Phase 6)
- WAL + transactions (Phase 7+)

---

### SQLite (Embedded, Single-File)

```
database.db (single file)
  ├── Page 1: Schema (sqlite_master table)
  ├── Page 2–N: B-tree pages (table data + indexes)
  └── WAL file (optional, -wal suffix)
```

**Simplicity:** Ek file, ek process, no server. ForgeDB guide isi se inspired hai (cstack/db_tutorial).

**Difference from us:** SQLite ke paas already B-tree, SQL parser, transactions hain. Hum abhi unse **~10+ phases peeche** hain.

---

### PostgreSQL (Heap + Indexes)

```
Table → Heap file (unordered rows, insert order)
Indexes → Separate B-tree / GiST / GIN files
```

**Key difference from InnoDB:** PostgreSQL mein table data **unordered heap** hai — indexes alag structure hain jo row location (TID = block_id + offset) point karte hain. InnoDB mein primary key = clustered index (data sorted by PK).

---

### LevelDB / RocksDB (LSM-Tree, Key-Value)

```
Write → MemTable (RAM) → flush → SSTable (sorted file on disk)
Read → MemTable + SSTables merge
Compaction → background merge of SSTables
```

**Different paradigm:** SQL rows nahi — key-value pairs. Used by Cassandra, RocksDB (Facebook). LSM = write-optimized (append-heavy), B+ Tree = read-optimized.

**Hum is path pe nahi ja rahe** — relational B+ Tree path follow kar rahe hain (MySQL/InnoDB style).

---

## ForgeDB vs Real Engines — Gap Analysis

### Feature Comparison (Current vs Real Engines)

| Feature | ForgeDB (current) | SQLite | InnoDB |
|---------|-------------------|--------|--------|
| Disk persistence | ✅ Page-based | ✅ | ✅ |
| Binary row format | ✅ Explicit wire format (58B) | ✅ Custom | ✅ Slotted page |
| Page layer | ✅ 16KB | ✅ 4KB | ✅ 16KB |
| Slotted pages | ✅ | ✅ | ✅ |
| Buffer pool / cache | ✅ LRU (basic) | ✅ | ✅ |
| SQL interface | ❌ | ✅ | ✅ |
| Indexes | ❌ | ✅ B-tree | ✅ B+ tree |
| WHERE / filtering | ❌ | ✅ | ✅ |
| Transactions (ACID) | ❌ | ✅ | ✅ Full MVCC |
| Crash recovery (WAL) | ❌ | ✅ | ✅ Redo log |
| Concurrent access | ❌ | ✅ | ✅ Multi-writer |

### How Far Are We? (Updated)

```
Production InnoDB/MySQL:  ████████████████████  100%
SQLite:                   ████████████████░░░░   ~80%
ForgeDB (current):        ████████░░░░░░░░░░░░   ~30–40% storage core
ForgeDB (+ B+Tree/WAL):   ██████████░░░░░░░░░░   ~50% (target)
```

**Ab kya complete hai:** Storage core skeleton — pages, slotted layout, buffer pool, wire format.  
**Ab kya missing hai:** B+Tree, WAL, SQL layer, transactions — but unki foundation ready hai.

---

### Kya Phase 1 Ne Sahi Seekhaya?

**Haan — yeh foundations hain jo har engine share karta hai:**

1. Data ultimately **bytes on disk** hain — sab kuch iske upar build hota hai
2. **Fixed-size records** se random access samajh aati hai (indexing ka base)
3. **Append-only write** simple hai aur WAL ka precursor hai
4. **Full scan** ka cost feel hota hai — indexing ki motivation clear hoti hai
5. **Persistence ≠ durability** — file likhna kaafi nahi crash safety ke liye

---

## Phase 1: Storage Engine — Row Storage on Disk

**Status:** Complete  
**Branch:** `main` (commit: `first commit`)

### Concept

Database ka sabse neeche wala layer ek **plain binary file** hai. Abhi koi SQL, parser, ya REPL nahi — sirf C++ struct ko raw bytes ki tarah disk pe likhna/padhna.

Hardcoded table: `Row` struct hi abhi "users table" ka schema hai. Dynamic `CREATE TABLE` Phase 3 mein aayega.

### Project Flow

```
./build/mydb
    ↓
main.cpp          → entry point, demo rows banata hai
    ↓
row.h             → Row struct (schema — fixed 60 bytes)
    ↓
storage.cpp       → insert_row / select_all (disk I/O)
    ↓
data/mydb.db      → actual database file (runtime, gitignored)
```

### Key Files

| File | Role |
|------|------|
| `include/row.h` | Table schema — `id`, `name[50]`, `age` |
| `include/storage.h` | Function declarations |
| `src/storage.cpp` | Binary read/write via `fstream` |
| `src/main.cpp` | Entry point — insert 2 rows, select all, print |

### Commands

```bash
# Build
g++ -std=c++17 -Iinclude src/main.cpp src/storage.cpp -o build/mydb

# Run
./build/mydb

# Verify binary on disk
xxd data/mydb.db

# Fresh start (clear old data)
rm data/mydb.db && ./build/mydb
```

### Core Learnings (Code-Level)

1. **Serialization = raw memory copy**
   - `file.write(reinterpret_cast<const char*>(&row), sizeof(Row))` struct ke bytes bina conversion ke disk pe likhta hai
   - `reinterpret_cast` compiler ko bolta hai: "in bytes ko struct nahi, raw bytes ki tarah treat karo"

2. **Fixed-size rows**
   - `char name[50]` use kiya, `std::string` nahi — variable length abhi handle nahi hota
   - Har row disk pe exactly `sizeof(Row)` bytes (60 bytes, padding included)
   - Row #3 locate karo: `file.seekg(3 * sizeof(Row))` — O(1) random access

3. **Binary vs text mode**
   - `std::ios::binary` — raw bytes, line ending / encoding conversion nahi
   - Text editor mein file garbage dikhegi — yeh confirm karta hai binary format hai

4. **Append mode = persistence + duplicates**
   - `std::ios::app` har insert file ke **end** pe append karta hai
   - Program band karke dubara chalane pe purana data rehta hai — **yahi persistence hai**
   - Har run pe same inserts → duplicate rows (expected in Phase 1, not a bug)

5. **Compiler padding**
   - Guide ne 58 bytes estimate kiya tha, actual `sizeof(Row) = 60` — alignment padding ki wajah se
   - Real engines apna layout **explicitly define** karte hain, struct padding pe depend nahi karte

6. **File location**
   - Path: `./data/mydb.db` (program ke working directory se relative)
   - `data/` folder pehle se exist karna chahiye; file pehli write pe auto-create hoti hai

### Phase 1 → Production Path (Hum Kahan Ja Rahe Hain)

```
Phase 1  ✅  Raw row append/read          ← hum yahan hain
Phase 2     REPL shell
Phase 3     SQL parser + CREATE TABLE
Phase 4     Executor (INSERT/SELECT/WHERE)
Phase 5     Page manager (4KB/16KB blocks)     ← InnoDB jaisa layer shuru
Phase 6     B+ Tree index                      ← O(log n) lookups
Phase 7     WAL + basic transactions           ← Durability + Atomicity
Phase 8     Buffer pool                        ← RAM caching
Phase 9+    Concurrency, MVCC                  ← Isolation
```

### Do / Don't (Phase 1)

**Do:**
- Fixed-size fields se start karo
- Har insert ke baad `select_all` se manually verify karo
- `xxd` se binary format confirm karo
- Modules alag rakho (`row`, `storage`, `main`)

**Don't:**
- `std::string` struct fields mein mat rakho (Phase 3+ problem)
- WHERE, indexing, multiple tables abhi mat sochna
- Raw `FILE*` aur `fstream` mix mat karo
- Assume karo ki file likhna = durable (fsync/WAL chahiye production mein)

### Gotchas We Hit

| Issue | Cause | Fix |
|-------|-------|-----|
| Duplicate rows on re-run | `ios::app` + no file clear | `rm data/mydb.db` before fresh test |
| `sizeof(Row)` ≠ expected | Compiler padding | Always print and verify |
| 4 rows after 2 runs | Persistence working correctly | Expected behavior |

### Interview-Ready One-Liners (Phase 1)

- **"Storage engine kya hai?"** — Database ka layer jo disk pe data ka layout, read/write, aur indexing manage karta hai. SQL parser ke neeche, OS file system ke upar.

- **"Fixed-size vs variable-length rows?"** — Fixed = O(1) offset calc, space waste. Variable = space efficient, slot directory / offset table chahiye.

- **"Page kyun?"** — Disk I/O minimum unit; ek seek mein 16KB lao, usme 100 rows — 100 seeks se better.

- **"WAL kya hai?"** — Pehle log likho, phir data update. Crash pe log replay se recover. Durability ka standard approach.

- **"Humne kya build kiya?"** — Simplest possible persistent store: struct → raw bytes → file append. Production engine ka ~5%, lekin core idea same hai.

### What's Next

Phase 2: REPL — interactive `mydb>` shell (`.exit`, `.help`, echo). User input lena shuru — abhi bhi storage same rahega.

---

## Storage Engine Evolution: Flat File → Page Manager + Buffer Pool

**Status:** Complete  
**Branch:** `feat/page-manager`

Yeh section capture karta hai: pehle kya tha, ab kya hai, **kyun** change kiya, **kaise** kaam karta hai, aur **long-term** mein yeh kaise help karega.

---

### "Storage Engine" Ka Matlab — Proper Definition

Jab tum MySQL mein likhte ho:
```sql
INSERT INTO users VALUES (1, 'atharv', 20);
```

Do alag layers kaam karti hain:

| Layer | Kya karta hai | Example |
|-------|---------------|---------|
| **SQL Layer** | Text samjho, plan banao | Parser, Optimizer, Executor |
| **Storage Engine** | Bytes disk pe kaise store/read karo | InnoDB, MyISAM |

**Storage engine** = woh subsystem jo:
- Rows ko disk pe **layout** karta hai (pages, slots)
- **I/O unit** decide karta hai (row vs page)
- **Cache** manage karta hai (buffer pool)
- **Indexes** store karta hai (future: B+Tree leaf nodes = pages)
- **Durability** guarantee karta hai (future: WAL)

Pehle ForgeDB mein sirf `fstream` + flat file tha — woh **persistence demo** tha, poora storage engine nahi. Ab humare paas woh layers hain jo InnoDB ke andar bhi hain (simplified).

---

### Pehle Kya Tha (Flat File Phase)

```
Row struct (RAM, 60 bytes with padding)
    ↓ reinterpret_cast — WHOLE struct copy
    ↓ sizeof(Row) bytes written
Flat file append (data/mydb.db)
    ↓ select_all: read sizeof(Row) chunks sequentially
Print rows
```

**Architecture:**
```
main.cpp → storage.cpp → flat file
              (no pages, no cache, no wire format)
```

**Problems:**

| Problem | Impact |
|---------|--------|
| Compiler padding in on-disk format | 60 bytes on disk, 58 bytes real data — non-portable |
| Row = I/O unit | 1000 inserts ≈ 1000 separate writes |
| No page abstraction | Can't cache efficiently |
| Fixed offset = row# × sizeof(Row) | Delete/update impossible without shifting all data |
| Full file scan for every query | O(n) always, no index path |
| No RAM cache | Every read hits disk |

**Yeh theek tha for:** learning persistence, seeing bytes on disk with `xxd`.  
**Yeh theek nahi tha for:** building anything InnoDB-like on top.

---

### Ab Kya Hai (Page Manager + Buffer Pool)

```
Row struct (RAM)
    ↓ row_codec: field-by-field serialize (58 bytes, portable)
    ↓ Page::insert_row() — slotted page layout
    ↓ BufferPool: cache page in RAM, mark dirty
    ↓ PageManager: write full 16KB page to disk
Disk file = Page0 | Page1 | Page2 | ... (each 16KB)
```

**Architecture:**
```
main.cpp
    ↓
StorageEngine          ← top-level API (insert_row, select_all)
    ↓
BufferPool             ← LRU cache, hit/miss counters, dirty tracking
    ↓
PageManager            ← read_page / write_page / allocate_new_page
    ↓
Page (slotted)         ← 16KB, header + rows + slot array
    ↓
row_codec              ← 58-byte wire format
    ↓
data/mydb.db
```

---

### Layer-by-Layer: Kya, Kyun, Kaise

#### 1. Row Codec (`row_codec.h/cpp`)

**Kya:** `serialize_row()` / `deserialize_row()` — explicit 58-byte layout.

**Kyun:**
- `sizeof(Row) = 60` includes 2 bytes compiler padding between `name[50]` and `age`
- Padding bytes = garbage on disk, layout compiler-specific
- Page Manager ko **fixed, known row size** chahiye for space math

**Kaise:**
```cpp
memcpy(buf + 0,  &row.id,   4);
memcpy(buf + 4,   row.name,  50);
memcpy(buf + 54,  &row.age,  4);
// ROW_SIZE = 58, always
```

**Long-term help:** B+Tree leaf nodes will store these exact 58-byte records. Index pages won't care about C++ struct layout — only wire format.

---

#### 2. Page — Slotted Page Format (`page.h/cpp`)

**Kya:** Fixed 16KB block with header, row data area, and slot array.

**Physical layout:**
```
┌──────────────────────────────────────────────────────────────┐
│ HEADER (16B)  │ Row1 │ Row2 │ Row3 │ free │ slot2│slot1│slot0│
│ page_id       │  ↑ data grows →              ↑ slots from end │
│ free_offset   │                                              │
│ slot_count    │              16 KB total                     │
│ lsn (unused)  │                                              │
└──────────────────────────────────────────────────────────────┘
```

**Kyun slotted pages (InnoDB-style):**
- **Old flat file:** row #5 at offset `5 × 60` — delete row #3 = shift everything after it
- **Slotted page:** slot array maps logical index → byte offset. Delete = mark slot empty, reuse space later
- Multiple rows per page = **one disk read gets 272 rows** (not 272 disk reads)

**Kaise insert:**
1. Check space: `row_end + (slot_count+1) × 2 <= PAGE_SIZE`
2. Write 58 bytes at `free_space_offset`
3. Store offset in slot array at page tail
4. Increment `slot_count`, advance `free_space_offset`

**Rows per page:** `(16384 - 16) / (58 + 2) = 272` rows/page  
**1000 rows = 4 pages** (verified in demo)

**Long-term help:** B+Tree **leaf nodes are pages**. When you build indexing, you'll store `(key → page_id, slot_index)` pointers into these exact pages.

---

#### 3. Page Manager (`page_manager.h/cpp`)

**Kya:** All disk I/O in units of full 16KB pages.

**Kyun:**
- OS and disk hardware operate on blocks — reading 16KB amortizes seek cost
- InnoDB tablespace = sequence of pages, not sequence of rows
- Buffer pool caches **pages**, not rows — need page-granularity I/O

**Kaise:**
```
Database file layout:
[ Page 0: 16KB ][ Page 1: 16KB ][ Page 2: 16KB ]...

read_page(N)  → seek to N × 16384, read 16384 bytes
write_page(N) → seek to N × 16384, write 16384 bytes
allocate_new_page() → extend file by 16KB, return new page id
```

**Long-term help:** WAL (Write-Ahead Log) will also be page-oriented — log records describe page changes. Transaction manager will read/write pages through this same interface.

---

#### 4. Buffer Pool (`buffer_pool.h/cpp`)

**Kya:** In-memory LRU cache of hot pages.

**Kyun:**
- Disk read ≈ milliseconds, RAM read ≈ nanoseconds
- Same page accessed repeatedly (common in OLTP) → cache hit = no disk I/O
- InnoDB buffer pool can be **GBs of RAM** — this is the same concept, simplified

**Kaise:**
```
get_page(id):
  in cache?  → HIT:  return from RAM, move to LRU front
  not cache? → MISS: read_page from disk, maybe evict LRU victim
              if victim dirty → flush to disk first
```

**Demo results (capacity=2, 4 pages on disk):**
```
After insert:  hits=1000, misses=0   (current page always in RAM)
After select:  hits=1000, misses=4   (4 pages, cache holds 2 → 4 disk reads)
```

**Long-term help:** Query performance tuning in real DBs = buffer pool hit ratio. You'll understand `SHOW ENGINE INNODB STATUS` buffer pool stats because you built one.

---

#### 5. StorageEngine (`storage.h/cpp`)

**Kya:** Top-level coordinator — the API future SQL executor will call.

**Kyun:** SQL layer shouldn't know about pages, slots, or cache. It calls `insert_row(row)` and `select_all()`.

**Kaise insert flow:**
1. `serialize_row(row)` → 58 bytes
2. Try insert on `current_page`
3. Page full? → `allocate_new_page()`, switch `current_page_id`
4. `mark_dirty(current_page_id)`
5. On flush → dirty pages written to disk

**Long-term help:** Phase 4 executor will replace `main.cpp`'s loop with `StorageEngine::insert_row()` calls from parsed SQL AST.

---

### Flat File vs Page Manager — Side by Side

| Aspect | Flat File (pehle) | Page Manager (ab) |
|--------|-------------------|-------------------|
| On-disk row size | 60 bytes (padded) | 58 bytes (explicit) |
| I/O unit | Single row | 16KB page |
| 1000 inserts | ~1000 appends | ~4 page writes |
| Rows per disk read | 1 | Up to 272 |
| RAM cache | None | LRU buffer pool |
| Row delete/update | Impossible cleanly | Slot-based (foundation laid) |
| Random lookup | O(n) full scan | Still O(n) — index next |
| InnoDB similarity | ~5% | ~30–40% storage core |

---

### InnoDB Se Relation — Ab Kahan Khade Hain

```
InnoDB Component              ForgeDB Status
─────────────────────────────────────────────
16KB pages                    ✅ Done
Slotted page format           ✅ Done
Buffer pool (LRU)             ✅ Done (simplified)
Page Manager / tablespace     ✅ Done
Explicit row wire format      ✅ Done
Clustered B+Tree index        ❌ Next
WAL / redo log                ❌ Future
MVCC / transactions           ❌ Future
Query optimizer               ❌ Far future
```

**Key insight:** B+Tree leaf nodes **are pages**. Getting pages wrong = reworking B+Tree later. Isliye pehle Page Manager solid kiya.

---

### Gotchas We Hit (Page Manager Build)

| Issue | Cause | Fix |
|-------|-------|-----|
| Garbled rows on read | Slot write/read used different offset formulas | Unified: `slot i` at `PAGE_SIZE - (i+1) × 2` for both read and write |
| `memcpy` overflow warning | `PageHeader` was 12 bytes, `PAGE_HEADER_SIZE` was 16 | Added `reserved` field + `static_assert` |
| Old `.db` file unreadable | Format changed: 60-byte rows → 16KB pages | Delete old file before testing |
| All cache hits on insert | Current page stays in buffer pool | Expected — only select with capacity < page_count shows misses |

---

### Long-Term Learning Value

**Interview / job mein:**
- "Explain InnoDB buffer pool" — tumne LRU cache khud likha hai
- "What is a slotted page?" — tumne slot array implement kiya hai
- "Why pages not rows for I/O?" — tumne 1000 rows → 4 pages demo dekha hai
- "Storage engine vs SQL layer" — tumhare paas dono layers alag hain codebase mein

**Next build steps (in order):**
1. **B+Tree index** — leaf nodes = pages you built today
2. **WAL** — log page changes before writing dirty pages
3. **SQL parser + executor** — calls `StorageEngine` API
4. **MVCC** — multiple row versions in slotted pages

**Mental model:**
```
SQL text
  → Parser/Executor (future)
    → StorageEngine (you have this)
      → BufferPool (you have this)
        → PageManager (you have this)
          → Slotted Pages (you have this)
            → Row Codec (you have this)
              → Disk
```

Har layer jo ab build hui, woh future layers ki **foundation** hai — skip nahi kar sakte.

---

### Verification Checklist (Jo Humne Run Kiya)

```bash
rm -f data/mydb.db
./build/mydb
```

Expected:
- `ROW_SIZE = 58`, `sizeof(Row) = 60`
- 1000 rows inserted, 4 pages on disk
- First row: `1 | user_1`, Last row: `1000 | user_1000`
- Buffer pool misses > 0 on select (with capacity < page count)
- `PASS: Multiple rows share pages`

---

### Interview One-Liners (Page Manager)

- **"Storage engine kya hai?"** — Disk pe data layout, I/O, caching, aur indexing manage karne wala layer. SQL parser ke neeche, OS ke upar.

- **"Slotted page kyun?"** — Row offsets track karo bina data shift kiye. Delete/update ke liye foundation. InnoDB ka standard format.

- **"Buffer pool hit vs miss?"** — Hit = page RAM mein thi, disk read nahi hua. Miss = disk se 16KB page load karni padi.

- **"Page size 16KB kyun?"** — OS block size aur sequential throughput ka balance. InnoDB default. Ek seek mein 272 rows mil sakte hain.

- **"Flat file se page manager pe kyun aaye?"** — Row-level I/O seek-bound hai. Page-level I/O + cache = production storage engine ka minimum viable core.

---

## Phase 2: REPL (Read-Eval-Print Loop)

**Status:** Not started  
*(Append learnings here after Phase 2 is complete)*

---
