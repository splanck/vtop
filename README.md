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

The `-d`/`--delay` option sets how often the display refreshes. The
interval is specified in seconds. The default is `3` seconds and the
valid range is `0.1`&ndash;`10` seconds. During interactive use you can
press `+` or `-` to adjust the delay in 0.1&nbsp;s steps.

The `-s` option selects the sort field. Available values are:

- `pid` &ndash; sort by process ID
- `cpu` &ndash; sort by CPU usage
- `mem` &ndash; sort by memory usage

The `-b`/`--batch` option runs without the ncurses interface and prints
plain text updates. Use `-n N` to limit the number of refresh cycles;
`0` runs indefinitely. The `-p` option restricts the output to the
comma-separated list of PIDs given. The `-E` and `-e` options control the
units used when displaying memory. Both accept one of `k`, `m`, `g`, `t`,
`p` or `e` for kilobytes through exabytes. `-E` affects the summary line
while `-e` scales per-process values.
Use `-m N` to limit the number of displayed tasks.
The `-w` option allows overriding the screen width used by the ncurses
layout. When the specified width is smaller than the default, columns are
truncated to fit.
The `-S`/`--secure` flag disables sending signals and changing
process priorities. Use this when running vtop in restricted
environments.

When running the ncurses interface you can press `F3` or `>` to cycle to
the next sort field and `<` to go back.
Press `F4` or `o` to toggle between ascending and descending order.

Additional shortcuts:

- Press `k` to send a custom signal to a process. vtop will prompt for the PID and signal number.
- Press `r` to change a process's nice value. You will be asked for the
  PID and the new nice level.
- Use `+` and `-` to increase or decrease the refresh delay while running.
- Press `c` to toggle per-core CPU usage display.
- Press `a` to toggle between the short command name and the full command line.
- Press `H` to toggle thread view (show individual threads).
- Press `i` to hide or show processes with zero CPU usage.
- Press `z` to toggle color output.
- Press `E` to cycle through memory units used for display.
- Press `F4` or `o` to change the sort direction.
- Press `space` to pause or resume updates.
- Press `h` to open a small help window with available shortcuts.
- Press `W` to save the current configuration.

These controls operate on live processes. Ensure you have permission to
signal or renice the target process. Running as root can terminate or slow
critical system tasks, so use with care.

## Configuration

When the ncurses interface exits it writes the current options to
`~/.vtoprc`. This file stores the refresh interval, sort column and the
enabled columns as well as the chosen memory units. On the next launch,
vtop reads this file to restore the
previous settings. You can also press `W` at any time to save the
configuration manually.

Example output with the ncurses interface:

The table shows PID, USER, process name, state, priority,
nice value, virtual memory size, resident set size, memory
usage percentage, CPU usage, total CPU time and the process
start time.

```text
$ vtop
load 0.00 0.01 0.05  up 1234s  tasks 1/87  cpu 2.0%  mem 27.3%
PID      NAME                     STATE  VSIZE    RSS  RSS%  CPU%   TIME     START
...
```

