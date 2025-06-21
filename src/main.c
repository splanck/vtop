#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include "version.h"
#include "ui.h"
#include "proc.h"
#include "control.h"

/* maximum number of process entries to display (0 = unlimited) */
static size_t max_entries;

static enum mem_unit parse_unit(const char *arg) {
    if (!arg || !*arg)
        return MEM_UNIT_K;
    char c = tolower((unsigned char)arg[0]);
    switch (c) {
    case 'm': return MEM_UNIT_M;
    case 'g': return MEM_UNIT_G;
    case 't': return MEM_UNIT_T;
    case 'p': return MEM_UNIT_P;
    case 'e': return MEM_UNIT_E;
    case 'k':
    default:  return MEM_UNIT_K;
    }
}

static void usage(const char *prog) {
    printf("Usage: %s [-d seconds] [-S] [-a] [-i] [--accum] [-s column] [-E unit] [-e unit] [-b iter] [-n iter] [-m max] [-p pid,...] [-C string] [-u user] [-U user] [-w cols]\n", prog);
    printf("  -d, --delay SECS   Refresh delay in seconds (default 3)\n");
    printf("  -S, --secure       Disable signaling and renicing tasks\n");
    printf("  -s, --sort  COL    Sort column: pid,cpu,mem,user,start,time,pri (default pid)\n");
    printf("  -E, --scale-summary-mem UNIT  Memory units for summary (k,m,g,t,p,e)\n");
    printf("  -e, --scale-task-mem UNIT     Memory units for processes (k,m,g,t,p,e)\n");
    printf("  -b, --batch ITER   Batch mode iterations (0=loop forever)\n");
    printf("  -n, --iterations N Number of refresh cycles (0=run forever)\n");
    printf("  -p, --pid   LIST   Comma-separated PIDs to monitor\n");
    printf("  -C, --command-filter STR  Show only tasks whose command contains STR\n");
    printf("  -u USER            Show only tasks owned by USER\n");
    printf("  -U USER            Same as -u\n");
    printf("  -m, --max   N     Maximum number of processes to display (0=all)\n");
    printf("  -w, --width COLS  Override screen width in columns\n");
    printf("  -a, --cmdline     Display the full command line by default\n");
    printf("  -i, --hide-idle   Hide processes with zero CPU usage\n");
    printf("  -H, --threads     Show individual threads instead of processes\n");
    printf("      --irix        Do not scale CPU%% by number of CPUs\n");
    printf("      --per-cpu     Show per-core CPU usage\n");
    printf("      --accum       Include child CPU time in TIME column\n");
#ifdef WITH_UI
    printf("      --list-fields  Print column names and exit\n");
#endif
    printf("  -V, --version     Print vtop version and exit\n");
}

static int run_batch(unsigned int delay_ms, enum sort_field sort,
                     unsigned int iterations) {
    struct process_info *procs = NULL;
    size_t proc_cap = 0;
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
    case SORT_USER:
        compare = cmp_proc_user;
        set_sort_descending(0);
        break;
    case SORT_START:
        compare = cmp_proc_start;
        set_sort_descending(0);
        break;
    case SORT_TIME:
        compare = cmp_proc_time;
        set_sort_descending(1);
        break;
    case SORT_PRI:
        compare = cmp_proc_priority;
        set_sort_descending(0);
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
        size_t need = count_processes();
        if (max_entries && need > max_entries)
            need = max_entries;
        if (need > proc_cap) {
            struct process_info *tmp = realloc(procs, need * sizeof(*procs));
            if (tmp) {
                procs = tmp;
                proc_cap = need;
            }
        }
        size_t count = list_processes(procs, proc_cap);
        if (max_entries && count > max_entries)
            count = max_entries;
        qsort(procs, count, sizeof(struct process_info), compare);
        double mem_usage = 0.0;
        if (ms.total > 0)
            mem_usage = 100.0 * (double)(ms.total - ms.available) /
                        (double)ms.total;
        double swap_usage = 0.0;
        if (ms.swap_total > 0)
            swap_usage = 100.0 * (double)ms.swap_used / (double)ms.swap_total;
        double swap_used = scale_kb(ms.swap_used, summary_unit);
        double swap_total = scale_kb(ms.swap_total, summary_unit);
        printf("load %.2f %.2f %.2f  up %.0fs  tasks %d total, %d running, %d sleeping, %d stopped, %d zombie  cpu %5.1f%% us %.1f%% sy %.1f%% id %.1f%%  mem %5.1f%%  swap %.0f/%.0f%s %.1f%%  intv %.1fs\n",
               misc.load1, misc.load5, misc.load15, misc.uptime,
               misc.total_tasks, misc.running_tasks, misc.sleeping_tasks,
               misc.stopped_tasks, misc.zombie_tasks,
               100.0 - cs.idle_percent, cs.user_percent, cs.system_percent,
               cs.idle_percent, mem_usage, swap_used, swap_total,
               mem_unit_suffix(summary_unit), swap_usage, delay_ms / 1000.0);
        printf("PID      CPU  USER     NAME                     STATE PRI  NICE  VSIZE    RSS   SHR  RSS%%  CPU%%   TIME     START\n");
        for (size_t i = 0; i < count; i++) {
            double vsz = procs[i].vsize / 1024.0; /* bytes to KB */
            vsz = scale_kb((unsigned long long)vsz, proc_unit);
            double rss = scale_kb((unsigned long long)procs[i].rss, proc_unit);
            double shr = scale_kb(procs[i].shared, proc_unit);
            printf("%-8d %3d %-8s %-25s %c %4ld %5ld %8.1f %5.1f %5.1f %6.2f %6.2f %8.0f %-8s\n",
                   procs[i].pid, procs[i].cpu, procs[i].user, procs[i].name, procs[i].state,
                   procs[i].priority, procs[i].nice, vsz, rss, shr,
                   procs[i].rss_percent, procs[i].cpu_usage,
                   procs[i].cpu_time, procs[i].start_time);
        }
        fflush(stdout);
        usleep(delay_ms * 1000);
        iter++;
    }
    free(procs);
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
        {"secure", no_argument, NULL, 'S'},
        {"sort", required_argument, NULL, 's'},
        {"scale-summary-mem", required_argument, NULL, 'E'},
        {"scale-task-mem", required_argument, NULL, 'e'},
        {"batch", required_argument, NULL, 'b'},
        {"iterations", required_argument, NULL, 'n'},
        {"max", required_argument, NULL, 'm'},
        {"pid", required_argument, NULL, 'p'},
        {"command-filter", required_argument, NULL, 'C'},
        {"user", required_argument, NULL, 'u'},
        {"euser", required_argument, NULL, 'U'},
        {"width", required_argument, NULL, 'w'},
        {"cmdline", no_argument, NULL, 'a'},
        {"hide-idle", no_argument, NULL, 'i'},
        {"threads", no_argument, NULL, 'H'},
