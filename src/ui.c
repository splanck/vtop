#include "proc.h"
#include "ui.h"
#include "control.h"
#ifdef WITH_UI
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define MAX_PROC 256
#define MIN_DELAY_MS 100
#define MAX_DELAY_MS 10000

static unsigned long long prev_total;
static unsigned long long prev_idle;

static double *core_usage;
static unsigned long long *core_prev_total;
static unsigned long long *core_prev_idle;
static size_t core_count;
static int show_cores;
static int show_full_cmd;
static int show_threads;

static enum sort_field current_sort;
static int (*compare_procs)(const void *, const void *) = cmp_proc_pid;

static void show_help(void) {
    const int h = 18;
    const int w = 52;
    WINDOW *win = newwin(h, w, (LINES - h) / 2, (COLS - w) / 2);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "Key bindings:");
    mvwprintw(win, 3, 2, "q  Quit");
    mvwprintw(win, 4, 2, "F3/>/<  Change sort field");
    mvwprintw(win, 5, 2, "F4/o    Toggle sort order");
    mvwprintw(win, 6, 2, "+/-     Adjust refresh delay");
    mvwprintw(win, 7, 2, "/       Filter by command name");
    mvwprintw(win, 8, 2, "u       Filter by user");
    mvwprintw(win, 9, 2, "k       Kill a process");
    mvwprintw(win, 10, 2, "r       Renice a process");
    mvwprintw(win, 11, 2, "c       Toggle per-core view");
    mvwprintw(win, 12, 2, "a       Toggle full command");
    mvwprintw(win, 13, 2, "H       Toggle thread view");
    mvwprintw(win, 14, 2, "SPACE    Pause/resume");
    mvwprintw(win, 15, 2, "h       Show this help");
    mvwprintw(win, h - 2, 2, "Press any key to return");
    wrefresh(win);
    nodelay(stdscr, FALSE);
    wgetch(win);
    nodelay(stdscr, TRUE);
    delwin(win);
}

static void set_sort(enum sort_field sort) {
    current_sort = sort;
    switch (sort) {
    case SORT_PID:
        compare_procs = cmp_proc_pid;
        set_sort_descending(0);
        break;
    case SORT_CPU:
        compare_procs = cmp_proc_cpu;
        set_sort_descending(1);
        break;
    case SORT_MEM:
        compare_procs = cmp_proc_mem;
        set_sort_descending(1);
        break;
    }
}

