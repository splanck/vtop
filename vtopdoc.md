# vtop Process Information Parsing

The `proc.c` module reads information directly from the Linux `/proc`
filesystem. It does not rely on external libraries so it can be
compiled on any Linux system with a standard C compiler.

## CPU Statistics
`read_cpu_stats()` opens `/proc/stat` and parses the first line which
contains cumulative CPU times. Fields are read using `sscanf` in the
order defined by the kernel: user, nice, system, idle, iowait, irq,
softirq and steal.

## Memory Statistics
`read_mem_stats()` looks for specific keys in `/proc/meminfo` such as
`MemTotal`, `MemFree` and `MemAvailable`. Values are read line by line
with simple string matching, allowing the code to remain portable.

## Running Processes
`list_processes()` iterates through numeric directories in `/proc`.
For each process it reads the corresponding `/proc/[pid]/stat` file to
extract the command name, state, virtual size and resident set size.
Only a subset of the fields is parsed, keeping the parser short and
robust.

These functions provide a lightweight interface for higher level
monitoring tools without requiring additional dependencies.
