# linkrot – agent guide

## Quick start

```sh
make          # build release binary (linkrot)
make debug    # build with -g3 -DDEBUG -DLOG_SHOW_SOURCE_LOCATION + ASan/UBSan
make clean    # remove build/ and linkrot binary
```

## Build quirks

- **C standard:** C17 (`-std=c17`) with POSIX.1-2008 (`_XOPEN_SOURCE 700`).
- **macOS:** requires `libargp` (`brew install argp-standalone`; linked via `-largp` in `LDLIBS`). Also needs `_DARWIN_C_SOURCE` for `sysconf(_SC_NPROCESSORS_ONLN)`.
- **On Linux:** defines `_GNU_SOURCE`; no extra lib needed.
- `compile_commands.json` is gitignored; regenerate with Bear if LSP needs it.
- Release: `-O3`, no `-g`. Debug: `-g3 -DDEBUG -DLOG_SHOW_SOURCE_LOCATION`, plus `-fsanitize=address -fsanitize=undefined` and `-fstack-usage`. Clang debug builds also add `-ffreestanding`.
- `make debug` auto-enables `O_DEBUG=1`, `O_LOG_SHOW_SOURCE_LOCATION=1`, `O_LOG_SHOW_TIME_STAMP=1`.
- `PATH_MAX` fallback define in `worker.c` — needed because `-ffreestanding` strips `<limits.h>` definitions.

## CLI flags (from `src/config.c`)

| Flag | Long          | Description                                              |
|------|---------------|----------------------------------------------------------|
| `-l` | `--log-level` | `off|fatal|error|warn|info|debug|trace` (default: info)  |
| `-L` | `--depth`     | Limit traversal depth (0 = start dir)                    |
| `-G` | `--glob`      | Glob pattern (default `**/*`)                            |
| `-j` | `--workers`   | Number of worker threads (default: CPU count, max 128)   |
| `-n` | `--dry-run`   | Show what would be deleted (requires `-D` or `-C`)       |
| `-D` | `--delete`    | Permanently delete broken symlinks                       |
| `-C` | `--delete-cmd`| Pluggable delete cmd (e.g. `trash`)                      |
| `-P` | `--print`     | Just print broken symlink paths                          |
| `-0` | `--null`      | Null-separated output (for xargs -0)                     |

Positional arg: `[DIR]` (default `.`).

`-D`, `-P`, and `-C` are mutually exclusive. `-n` requires `-D` or `-C`.

## Architecture

Producer-consumer with bounded queue:

```
Traversal (1 thread)  →  Queue (1024 slots)  →  Workers (N threads)
  nftw() walk               circular buffer        glob match
  enqueue paths             mutex + cv              stat check
                                                 delete / print
```

- **Traversal thread**: runs `nftw()`, pushes symlink paths into the queue. Minimal work.
- **Worker threads**: each pops a path, does glob match → stat → readlink → action.
- **Per-worker stats**: each thread has its own `WorkerStats` (found/deleted/errors), merged at the end. No atomics needed during work.
- **Shutdown**: `queue_shutdown()` broadcasts cv's, workers drain and exit, `worker_pool_wait()` joins all.
- **Output**: workers hold `output_mutex` for stdout writes (print mode). LOG_* calls are already mutex-protected internally.

## Source layout

```
src/
  main.c            -- orchestration: parse → queue → workers → traverse → summary
  config.c / h      -- Config struct, argp parsing, flag validation
  queue.c / h       -- thread-safe bounded queue (circular buffer + mutex + cv)
  traverse.c / h    -- nftw-based walk, pushes symlink paths into queue
  worker.c / h      -- thread pool, per-worker stats, output mutex
  symlink.c / h     -- pure logic: glob match, broken detection, delete
  log.c / h         -- thread-safe colorized logger (unchanged)
  project_config.h  -- version, homepage, author metadata
```

## Code style & tooling

- **Clang-tidy** configured (`.clang-tidy`): `readability-*`, `modernize-*`, `bugprone-*`, `misc-*`, `fuchsia-restrict-system-includes`, `llvm-header-guard`. No CI enforces it.
- Headers use `_HEADER_H_` style include guards.
- Logger macros: `LOG_FATAL`, `LOG_ERROR`, `LOG_WARN`, `LOG_INFO`, `LOG_DEBUG`, `LOG_TRACE`, `LOG_PERROR`, `LOG_CUSTOM`. Must call `log_init()` before any `LOG_*` call (initialized in `main`).

## No tests, no CI

No test framework, no CI pipeline. Manual validation only.

## Homepage

https://github.com/pritam12426/linkrot
