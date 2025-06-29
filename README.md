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
- `vsize` &ndash; sort by virtual memory size
- `user` &ndash; sort by username
- `start` &ndash; sort by start time

The `-b`/`--batch` option runs without the ncurses interface and prints
plain text updates. Use `-n N` to limit the number of refresh cycles;
`0` runs indefinitely. The `-p` option restricts the output to the
comma-separated list of PIDs given. The `-C` option filters processes by a
substring of the command name. The `-E` and `-e` options control the
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
The `--accum` option displays CPU time including dead children.
The `--list-fields` option prints all available column names and exits.
The `-a`/`--cmdline` flag shows the full command line instead of the short
command name.
Use `-i`/`--hide-idle` to start with idle processes hidden.
Use `--hide-kthreads` to hide kernel threads whose names begin with `[`.
Use `-H`/`--threads` to show individual threads instead of processes.
Use `--irix` to display CPU usage relative to a single CPU.
Use `--per-cpu` to show per-core CPU usage by default.
Use `-V`/`--version` to print the vtop version and exit.

Use `-u USER` or `-U USER` to show only processes owned by `USER`.
Use `-C STR` or `--command-filter STR` to display only tasks whose
command contains the given substring.
Use `--state=R` to display only tasks in state `R` (running). Use an empty
argument to clear the filter.

```sh
vtop -u alice
```

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
- Press `K` to hide or show kernel threads.
- Press `i` to hide or show processes with zero CPU usage.
- Press `g` to filter by process state (e.g., `R` for running).
- Press `Z` to cycle through color schemes.
- Press `x` to toggle highlighting of the sorted column.
- Press `b` to toggle bold text in the process list.
- Press `S` to toggle cumulative CPU time.
- Press `I` to toggle Irix mode (no CPU scaling).
- Press `E` to cycle through memory units used for display.
- Press `F4` or `o` to change the sort direction.
- Press `U` to sort by user.
- Press `B` to sort by start time.
- Press `C` to sort by CPU usage.
- Press `M` to sort by memory usage.
- Press `space` to pause or resume updates.
- Press `h` to open a small help window with available shortcuts.
- Press `W` to save the current configuration.
- Press `f` to open the field manager. Use `space` to toggle visibility,
including the CPU, SHR, READ and WRITE columns, and `h`/`l` to move the selected column left or right.

These controls operate on live processes. Ensure you have permission to
signal or renice the target process. Running as root can terminate or slow
critical system tasks, so use with care.

## Configuration

When the ncurses interface exits it writes the current options to
`~/.vtoprc`. This file stores the refresh interval, sort column, the
enabled columns and their order, the chosen memory units and the active color scheme. On the
next launch, vtop reads this file to restore the
previous settings. You can also press `W` at any time to save the
configuration manually.

Example output with the ncurses interface:

The table shows PID, CPU, USER, process name, state, priority,
nice value, virtual memory size, resident set size, shared
memory size, memory usage percentage, CPU usage, total CPU time,
bytes read from and written to storage and the process start time.
The header above the table displays the system load averages,
uptime and a full task state summary in the form
`tasks <total> total, <running> running, <sleeping> sleeping,
<stopped> stopped, <zombie> zombie`.

```text
$ vtop
load 0.00 0.01 0.05  up 1234s  tasks 87 total, 1 running, 86 sleeping, 0 stopped, 0 zombie  cpu 2.0%  mem 27.3%
PID      NAME                     STATE  VSIZE    RSS   SHR  RSS%  CPU%   TIME     START
...
```


## License

This project is licensed under the BSD 2-Clause "Simplified" License. See the [LICENSE](LICENSE) file for details.