int run_ui(unsigned int delay_ms, enum sort_field sort,
           unsigned int iterations) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    struct process_info procs[MAX_PROC];
    struct cpu_stats cs;
    struct mem_stats ms;
    struct misc_stats misc;
    double cpu_usage = 0.0;
    double mem_usage = 0.0;
    double swap_usage = 0.0;
    size_t count = 0;
    int paused = 0;
    unsigned int iter = 0;

    set_sort(sort);
    show_threads = get_thread_mode();
    set_thread_mode(show_threads);
    unsigned int interval = delay_ms;
    if (interval < MIN_DELAY_MS)
        interval = MIN_DELAY_MS;
    if (interval > MAX_DELAY_MS)
        interval = MAX_DELAY_MS;
    int ch = 0;
    while (ch != 'q' && (iterations == 0 || iter < iterations)) {
        if (!paused && read_cpu_stats(&cs) == 0) {
            unsigned long long idle = cs.idle + cs.iowait;
            unsigned long long total = cs.user + cs.nice + cs.system +
                                       cs.irq + cs.softirq + cs.steal + idle;
            unsigned long long d_total = total - prev_total;
            unsigned long long d_idle = idle - prev_idle;
            if (d_total > 0)
                cpu_usage = 100.0 * (double)(d_total - d_idle) / (double)d_total;
            prev_total = total;
            prev_idle = idle;

            size_t n = get_cpu_core_count();
            const struct cpu_core_stats *cores = get_cpu_core_stats();
            if (n != core_count) {
                free(core_usage);
                free(core_prev_total);
                free(core_prev_idle);
                core_usage = calloc(n, sizeof(double));
                core_prev_total = calloc(n, sizeof(unsigned long long));
                core_prev_idle = calloc(n, sizeof(unsigned long long));
                core_count = n;
            }
            for (size_t i = 0; i < n; i++) {
                unsigned long long cidle = cores[i].idle + cores[i].iowait;
                unsigned long long ctotal = cores[i].user + cores[i].nice +
                                            cores[i].system + cores[i].irq +
                                            cores[i].softirq + cores[i].steal +
                                            cidle;
                unsigned long long cd_total = ctotal - core_prev_total[i];
                unsigned long long cd_idle = cidle - core_prev_idle[i];
                double usage = 0.0;
                if (cd_total > 0)
                    usage = 100.0 * (double)(cd_total - cd_idle) /
                            (double)cd_total;
                core_usage[i] = usage;
                core_prev_total[i] = ctotal;
                core_prev_idle[i] = cidle;
            }
        }
        if (!paused && read_mem_stats(&ms) == 0 && ms.total > 0) {
            unsigned long long used = ms.total - ms.available;
            mem_usage = 100.0 * (double)used / (double)ms.total;
            if (ms.swap_total > 0)
                swap_usage = 100.0 * (double)ms.swap_used /
                             (double)ms.swap_total;
            else
                swap_usage = 0.0;
        }
        if (!paused)
            read_misc_stats(&misc);

        if (!paused)
            count = list_processes(procs, MAX_PROC);
        qsort(procs, count, sizeof(struct process_info), compare_procs);
        erase();
        char fbuf[128] = "";
        const char *nf = get_name_filter();
        const char *uf = get_user_filter();
        if (nf[0]) {
            strncat(fbuf, " cmd=", sizeof(fbuf) - strlen(fbuf) - 1);
            strncat(fbuf, nf, sizeof(fbuf) - strlen(fbuf) - 1);
        }
        if (uf[0]) {
            strncat(fbuf, " user=", sizeof(fbuf) - strlen(fbuf) - 1);
            strncat(fbuf, uf, sizeof(fbuf) - strlen(fbuf) - 1);
        }
        mvprintw(0, 0,
                 "load %.2f %.2f %.2f  up %.0fs  tasks %d/%d  cpu %5.1f%% us %.1f%% sy %.1f%% id %.1f%%  mem %5.1f%%  swap %llu/%llu %.1f%%  intv %.1fs%s%s",
                 misc.load1, misc.load5, misc.load15, misc.uptime,
                 misc.running_tasks, misc.total_tasks, cpu_usage,
                 cs.user_percent, cs.system_percent, cs.idle_percent,
                 mem_usage, ms.swap_used, ms.swap_total, swap_usage,
                 interval / 1000.0, paused ? " [PAUSED]" : "", fbuf);
        int row = 1;
        if (show_cores && core_count > 0) {
            char cbuf[256] = "";
            for (size_t i = 0; i < core_count; i++) {
                char seg[32];
                snprintf(seg, sizeof(seg), "cpu%zu %5.1f%% ", i, core_usage[i]);
                if (strlen(cbuf) + strlen(seg) < sizeof(cbuf))
                    strcat(cbuf, seg);
                else
                    break;
            }
            mvprintw(1, 0, "%s", cbuf);
            row = 2;
        }
        mvprintw(row, 0, "%s",
                 show_full_cmd ?
                     (show_threads ?
                          "PID      TID      USER     COMMAND                  STATE PRI  NICE  VSIZE    RSS  RSS%  CPU%   TIME     START" :
                          "PID      USER     COMMAND                  STATE PRI  NICE  VSIZE    RSS  RSS%  CPU%   TIME     START") :
                     (show_threads ?
                          "PID      TID      USER     NAME                     STATE PRI  NICE  VSIZE    RSS  RSS%  CPU%   TIME     START" :
                          "PID      USER     NAME                     STATE PRI  NICE  VSIZE    RSS  RSS%  CPU%   TIME     START"));
        for (size_t i = 0; i < count && i < LINES - row - 2; i++) {
            const char *disp = show_full_cmd && procs[i].cmdline[0] ?
                                procs[i].cmdline : procs[i].name;
            if (show_threads)
                mvprintw(i + row + 1, 0,
                         "%-8d %-8d %-8s %-25s %c %4ld %5ld %8llu %5ld %6.2f %6.2f %8.0f %-8s",
                         procs[i].pid, procs[i].tid, procs[i].user, disp, procs[i].state,
                         procs[i].priority, procs[i].nice,
                         procs[i].vsize, procs[i].rss,
                         procs[i].rss_percent, procs[i].cpu_usage,
                         procs[i].cpu_time, procs[i].start_time);
            else
                mvprintw(i + row + 1, 0,
                         "%-8d %-8s %-25s %c %4ld %5ld %8llu %5ld %6.2f %6.2f %8.0f %-8s",
                         procs[i].pid, procs[i].user, disp, procs[i].state,
                         procs[i].priority, procs[i].nice,
                         procs[i].vsize, procs[i].rss,
                         procs[i].rss_percent, procs[i].cpu_usage,
                         procs[i].cpu_time, procs[i].start_time);
        }
        refresh();
        usleep(interval * 1000);
        ch = getch();
        iter++;
        if (ch == KEY_F(3) || ch == '>') {
            if (current_sort == SORT_MEM)
                set_sort(SORT_PID);
            else
                set_sort(current_sort + 1);
        } else if (ch == '<') {
            if (current_sort == SORT_PID)
                set_sort(SORT_MEM);
            else
                set_sort(current_sort - 1);
        } else if (ch == '+') {
            if (interval + 100 <= MAX_DELAY_MS)
                interval += 100;
        } else if (ch == '-') {
            if (interval > MIN_DELAY_MS)
                interval -= 100;
        } else if (ch == '/') {
            char buf[64];
            nodelay(stdscr, FALSE);
            echo();
            curs_set(1);
            mvprintw(LINES - 1, 0, "Command filter: ");
            getnstr(buf, sizeof(buf) - 1);
            set_name_filter(buf[0] ? buf : NULL);
            noecho();
            curs_set(0);
            nodelay(stdscr, TRUE);
        } else if (ch == 'u') {
            char buf[32];
            nodelay(stdscr, FALSE);
            echo();
            curs_set(1);
            mvprintw(LINES - 1, 0, "User filter: ");
            getnstr(buf, sizeof(buf) - 1);
            set_user_filter(buf[0] ? buf : NULL);
            noecho();
            curs_set(0);
            nodelay(stdscr, TRUE);
        } else if (ch == 'k') {
            char buf[16];
            nodelay(stdscr, FALSE);
            echo();
            curs_set(1);
            mvprintw(LINES - 1, 0, "PID to kill: ");
            getnstr(buf, sizeof(buf) - 1);
            int pid = atoi(buf);
            send_signal(pid, SIGTERM);
            noecho();
            curs_set(0);
            nodelay(stdscr, TRUE);
        } else if (ch == 'r') {
            char buf1[16];
            char buf2[16];
            nodelay(stdscr, FALSE);
            echo();
            curs_set(1);
            mvprintw(LINES - 1, 0, "PID to renice: ");
            getnstr(buf1, sizeof(buf1) - 1);
            mvprintw(LINES - 1, 0, "New nice value: ");
            getnstr(buf2, sizeof(buf2) - 1);
            int pid = atoi(buf1);
            int nv = atoi(buf2);
            change_priority(pid, nv);
            noecho();
            curs_set(0);
            nodelay(stdscr, TRUE);
        } else if (ch == KEY_F(4) || ch == 'o') {
            set_sort_descending(!get_sort_descending());
        } else if (ch == 'c') {
            show_cores = !show_cores;
        } else if (ch == 'a') {
            show_full_cmd = !show_full_cmd;
        } else if (ch == 'H') {
            show_threads = !show_threads;
            set_thread_mode(show_threads);
        } else if (ch == ' ') {
            paused = !paused;
        } else if (ch == 'h') {
            show_help();
        }
    }
    endwin();
    free(core_usage);
    free(core_prev_total);
    free(core_prev_idle);
    return 0;
}
#endif /* WITH_UI */
