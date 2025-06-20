# vtop
A modern top replacement for UNIX.

For detailed documentation see [vtopdoc.md](vtopdoc.md).

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

