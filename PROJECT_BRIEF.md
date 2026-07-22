# linkrot — Complete Codebase Reference for a New Contributor

> **Purpose**: This document gives a new contributor a complete, accurate mental model of the codebase in one read. No speculation — only what exists in the repository as of the current commit.

---

## 1. Project Identity

| Attribute        | Value                                                                                                            |
| ---------------- | ---------------------------------------------------------------------------------------------------------------- |
| **Name**         | linkrot                                                                                                          |
| **Language**     | C17 (strict: `-std=c17 -Wall -Wextra -Wpedantic -Wstrict-prototypes -Wmissing-prototypes -Wshadow -Wconversion`) |
| **Platforms**    | macOS, Linux                                                                                                     |
| **Dependencies** | `argp-standalone` on macOS (Homebrew), `pthread`, C library                                                      |
| **Binary**       | Single executable `./linkrot`                                                                                    |
| **License**      | MIT                                                                                                              |
| **Philosophy**   | _Do one thing well._ Find and clean up broken symlinks. No config files, no env vars. All config via CLI flags.  |

**Not a framework.** Not a library. A single-purpose cleanup tool.

---

## 2. High-Level Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                        main thread                            │
│  argp CLI → log_init → queue_create → worker_pool_start      │
│                                                    ↓         │
│                                            traverse(cfg, q)  │
│                                            (blocks until     │
│                                             walk finishes)   │
│                                                    ↓         │
│                                          queue_shutdown()    │
│                                          worker_pool_wait()  │
│                                          print summary       │
└──────────────────────────────────────────────────────────────┘

