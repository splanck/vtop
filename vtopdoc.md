# vtop Process Information Parsing

The `proc.c` module reads information directly from the Linux `/proc`
filesystem. It does not rely on external libraries so it can be
compiled on any Linux system with a standard C compiler.

## CPU Statistics
`read_cpu_stats()` opens `/proc/stat` and parses both the aggregate
`cpu` line and any `cpu0`, `cpu1`, ... entries. Each line provides
cumulative times for user, nice, system, idle, iowait, irq, softirq and
steal cycles. The per-core values are stored in an array that can be
queried by the UI.

The **user** field represents time running processes in user space
(including "nice" time). **System** accounts for time spent executing
kernel code, servicing interrupts and the "steal" time taken by a
hypervisor. **Idle** covers idle loops and I/O wait time when the CPU is
not executing tasks.

`read_cpu_stats()` also calculates the percentage of time spent in each
of these states since the previous call. These percentages are displayed
in the UI header next to the overall CPU usage.

## Memory Statistics
`read_mem_stats()` looks for specific keys in `/proc/meminfo` such as
`MemTotal`, `MemFree` and `MemAvailable`. Values are read line by line
with simple string matching, allowing the code to remain portable.

## Miscellaneous Statistics
`read_misc_stats()` parses `/proc/loadavg` and `/proc/uptime` to obtain
load averages, total running tasks and system uptime. The function fills
a `struct misc_stats` with three load values, the uptime in seconds and
the number of running versus total tasks.

## Running Processes
`list_processes()` iterates through numeric directories in `/proc`.
For each process it reads `/proc/[pid]/stat` for basic metrics and
`/proc/[pid]/cmdline` to obtain the full argument list. The command line
is stored as a space separated string along with the short command name,
state, virtual size and resident set size. Only a subset of the fields
is parsed, keeping the parser short and robust. The real user ID is read
from `/proc/[pid]/status` and resolved to a username via `getpwuid()` so
the UI can display the process owner.

`list_processes()` also reports the resident set size as a percentage of
total system memory. The value is computed with

```
rss_percent = (rss * page_size / 1024) / MemTotal * 100
```

where `rss` comes from `/proc/[pid]/stat`, `page_size` is obtained from
`getpagesize()` and `MemTotal` is read by `read_mem_stats()`.

These functions provide a lightweight interface for higher level
monitoring tools without requiring additional dependencies.

## Command-line Options

`vtop` accepts a few options similar to classic `top`.

- `-d SECS` &mdash; Set the refresh delay in seconds. The default is
  `3` seconds just like `top`.
- `-s COL` &mdash; Choose the column to sort by. Supported values are
  `pid`, `cpu` and `mem`. The default is `pid`.

Examples:

```sh
vtop              # run with defaults (3s delay, sort by pid)
vtop -d 1         # update every second
vtop -s cpu       # sort processes by CPU usage
```

The interactive display lists PID, USER, command name, state,
priority, nice value, virtual memory size, resident set size,
memory usage percentage, CPU usage, total CPU time and the process
start time.
In the interactive interface press `F3` or `>` to cycle to the next sort
field and `<` to go back. Use `F4` or `o` to toggle the sort order.

Process management shortcuts are also available:

- `k` &ndash; prompt for a PID and send `SIGTERM` to that process.
- `r` &ndash; prompt for a PID and new nice value to adjust process priority.
- `c` &ndash; toggle per-core CPU usage display.
- `a` &ndash; toggle between the short name and full command line.
- `F4`/`o` &ndash; change the sort direction.
- `h` &ndash; display a help window showing available shortcuts.

Use caution when running with elevated privileges because killing or
renicing critical processes can destabilize the system.
