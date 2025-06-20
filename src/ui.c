#include "proc.h"
#include "ui.h"
#ifdef WITH_UI
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PROC 256

static enum sort_field current_sort;

static int compare_procs(const void *a, const void *b) {
    const struct process_info *pa = a;
    const struct process_info *pb = b;
    switch (current_sort) {
    case SORT_PID:
        return pa->pid - pb->pid;
    case SORT_NAME:
        return strcmp(pa->name, pb->name);
    case SORT_VSIZE:
        if (pa->vsize < pb->vsize) return 1;
        if (pa->vsize > pb->vsize) return -1;
        return 0;
    case SORT_RSS:
        if (pa->rss < pb->rss) return 1;
        if (pa->rss > pb->rss) return -1;
        return 0;
    }
    return 0;
}

int run_ui(unsigned int delay_ms, enum sort_field sort) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    struct process_info procs[MAX_PROC];

    current_sort = sort;
    int ch = 0;
    while (ch != 'q') {
        size_t count = list_processes(procs, MAX_PROC);
        qsort(procs, count, sizeof(struct process_info), compare_procs);
        erase();
        mvprintw(0, 0, "PID      NAME                     STATE  VSIZE    RSS");
        for (size_t i = 0; i < count && i < LINES - 2; i++) {
            mvprintw(i + 1, 0, "%-8d %-25s %c %8llu %5ld",
                     procs[i].pid, procs[i].name, procs[i].state,
                     procs[i].vsize, procs[i].rss);
        }
        refresh();
        usleep(delay_ms * 1000);
        ch = getch();
    }
    endwin();
    return 0;
}
#endif /* WITH_UI */