┌──────────────────┐     enqueue      ┌─────────────────┐     dequeue     ┌──────────────────┐
│   Traversal      │ ───────────────► │   Bounded Queue  │ ──────────────► │    Workers       │
│   (1 thread)     │                  │   (1024 slots)   │                 │   (N threads)    │
│                  │                  │   circular buf    │                 │                  │
│   nftw() walk    │                  │   mutex + 2 cv    │                 │   glob match     │
│   depth filter   │                  └─────────────────┘                 │   stat check     │
│   symlink filter │                                                       │   readlink       │
│   strdup + push  │                                                       │   delete / print │
└──────────────────┘                                                       └──────────────────┘
```

**Key invariants:**

- Traversal thread does minimal work — only walks and enqueues paths.
- Workers do all real work — glob, stat, readlink, delete.
- Each worker owns its own `WorkerStats` — no atomics during work, merged at the end.
- `Config` is read-only after `config_parse()` — safe to share without locking.
- Paths are `strdup`'d by traversal, `free`'d by workers after processing.

---

## 3. Source Tree

```
src/
├── main.c              Entry point, orchestration, summary output
├── config.c / .h       Config struct, argp parsing, flag validation
├── queue.c / .h        Thread-safe bounded queue (circular buffer + mutex + cv)
├── traverse.c / .h     nftw-based filesystem walk, pushes paths into queue
├── worker.c / .h       Thread pool, per-worker stats, output mutex
├── symlink.c / .h      Pure logic: glob match, broken detection, delete
├── log.c / .h          Thread-safe colorized logger (unchanged, not owned)
└── project_config.h    VERSION, HOMEPAGE_URL, SHORT_DESC, AUTHOR constants
```

**No subdirectories.** Flat `src/` with `.c/.h` pairs. `Makefile` uses `$(wildcard src/*.c)`.

---

## 4. Module Deep Dive

### 4.1 `main.c` — Orchestration

- `config_parse()` → `Config*` (argp, validation, CPU count detection).
- `log_init(cfg->log_level)` — must be called before any `LOG_*` macro.
- Debug mode: prints all `argv[]` at `LOG_DEBUG`.
- `queue_create(1024)` — bounded buffer for path passing.
- `worker_pool_start(queue, cfg, cfg->num_workers)` — spawns N threads.
- `traverse(cfg, queue)` — blocks until `nftw()` finishes.
- `queue_shutdown(queue)` — signals workers to drain and exit.
- `worker_pool_wait(&stats)` — joins all threads, sums per-worker counters.
- Summary: prints found/deleted/errors, returns `(errors > 0) ? 1 : 0`.

### 4.2 `config.c / .h` — CLI & Configuration

```c
typedef struct {
    bool        dry_run;        // -n
    bool        delete;         // -D
    bool        print;          // -P
    bool        null;           // -0
    int         max_depth;      // -L (-1 = unlimited)
    int         num_workers;    // -j (default: sysconf(_SC_NPROCESSORS_ONLN))
    const char *dir;            // positional [DIR]
    const char *glob;           // -G (default: "**/*")
    const char *delete_cmd;     // -C
    Log_level_t log_level;      // -l (default: LOG_LEVEL_INFO)
} Config;
```

- Static `cfg` initialized with defaults, mutated by `parse_opt()`.
- After argp: validates mutual exclusion (`-D`, `-P`, `-C`), validates `-n` requires `-D` or `-C`, clamps worker count to 1–128.
- Returns pointer to static `cfg` — valid for entire process lifetime.

### 4.3 `queue.c / .h` — Thread-Safe Bounded Queue

```c
struct Queue {
    char         **buf;       // circular buffer
    int            capacity;  // 1024
    int            head;      // read index
    int            tail;      // write index
    int            count;     // current items
    bool           shutdown;  // termination flag
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
};
```

- `queue_push()`: blocks via `pthread_cond_wait()` when full. No-op after shutdown.
- `queue_pop()`: blocks when empty. Returns `NULL` after `queue_shutdown()`.
- `queue_shutdown()`: sets flag, broadcasts both condvars.
- `queue_destroy()`: frees mutex, condvars, buffer, struct.

### 4.4 `traverse.c / .h` — Filesystem Walk

- Module-level static pointers (`t_cfg`, `t_queue`) set before `nftw()`.
- `walk_callback()`:
  1. Depth check: skip if `ftwbuf->level > max_depth`.
  2. Type filter: only `FTW_SL` (symlink, not followed).
  3. `strdup(fpath)` → `queue_push()` — ownership transfers to queue.
- Uses `nftw()` with `FTW_PHYS` (don't follow symlinks during walk).

### 4.5 `worker.c / .h` — Thread Pool

```c
typedef struct {
    long found;
    long deleted;
    long errors;
} WorkerStats;
```

- `worker_pool_start()`: allocates `pthread_t[]` and `WorkerStats[]`, spawns threads.
- `worker_fn()` (per thread): loops on `queue_pop()`, processes each path:
  1. `symlink_matches_glob()` — short-circuits on `**/*` (default).
  2. `symlink_is_broken()` — stat check + readlink.
  3. Dispatch: print (with `output_mutex`) / dry-run / delete / info.
  4. `free(path)` — worker owns the `strdup`'d path.
- `worker_pool_wait()`: joins all threads, sums stats into `out`.
- `output_mutex`: protects stdout writes in `-P` mode (LOG_* macros are already internally mutex-protected).

### 4.6 `symlink.c / .h` — Pure Logic

```c
bool symlink_matches_glob(const char *path, const char *pattern);
bool symlink_is_broken(const char *path, char *target_buf, size_t target_len);
int  symlink_delete(const char *path, const char *delete_cmd);
```

- `symlink_matches_glob()`: `fnmatch()` wrapper. Returns `true` immediately for `**/*`.
- `symlink_is_broken()`: `stat()` → if success, target exists (not broken). If `ENOENT`/`ENOTDIR`/`ELOOP`, broken. Reads target via `readlink()`.
- `symlink_delete()`: if `delete_cmd`, `fork()` + `execlp()` + `waitpid()`. Else `unlink()`. Returns 0 on success, -1 on failure.

### 4.7 `log.c / .h` — Logger (Unchanged)

- Thread-safe via `pthread_mutex`.
- Levels: `OFF`, `FATAL`, `ERROR`, `WARN`, `INFO`, `DEBUG`, `TRACE`.
- Compile-time flags: `LOG_SHOW_SOURCE_LOCATION`, `LOG_SHOW_TIME_STAMP`.
- Auto-detects TTY for ANSI colour.
- `log_init(level)` sets level and TTY detection. Must be called before first `LOG_*`.

### 4.8 `project_config.h` — Metadata

```c
#define PROJECT_VERSION       "1.1.0"
#define PROJECT_HOMEPAGE_URL  "https://github.com/pritam12426/linkrot"
#define PROJECT_SHORT_DESC    "Find and clean up broken symlinks..."
#define AUTH_MESSAGE          "Author: Pritam <...>"
```

---

## 5. Execution Flow (Happy Path)

```
 1. main: config_parse(argc, argv) → Config*
 2. main: log_init(cfg->log_level)
 3. main: queue_create(1024)
 4. main: worker_pool_start(queue, cfg, num_workers)
         → spawns N threads, each loops on queue_pop()
 5. main: traverse(cfg, queue)
         → nftw() walks directory tree
         → walk_callback() filters symlinks, strdup + queue_push()
 6. main: queue_shutdown(queue)
         → signals workers: no more items coming
 7. main: worker_pool_wait(&stats)
         → workers drain remaining items, exit
         → joins all threads, sums stats

 Each worker (concurrent):
  a. queue_pop() → char* path
  b. symlink_matches_glob(path, cfg->glob) → skip if no match
  c. symlink_is_broken(path, target_buf) → skip if not broken
  d. stats.found++
  e. Dispatch:
     - print mode:    puts(path) under output_mutex
     - dry-run:       LOG_INFO("[dry-run] %s -> %s", ...)
     - delete mode:   symlink_delete(path, cfg->delete_cmd)
     - default:       LOG_INFO("%s -> %s", ...)
  f. free(path)

 8. main: print summary
 9. main: queue_destroy(queue)
