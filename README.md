# linkrot

Find and clean up broken symbolic links, recursively, on macOS and Linux.

Multi-threaded producer-consumer architecture: one thread walks the filesystem, N workers process broken symlinks in parallel.

## Install

```sh
git clone https://github.com/pritam12426/linkrot.git
cd linkrot
make
make install             # installs to /usr/local/bin
```

### Dependencies

- **macOS:** `brew install argp-standalone` (libargp, linked via `-largp`)
- **Linux:** No extra deps (uses `_GNU_SOURCE`)

## Usage

```
linkrot [option...] [DIR]
```

Pass a directory as a positional argument, or scan the current directory.

### Options

| Flag       | Description                                                |
| ---------- | ---------------------------------------------------------- |
| `-D`       | Permanently delete broken symlinks                         |
| `-C CMD`   | Custom delete command (e.g., `trash`)                      |
| `-P`       | Print broken symlink paths only                            |
| `-n`       | Preview deletions without executing (requires `-D` or `-C`)|
| `-L DEPTH` | Max traversal depth (0 = current dir only)                 |
| `-G GLOB`  | Glob pattern (default: `**/*`)                             |
| `-j NUM`   | Worker threads (default: CPU count, max 128)               |
| `-l LEVEL` | Log level: off, fatal, error, warn, info, debug, trace     |
| `-0`       | Null-separate output (for `xargs -0`)                      |

`-D`, `-P`, and `-C` are mutually exclusive.

### Examples

```sh
# List broken symlinks in ~/.local/bin
linkrot ~/.local/bin

# Recurse into src/ with a glob filter
linkrot -G "*.o" src/

# Dry-run what would be deleted
linkrot -n -D ~/Downloads

# Permanently delete broken symlinks
linkrot -D ~/Documents

# Use the trash command instead of permanent delete
linkrot -C trash ~/Desktop

# Pipe paths into xargs (handles spaces, newlines)
linkrot -P -0 ~/somedir | xargs -0 rm

# Limit scan depth to one level
linkrot -L 1 ~/projects

# Use 8 worker threads
linkrot -j 8 -D ~/Downloads
```

## Build

```sh
make help     # show available targets
make          # release build (-O3)
make debug    # debug build (-g3, ASan, UBSan)
make clean    # remove build artifacts
```

## License

[MIT](LICENSE)
