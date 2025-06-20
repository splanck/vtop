#include "proc.h"
#include "ui.h"
#ifdef WITH_UI
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PROC 256

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

    set_sort(sort);
    int ch = 0;
    while (ch != 'q') {
        size_t count = list_processes(procs, MAX_PROC);
        qsort(procs, count, sizeof(struct process_info), compare_procs);
        erase();
        mvprintw(0, 0, "%s", "PID      NAME                     STATE  VSIZE    RSS  RSS%  CPU%");
        for (size_t i = 0; i < count && i < LINES - 2; i++) {
            mvprintw(i + 1, 0, "%-8d %-25s %c %8llu %5ld %6.2f %6.2f",
                     procs[i].pid, procs[i].name, procs[i].state,
                     procs[i].vsize, procs[i].rss,
                     procs[i].rss_percent, procs[i].cpu_usage);
        }
        refresh();
        usleep(delay_ms * 1000);
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
        }
    }
    endwin();
    return 0;
}
#endif /* WITH_UI */