```

---

## 6. Build System

### `Makefile` (top-level)

```make
# Auto-discovers sources
SRC = $(wildcard src/*.c)
HEADERS = $(wildcard src/*.h)

# Build options (via command line)
O_DEBUG := 0                     # enable debug build
O_LOG_SHOW_SOURCE_LOCATION := 0  # prepend [file:line:func]
O_LOG_SHOW_TIME_STAMP := 0       # prepend [HH:MM:SS.ffffff]

# make debug auto-enables all three
make debug    # -g3 -DDEBUG -DLOG_SHOW_SOURCE_LOCATION -DLOG_SHOW_TIME_STAMP
              # + ASan + UBSan + -fstack-usage + -ffreestanding (clang)

make          # -O3

# macOS: needs argp-standalone (brew install argp-standalone)
LDLIBS += -largp
```

### No dependency tracking

- Headers not in `makefile` deps. Run `make clean` after header changes.

---

## 7. Testing

No test framework. Manual validation only:

```sh
# Create fixtures
mkdir -p /tmp/linkrot_test
ln -sf /nonexistent /tmp/linkrot_test/broken1
ln -sf /nonexistent /tmp/linkrot_test/broken2
ln -sf /tmp /tmp/linkrot_test/working

# All modes
./linkrot /tmp/linkrot_test              # default
./linkrot -P /tmp/linkrot_test           # print paths
./linkrot -n -D /tmp/linkrot_test        # dry-run
./linkrot -D /tmp/linkrot_test           # delete
./linkrot -j 4 -D /tmp/linkrot_test      # custom workers
./linkrot -l trace /tmp/linkrot_test     # verbose

# Validation checks
./linkrot -D -P /tmp/linkrot_test        # should error (mutual exclusion)
./linkrot -n /tmp/linkrot_test           # should error (-n needs -D or -C)
```

---

## 8. Key Design Decisions

| Decision                        | Why                                                                  |
| ------------------------------- | -------------------------------------------------------------------- |
| Producer-consumer with queue    | Decouples traversal from processing; enables parallel deletes        |
| nftw for traversal              | Portable, handles depth limiting, one syscall per dir entry          |
| Glob in workers, not traversal  | Keeps traversal fast; queue carries raw paths                        |
| Per-worker stats (no atomics)   | Avoids contention; merge once at the end                             |
| Bounded queue (1024)            | Backpressure: prevents traversal from outrunning slow workers        |
| output_mutex for print mode     | Prevents interleaved stdout lines; LOG_* are already mutex-protected |
| Config is read-only after parse | No locking needed across threads                                     |
| strdup/free ownership model     | Clear: traversal allocates, worker frees                             |
| PATH_MAX fallback define        | Needed because `-ffreestanding` strips `<limits.h>` in debug builds  |

---

## 9. Known Limitations (By Design)

- **No resume/cleanup on crash** — interrupted deletes are not rolled back.
- **No dry-run log to file** — `-n` output goes to stderr only.
- **No parallel traversal** — single `nftw()` thread; parallelism is in workers only.
- **No exclude patterns** — only positive glob (`-G`), no `--exclude`.
- **Non-deterministic output order** — workers process concurrently; order depends on scheduling.

---

## 10. Files an Agent Might Need to Touch

| Task                        | Files                                       |
| --------------------------- | ------------------------------------------- |
| Add CLI flag                | `config.h` (struct), `config.c` (argp)      |
| Add new action mode         | `worker.c` (dispatch chain)                 |
| Change queue size           | `queue.c` or `main.c` (hardcoded 1024)      |
| Adjust worker count default | `config.c` (sysconf fallback)               |
| Add glob behavior           | `symlink.c` (matches_glob)                  |
| Change deletion behavior    | `symlink.c` (symlink_delete)                |
| Modify traversal filtering  | `traverse.c` (walk_callback)                |
| Add new log usage           | Any file — use `LOG_INFO`, `LOG_ERROR`, etc |

---

## 11. Mental Model Checklist

- [ ] Single binary, no runtime deps, CLI-only config
- [ ] Traversal thread = walk + enqueue only (minimal work)
- [ ] Workers = all real work (glob, stat, readlink, delete)
- [ ] Bounded queue provides backpressure
- [ ] Per-worker stats, merged once at shutdown
- [ ] Config is read-only after parse — no locking needed
- [ ] `strdup` by traversal, `free` by worker — clear ownership
- [ ] `output_mutex` only for `-P` mode stdout writes
- [ ] LOG_* macros are already thread-safe internally
- [ ] No parallel traversal — only parallel processing
- [ ] No tests — manual validation only

---

## 12. Quick Commands

```bash
# Build release
make

# Build debug (ASan + UBSan)
make debug

# Clean
make clean

# Show help
./linkrot --help

# Show version
./linkrot --version
```

---

_Generated from codebase inspection. Update when architecture changes._
