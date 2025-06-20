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

static enum sort_field current_sort;
static int (*compare_procs)(const void *, const void *) = cmp_proc_pid;

static void set_sort(enum sort_field sort) {
    current_sort = sort;
    switch (sort) {
    case SORT_PID:
        compare_procs = cmp_proc_pid;
        break;
    case SORT_CPU:
        compare_procs = cmp_proc_cpu;
        break;
    case SORT_MEM:
        compare_procs = cmp_proc_mem;
        break;
    }
}

int run_ui(unsigned int delay_ms, enum sort_field sort) {
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

    set_sort(sort);
    unsigned int interval = delay_ms;
    if (interval < MIN_DELAY_MS)
        interval = MIN_DELAY_MS;
    if (interval > MAX_DELAY_MS)
        interval = MAX_DELAY_MS;
    int ch = 0;
    while (ch != 'q') {
        if (read_cpu_stats(&cs) == 0) {
            unsigned long long idle = cs.idle + cs.iowait;
            unsigned long long total = cs.user + cs.nice + cs.system +
                                       cs.irq + cs.softirq + cs.steal + idle;
            unsigned long long d_total = total - prev_total;
            unsigned long long d_idle = idle - prev_idle;
            if (d_total > 0)
                cpu_usage = 100.0 * (double)(d_total - d_idle) / (double)d_total;
            prev_total = total;
            prev_idle = idle;
        }
        if (read_mem_stats(&ms) == 0 && ms.total > 0) {
            unsigned long long used = ms.total - ms.available;
            mem_usage = 100.0 * (double)used / (double)ms.total;
        }
        read_misc_stats(&misc);

        size_t count = list_processes(procs, MAX_PROC);
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
                 "load %.2f %.2f %.2f  up %.0fs  tasks %d/%d  cpu %5.1f%%  mem %5.1f%%  intv %.1fs%s",
                 misc.load1, misc.load5, misc.load15, misc.uptime,
                 misc.running_tasks, misc.total_tasks, cpu_usage, mem_usage,
                 interval / 1000.0, fbuf);
        mvprintw(1, 0, "%s",
                 "PID      USER     NAME                     STATE  VSIZE    RSS  RSS%  CPU%   TIME     START");
        for (size_t i = 0; i < count && i < LINES - 3; i++) {
            mvprintw(i + 2, 0,
                     "%-8d %-8s %-25s %c %8llu %5ld %6.2f %6.2f %8.0f %-8s",
                     procs[i].pid, procs[i].user, procs[i].name, procs[i].state,
                     procs[i].vsize, procs[i].rss,
                     procs[i].rss_percent, procs[i].cpu_usage,
                     procs[i].cpu_time, procs[i].start_time);
        }
        refresh();
        usleep(interval * 1000);
        ch = getch();
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
        }
    }
    endwin();
    return 0;
}
#endif /* WITH_UI */
