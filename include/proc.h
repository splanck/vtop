#ifndef PROC_H
#define PROC_H

#include <stddef.h>

struct cpu_stats {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
};

struct mem_stats {
    unsigned long long total;
    unsigned long long free;
    unsigned long long available;
    unsigned long long buffers;
    unsigned long long cached;
};

struct process_info {
    int pid;
    char name[256];
    char state;
    unsigned long long vsize;
    long rss;
};

int read_cpu_stats(struct cpu_stats *stats);
int read_mem_stats(struct mem_stats *stats);
size_t list_processes(struct process_info *buf, size_t max);

#endif /* PROC_H */
