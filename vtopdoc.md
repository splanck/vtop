# vtop Documentation

## Overview
vtop is a lightweight process monitor designed as a modern replacement for the classic `top` command. It reads metrics directly from `/proc` and offers an optional ncurses user interface. Because it relies only on standard C libraries (plus `ncurses` when enabled), vtop can be built on nearly any Linux system.

## Installation
Build using the provided `Makefile`:

```sh
make
```

To include the interactive UI, add the `WITH_UI` flag:

```sh
make WITH_UI=1
```

## Usage Examples
Run the command-line version which simply prints the version number:

```sh
./vtop
```

When compiled with the UI enabled, launch the interactive monitor:

```sh
make WITH_UI=1 run
```

## Future Enhancements
Planned improvements include sortable columns, richer memory statistics, and additional configuration options. Contributions and feature requests are encouraged.

## Process Information Parsing
The `proc.c` module reads information directly from the Linux `/proc` filesystem. It does not rely on external libraries so it can be compiled on any Linux system with a standard C compiler.

### CPU Statistics
`read_cpu_stats()` opens `/proc/stat` and parses the first line which contains cumulative CPU times. Fields are read using `sscanf` in the order defined by the kernel: user, nice, system, idle, iowait, irq, softirq and steal.

### Memory Statistics
`read_mem_stats()` looks for specific keys in `/proc/meminfo` such as `MemTotal`, `MemFree` and `MemAvailable`. Values are read line by line with simple string matching, allowing the code to remain portable.

### Running Processes
`list_processes()` iterates through numeric directories in `/proc`. For each process it reads the corresponding `/proc/[pid]/stat` file to extract the command name, state, virtual size and resident set size. Only a subset of the fields is parsed, keeping the parser short and robust.

These functions provide a lightweight interface for higher level monitoring tools without requiring additional dependencies.
