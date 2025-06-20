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

struct cpu_core_stats {
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
    unsigned long long swap_total;
    unsigned long long swap_used;
};

struct misc_stats {
    double load1;
    double load5;
    double load15;
    double uptime;
    int running_tasks;
    int total_tasks;
};

struct process_info {
    int pid;
    /* Numeric user ID of the process */
    unsigned int uid;
    /* Short username resolved from uid */
    char user[32];
    char name[256];
    char state;
    unsigned long long vsize;
    long rss;
    /* RSS as a percentage of total system memory */
    double rss_percent;
    /* User and system CPU times in clock ticks */
    unsigned long long utime;
    unsigned long long stime;
    /* Calculated CPU usage since last update (percentage) */
    double cpu_usage;
    /* Total CPU time in seconds */
    double cpu_time;
    /* Process start time as HH:MM:SS */
    char start_time[16];
};

int read_cpu_stats(struct cpu_stats *stats);
size_t get_cpu_core_count(void);
const struct cpu_core_stats *get_cpu_core_stats(void);
int read_mem_stats(struct mem_stats *stats);
size_t list_processes(struct process_info *buf, size_t max);
int read_misc_stats(struct misc_stats *stats);

/* optional filtering */
void set_name_filter(const char *substr);
void set_user_filter(const char *user);
const char *get_name_filter(void);
const char *get_user_filter(void);

/* comparison helpers for sorting */
int cmp_proc_pid(const void *a, const void *b);
int cmp_proc_cpu(const void *a, const void *b);
int cmp_proc_mem(const void *a, const void *b);

#endif /* PROC_H */
