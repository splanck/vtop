#include "proc.h"
#ifdef WITH_UI
#include <ncurses.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_PROC 256

int run_ui(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    struct process_info procs[MAX_PROC];

    int ch = 0;
    while (ch != 'q') {
        size_t count = list_processes(procs, MAX_PROC);
        erase();
        mvprintw(0, 0, "PID      NAME                     STATE  VSIZE    RSS");
        for (size_t i = 0; i < count && i < LINES - 2; i++) {
            mvprintw(i + 1, 0, "%-8d %-25s %c %8llu %5ld",
                     procs[i].pid, procs[i].name, procs[i].state,
                     procs[i].vsize, procs[i].rss);
        }
        refresh();
        usleep(500000); /* half a second */
        ch = getch();
    }
    endwin();
    return 0;
}
#endif /* WITH_UI */
