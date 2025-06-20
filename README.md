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

Additional shortcuts:

- Press `k` to send `SIGTERM` to a process. vtop will prompt for the PID.
- Press `r` to change a process's nice value. You will be asked for the
  PID and the new nice level.

These controls operate on live processes. Ensure you have permission to
signal or renice the target process. Running as root can terminate or slow
critical system tasks, so use with care.

Example output with the ncurses interface:

```text
$ vtop
load 0.00 0.01 0.05  up 1234s  tasks 1/87  cpu 2.0%  mem 27.3%
PID      NAME                     STATE  VSIZE    RSS  RSS%  CPU%
...
```

