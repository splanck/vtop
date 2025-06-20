#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "version.h"
#include "ui.h"
#include "proc.h"

static void usage(const char *prog) {
    printf("Usage: %s [-d seconds] [-s column] [-b iter] [-n iter] [-p pid,...]\n", prog);
    printf("  -d, --delay SECS   Refresh delay in seconds (default 3)\n");
    printf("  -s, --sort  COL    Sort column: pid,cpu,mem (default pid)\n");
    printf("  -b, --batch ITER   Batch mode iterations (0=loop forever)\n");
    printf("  -n, --iterations N Number of refresh cycles (0=run forever)\n");
    printf("  -p, --pid   LIST   Comma-separated PIDs to monitor\n");
}

static int run_batch(unsigned int delay_ms, enum sort_field sort,
                     unsigned int iterations) {
    const size_t MAX_PROC = 256;
    struct process_info procs[MAX_PROC];
    struct cpu_stats cs;
    struct mem_stats ms;
    struct misc_stats misc;
    int (*compare)(const void *, const void *) = cmp_proc_pid;
    switch (sort) {
    case SORT_CPU:
        compare = cmp_proc_cpu;
        set_sort_descending(1);
        break;
    case SORT_MEM:
        compare = cmp_proc_mem;
        set_sort_descending(1);
        break;
    default:
        compare = cmp_proc_pid;
        set_sort_descending(0);
        break;
    }
    unsigned int iter = 0;
    while (iterations == 0 || iter < iterations) {
        if (read_cpu_stats(&cs) != 0)
            memset(&cs, 0, sizeof(cs));
        if (read_mem_stats(&ms) != 0)
            memset(&ms, 0, sizeof(ms));
        read_misc_stats(&misc);
        size_t count = list_processes(procs, MAX_PROC);
        qsort(procs, count, sizeof(struct process_info), compare);
        double mem_usage = 0.0;
        if (ms.total > 0)
            mem_usage = 100.0 * (double)(ms.total - ms.available) /
                        (double)ms.total;
        double swap_usage = 0.0;
        if (ms.swap_total > 0)
            swap_usage = 100.0 * (double)ms.swap_used / (double)ms.swap_total;
        printf("load %.2f %.2f %.2f  up %.0fs  tasks %d/%d  cpu %5.1f%% us %.1f%% sy %.1f%% id %.1f%%  mem %5.1f%%  swap %llu/%llu %.1f%%  intv %.1fs\n",
               misc.load1, misc.load5, misc.load15, misc.uptime,
               misc.running_tasks, misc.total_tasks,
               100.0 - cs.idle_percent, cs.user_percent, cs.system_percent,
               cs.idle_percent, mem_usage, ms.swap_used, ms.swap_total,
               swap_usage, delay_ms / 1000.0);
        printf("PID      USER     NAME                     STATE PRI  NICE  VSIZE    RSS  RSS%%  CPU%%   TIME     START\n");
        for (size_t i = 0; i < count; i++) {
            printf("%-8d %-8s %-25s %c %4ld %5ld %8llu %5ld %6.2f %6.2f %8.0f %-8s\n",
                   procs[i].pid, procs[i].user, procs[i].name, procs[i].state,
                   procs[i].priority, procs[i].nice, procs[i].vsize,
                   procs[i].rss, procs[i].rss_percent, procs[i].cpu_usage,
                   procs[i].cpu_time, procs[i].start_time);
        }
        fflush(stdout);
        usleep(delay_ms * 1000);
        iter++;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    unsigned int delay_ms = 3000; /* default 3 seconds */
    enum sort_field sort = SORT_PID;

#ifdef WITH_UI
    ui_load_config(&delay_ms, &sort);
#endif

    static struct option long_opts[] = {
        {"delay", required_argument, NULL, 'd'},
        {"sort", required_argument, NULL, 's'},
        {"batch", required_argument, NULL, 'b'},
        {"iterations", required_argument, NULL, 'n'},
        {"pid", required_argument, NULL, 'p'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt, idx;
    int batch = 0;
    unsigned int iterations = 0;
    while ((opt = getopt_long(argc, argv, "d:s:b:n:p:h", long_opts, &idx)) != -1) {
        switch (opt) {
        case 'd':
            delay_ms = (unsigned int)(strtod(optarg, NULL) * 1000);
            break;
        case 's':
            if (strcmp(optarg, "cpu") == 0)
                sort = SORT_CPU;
            else if (strcmp(optarg, "mem") == 0)
                sort = SORT_MEM;
            else
                sort = SORT_PID;
            break;
        case 'b':
            batch = 1;
            iterations = (unsigned int)strtoul(optarg, NULL, 10);
            break;
        case 'n':
            iterations = (unsigned int)strtoul(optarg, NULL, 10);
            break;
        case 'p':
            set_pid_filter(optarg);
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 0;
        }
    }

    if (batch)
        return run_batch(delay_ms, sort, iterations);

#ifdef WITH_UI
    return run_ui(delay_ms, sort, iterations);
#else
    (void)delay_ms; /* unused */
    (void)sort;     /* unused */
    printf("vtop version %s\n", VTOP_VERSION);
    return 0;
#endif
}
