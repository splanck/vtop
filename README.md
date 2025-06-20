# vtop
A modern top replacement for UNIX.

## Building
Compile with gcc:
```sh
gcc src/main.c -Iinclude -o vtop
```

### With ncurses UI
To build the interactive view using `ncurses`, enable the `WITH_UI` flag:
```sh
make WITH_UI=1
```
Otherwise, running `make` will build a simple command-line version
that prints the version number.

## Usage

The `-s` option selects the sort field. Available values are:

- `pid` &ndash; sort by process ID
- `cpu` &ndash; sort by CPU usage
- `mem` &ndash; sort by memory usage

When running the ncurses interface you can press `F3` or `>` to cycle to
the next sort field and `<` to go back.

