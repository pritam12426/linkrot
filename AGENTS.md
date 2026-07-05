# linkrot – agent guide

## Quick start

```sh
make          # build release binary (linkrot)
make debug    # build with -g3 -DDEBUG -DLOG_SHOW_SOURCE_LOCATION
make clean    # remove build/ and linkrot binary
```

## Build quirks

- **C standard:** C17 (`-std=c17`) with POSIX.1-2008 (`_XOPEN_SOURCE 700`).
- **macOS:** requires `libargp` (`brew install argp-standalone`; linked via `-largp` in `LDLIBS`).
- **On Linux:** defines `_GNU_SOURCE`; no extra lib needed.
- `compile_commands.json` exists but references a stale path; regenerate with `make -p` or Bear if LSP acts up.
- Release flags: `-O3`, no `-g`. Debug flags: `-g3 -DDEBUG -DLOG_SHOW_SOURCE_LOCATION` (and `-ffreestanding` for clang).
- Pass `O_DEBUG=1` to force a debug build: `make debug -B O_DEBUG=1`.
- Pass `log_time_stamp` as a goal to add `-DLOG_SHOW_TIME_STAMP`.

## CLI flags (from `src/main.c`)

| Flag | Long          | Description                            |
|------|---------------|----------------------------------------|
| `-l` | `--log-level` | `error|warn|info|debug` (default info) |
| `-L` | `--depth`     | Limit traversal depth (0 = start dir)  |
| `-G` | `--glob`      | Glob pattern (default `**/*`)          |
| `-n` | `--dry-run`   | Show what would be deleted             |
| `-D` | `--delete`    | Permanently delete broken symlinks     |
| `-C` | `--delete-cmd`| Pluggable delete cmd (e.g. `trash`)    |
| `-I` | `--dir`       | Directory to scan (default `.`)        |
| `-P` | `--print`     | Just print broken symlink paths        |
| `-0` | `--null`      | Null-separated output (for xargs -0)   |

## Source layout

```
src/
  main.c            -- entrypoint (parses args, kicks off nftw, prints summary)
  visitor.c / h     -- nftw callback (visit), helpers (delete, glob match), counters
  log.c / log.h     -- colorized stderr logger (level-gated, source-loc optional)
  project_config.h  -- version, homepage, author metadata
```

## Code style & tooling

- **Clang-tidy** configured (`.clang-tidy`) with `llvm-header-guard`, `readability-*`, `modernize-*`, `bugprone-*`, `misc-*`, `fuchsia-restrict-system-includes`.
- Headers use `_HEADER_H_` style include guards.
- Logger macros: `LOG_ERROR`, `LOG_WARN`, `LOG_INFO`, `LOG_DEBUG`, `LOG_PERROR`, `LOG_CUSTOM`. Source location is shown only when `LOG_SHOW_SOURCE_LOCATION` is defined (debug build).

## No tests, no CI

Single commit, no test framework, no CI pipeline. Manual validation only.

## Homepage

https://github.com/pritam12426/linkrot
