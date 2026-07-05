# linkrot

Find and clean up broken symbolic links, recursively, on macOS and Linux.

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

| Flag       | Description                                         |
| ---------- | --------------------------------------------------- |
| `-L DEPTH` | Max traversal depth (0 = current dir only)          |
| `-G GLOB`  | Glob pattern (default: `**/*`)                      |
| `-l LEVEL` | Log level: error, warn, info, debug (default: info) |
| `-n`       | Preview deletions without executing                 |
| `-D`       | Permanently delete broken symlinks                  |
| `-C CMD`   | Custom delete command (e.g., `trash`)               |
| `-P`       | Print broken symlink paths only                     |
| `-0`       | Null-separate output (for `xargs -0`)               |

### Examples

```sh
# List broken symlinks in ~/.local/bin
linkrot ~/.local/bin

# Recurse into src/ with a glob filter
linkrot -G "*.o" src/

# Dry-run what would be deleted
linkrot -n ~/Downloads

# Permanently delete broken symlinks
linkrot -D ~/Documents

# Use the trash command instead of permanent delete
linkrot -C trash ~/Desktop

# Pipe paths into xargs (handles spaces, newlines)
linkrot -P -0 ~/somedir | xargs -0 rm

# Limit scan depth to one level
linkrot -L 1 ~/projects
```

## Build

```sh
make          # release build (-O3)
make debug    # debug build (-g3 -DDEBUG -DLOG_SHOW_SOURCE_LOCATION)
make clean    # remove build artifacts
```

## License

[MIT](LICENSE)
