# linkrot — Developer Guide

## Architecture

Producer-consumer with a bounded queue:

```
Traversal (1 thread)  →  Queue (1024 slots)  →  Workers (N threads)
  nftw() walk               circular buffer        glob match
  enqueue paths             mutex + cv              stat / readlink
                                                 delete / print
```

**Traversal thread** runs `nftw()` and pushes `strdup`'d symlink paths into the queue. It only does depth limiting and symlink-type filtering — all real work happens in workers.

**Worker threads** each pop a path, run glob match → stat check → readlink → action. Each worker has its own `WorkerStats` struct (found/deleted/errors), merged at the end of `worker_pool_wait()`.

**Shutdown** is signaled via `queue_shutdown()`, which broadcasts the condition variables. Workers drain remaining items then exit. `worker_pool_wait()` joins all threads.

## Source layout

### Entry point and orchestration

```
src/main.c
  ├── Parses CLI via config_parse()
  ├── Initializes logger via log_init()
  ├── Creates bounded queue (1024 slots)
  ├── Starts worker pool
  ├── Runs filesystem traversal (blocks until walk finishes)
  ├── Signals queue shutdown
  ├── Waits for workers, collects merged stats
  └── Prints summary, returns exit code
```

### CLI and configuration

```
src/config.h                    src/config.c
  Config struct definition        argp parser (options[], parse_opt)
  - dry_run, delete, print        Flag validation (mutual exclusion)
  - null, max_depth, num_workers   Default values (static cfg)
  - dir, glob, delete_cmd          config_parse() → Config*
  - log_level
```

### Thread-safe bounded queue

```
src/queue.h                     src/queue.c
  Public API only (opaque type)    struct Queue (internal)
  - queue_create(capacity)         - char **buf (circular buffer)
  - queue_push(q, path)            - mutex + not_empty/not_full cv
  - queue_pop(q) → char*           - shutdown flag
  - queue_shutdown(q)              queue_push: blocks if full
  - queue_destroy(q)               queue_pop: blocks if empty, NULL on shutdown
```

### Filesystem traversal

```
src/traverse.h                  src/traverse.c
  traverse(cfg, queue) → int      walk_callback (nftw callback)
                                   - Depth limiting (ftwbuf->level)
                                   - Symlink-only filter (FTW_SL)
                                   - strdup + queue_push
                                   nftw(cfg->dir, walk_callback, 64, FTW_PHYS)
```

### Worker thread pool

```
src/worker.h                    src/worker.c
  WorkerStats struct              output_mutex (stdout protection)
  - found, deleted, errors        worker_fn (per-thread):
  worker_pool_start()               - queue_pop loop
  worker_pool_wait(out)              - symlink_matches_glob()
                                     - symlink_is_broken()
                                     - dispatch: print / dry-run / delete
                                   Per-worker stats → merged at end
```

### Symlink logic (pure, no threading)

```
src/symlink.h                   src/symlink.c
  symlink_matches_glob()          fnmatch() wrapper, short-circuits **/*
  symlink_is_broken()             stat() to check target, readlink()
  symlink_delete()                fork+exec (delete_cmd) or unlink()
```

### Logger (unchanged)

```
src/log.h / log.c
  Thread-safe stderr logger (pthread_mutex)
  LOG_FATAL / LOG_ERROR / LOG_WARN / LOG_INFO / LOG_DEBUG / LOG_TRACE / LOG_PERROR / LOG_CUSTOM
  Compile-time: LOG_SHOW_SOURCE_LOCATION, LOG_SHOW_TIME_STAMP
  log_init(level) must be called before any LOG_* macro
```

### Metadata

```
src/project_config.h
  PROJECT_VERSION, PROJECT_HOMEPAGE_URL, PROJECT_SHORT_DESC, AUTH_MESSAGE
```

## Threading details

### Queue

- Fixed capacity (1024 slots).
- `queue_push()` blocks when full (backpressure from slow workers).
- `queue_pop()` blocks when empty; returns `NULL` after `queue_shutdown()`.
- Internal: circular buffer with `pthread_mutex_t` + two `pthread_cond_t` (not_empty, not_full).

### Worker pool

- Spawned via `worker_pool_start(queue, cfg, count)`.
- Count defaults to `sysconf(_SC_NPROCESSORS_ONLN)`, capped at 128.
- Each thread owns a `WorkerStats` — no atomics needed during work.
- `worker_pool_wait(WorkerStats *out)` joins all threads and sums stats into `out`.
- For `-P` (print mode), workers acquire `output_mutex` before writing to stdout.

### Data ownership

- Paths are `strdup`'d in `traverse.c` and `free`'d in `worker.c` after processing.
- `Config` is read-only after `config_parse()` — safe to share across threads without locking.

## Adding a new CLI flag

1. Add the field to `Config` in `src/config.h`.
2. Add the `argp_option` entry and `parse_opt` case in `src/config.c`.
3. Set the default in the static `cfg` initializer in `src/config.c`.
4. Add validation in `config_parse()` if needed (e.g., mutual exclusion).
5. Use `cfg->field` in the relevant module.

## Adding a new action mode

If you want a mode alongside `-D`, `-P`, and `-C`:

1. Add a `bool` or `const char *` to `Config`.
2. Add the CLI flag in `config.c`.
3. Update the mutual exclusion check in `config_parse()`.
4. Handle the new mode in `worker_fn()` in `src/worker.c`, in the dispatch chain after the `symlink_is_broken()` check.

## Platform notes

### macOS

- Requires `argp-standalone` (`brew install argp-standalone`).
- Needs `_DARWIN_C_SOURCE` for `sysconf(_SC_NPROCESSORS_ONLN)`.
- `nftw()` and `FTW_PHYS` work as expected.

### Linux

- Defines `_GNU_SOURCE`. No extra deps.
- `nftw()` with `FTW_PHYS` is standard.

### Debug builds with clang

- `-ffreestanding` is added, which strips `<limits.h>` definitions.
- `PATH_MAX` fallback (`#define PATH_MAX 4096`) is in `src/worker.c`.
- ASan and UBSan are enabled — run `make debug` and exercise error paths.

## Code conventions

- **C standard:** C17 (`-std=c17`) with POSIX.1-2008 (`_XOPEN_SOURCE 700`).
- **Include guards:** `_HEADER_H_` style.
- **Logger:** Use `LOG_INFO`, `LOG_ERROR`, etc. Always call `log_init()` before first use.
- **No globals** except static module-level state. Config is passed as a pointer.
- **Error handling:** Functions return `int` (0 success, -1 error) or `bool` (true = condition met).

## Build commands

```sh
make              # release (-O3)
make debug        # debug (-g3, ASan, UBSan, source location, timestamps)
make clean        # remove build/ and linkrot binary
make help         # show available targets
```

## Testing

No test framework. Manual validation:

```sh
# Create test fixtures
mkdir -p /tmp/linkrot_test
ln -sf /nonexistent /tmp/linkrot_test/broken
ln -sf /tmp /tmp/linkrot_test/working

# Run all modes
linkrot /tmp/linkrot_test              # default: info output
linkrot -P /tmp/linkrot_test           # print paths only
linkrot -n -D /tmp/linkrot_test        # dry-run
linkrot -D /tmp/linkrot_test           # delete
linkrot -j 4 -D /tmp/linkrot_test      # multiple workers
linkrot -l trace /tmp/linkrot_test     # verbose debug output
```