#ifdef WITH_UI
        {"list-fields", no_argument, NULL, 2},
#endif
        {"per-cpu", no_argument, NULL, '1'},
        {"accum", no_argument, NULL, 1},
        {"irix", no_argument, NULL, 3},
        {"version", no_argument, NULL, 'V'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt, idx;
    int batch = 0;
    unsigned int iterations = 0;
    int columns = 0;
    while ((opt = getopt_long(argc, argv, "d:Ss:E:e:b:n:m:p:C:u:U:w:aiHVh", long_opts, &idx)) != -1) {
        switch (opt) {
        case 'd':
            delay_ms = (unsigned int)(strtod(optarg, NULL) * 1000);
            break;
        case 'S':
            secure_mode = 1;
            break;
        case 1:
            set_show_accum_time(1);
            break;
        case 2:
#ifdef WITH_UI
            ui_list_fields();
#endif
            return 0;
        case 3:
            set_cpu_irix_mode(1);
            break;
        case '1':
#ifdef WITH_UI
            ui_set_show_cores(1);
#endif
            break;
        case 's':
            if (strcmp(optarg, "cpu") == 0)
                sort = SORT_CPU;
            else if (strcmp(optarg, "mem") == 0)
                sort = SORT_MEM;
            else if (strcmp(optarg, "user") == 0)
                sort = SORT_USER;
            else if (strcmp(optarg, "start") == 0)
                sort = SORT_START;
            else if (strcmp(optarg, "time") == 0)
                sort = SORT_TIME;
            else if (strcmp(optarg, "pri") == 0 ||
                     strcmp(optarg, "priority") == 0)
                sort = SORT_PRI;
            else
                sort = SORT_PID;
            break;
        case 'E':
            summary_unit = parse_unit(optarg);
            break;
        case 'e':
            proc_unit = parse_unit(optarg);
            break;
        case 'b':
            batch = 1;
            iterations = (unsigned int)strtoul(optarg, NULL, 10);
            break;
        case 'n':
            iterations = (unsigned int)strtoul(optarg, NULL, 10);
            break;
        case 'm':
            max_entries = (size_t)strtoul(optarg, NULL, 10);
            break;
        case 'p':
            set_pid_filter(optarg);
            break;
        case 'C':
            set_name_filter(optarg);
            break;
        case 'u':
        case 'U':
            set_user_filter(optarg);
            break;
        case 'w':
            columns = atoi(optarg);
            if (columns < 0)
                columns = 0;
            break;
        case 'a':
#ifdef WITH_UI
            ui_set_show_full_cmd(1);
#endif
            break;
        case 'i':
#ifdef WITH_UI
            ui_set_show_idle(0);
#endif
            break;
        case 'H':
            set_thread_mode(1);
            break;
        case 'V':
            printf("vtop version %s\n", VTOP_VERSION);
            return 0;
        case 'h':
        default:
            usage(argv[0]);
            return 0;
        }
    }

    if (batch)
        return run_batch(delay_ms, sort, iterations);

#ifdef WITH_UI
    return run_ui(delay_ms, sort, iterations, columns, max_entries);
#else
    (void)delay_ms; /* unused */
    (void)sort;     /* unused */
    (void)columns;  /* unused */
    printf("vtop version %s\n", VTOP_VERSION);
    return 0;
#endif
}
