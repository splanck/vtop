#include "proc.h"
#include "ui.h"
#include "control.h"
#ifdef WITH_UI
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

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
static int show_idle = 1;
static int color_enabled = 1;
static int show_forest;

#define CP_SORT 1
#define CP_RUNNING 2

static enum sort_field current_sort;
static int (*compare_procs)(const void *, const void *) = cmp_proc_pid;

enum column_id {
    COL_PID,
    COL_TID,
    COL_USER,
    COL_CMD,
    COL_STATE,
    COL_PRI,
    COL_NICE,
    COL_VSIZE,
    COL_RSS,
    COL_RSSP,
    COL_CPUP,
    COL_TIME,
    COL_START,
    COL_COUNT
};

struct column_def {
    enum column_id id;
    const char *title;
    int width;
    int left;
    int enabled;
};

static struct column_def columns[COL_COUNT] = {
    {COL_PID,   "PID",     8, 1, 1},
    {COL_TID,   "TID",     8, 1, 0},
    {COL_USER,  "USER",    8, 1, 1},
    {COL_CMD,   "NAME",   25, 1, 1},
    {COL_STATE, "STATE",   5, 1, 1},
    {COL_PRI,   "PRI",     4, 0, 1},
    {COL_NICE,  "NICE",    5, 0, 1},
    {COL_VSIZE, "VSIZE",   8, 0, 1},
    {COL_RSS,   "RSS",     5, 0, 1},
    {COL_RSSP,  "RSS%",    6, 0, 1},
    {COL_CPUP,  "CPU%",    6, 0, 1},
    {COL_TIME,  "TIME",    8, 0, 1},
    {COL_START, "START",   8, 1, 1}
};

/* configuration file helpers */
static const char *get_config_path(void) {
    const char *home = getenv("HOME");
    if (!home)
        home = ".";
    static char path[512];
    snprintf(path, sizeof(path), "%s/.vtoprc", home);
    return path;
}

int ui_load_config(unsigned int *delay_ms, enum sort_field *sort) {
    const char *path = get_config_path();
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        if (line[0] == '#' || line[0] == '\0')
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        if (strcmp(key, "interval_ms") == 0) {
            if (delay_ms)
                *delay_ms = (unsigned int)strtoul(val, NULL, 10);
        } else if (strcmp(key, "sort") == 0) {
            if (sort) {
                if (strcmp(val, "cpu") == 0)
                    *sort = SORT_CPU;
                else if (strcmp(val, "mem") == 0)
                    *sort = SORT_MEM;
                else if (strcmp(val, "time") == 0)
                    *sort = SORT_TIME;
                else if (strcmp(val, "pri") == 0 ||
                         strcmp(val, "priority") == 0)
                    *sort = SORT_PRI;
                else
                    *sort = SORT_PID;
            }
        } else if (strcmp(key, "show_cores") == 0) {
            show_cores = atoi(val);
        } else if (strcmp(key, "show_full_cmd") == 0) {
            show_full_cmd = atoi(val);
        } else if (strcmp(key, "show_threads") == 0) {
            show_threads = atoi(val);
        } else if (strcmp(key, "show_idle") == 0) {
            show_idle = atoi(val);
        } else if (strcmp(key, "show_forest") == 0) {
            show_forest = atoi(val);
        } else if (strcmp(key, "color_enabled") == 0) {
            color_enabled = atoi(val);
        } else if (strcmp(key, "columns") == 0) {
            char *tok = strtok(val, ",");
            for (int i = 0; i < COL_COUNT && tok; i++) {
                columns[i].enabled = atoi(tok);
                tok = strtok(NULL, ",");
            }
        }
    }
    fclose(fp);
    return 0;
}

int ui_save_config(unsigned int delay_ms, enum sort_field sort) {
    const char *path = get_config_path();
    FILE *fp = fopen(path, "w");
    if (!fp)
        return -1;
    fprintf(fp, "interval_ms=%u\n", delay_ms);
    const char *s = "pid";
    if (sort == SORT_CPU)
        s = "cpu";
    else if (sort == SORT_MEM)
        s = "mem";
    else if (sort == SORT_TIME)
        s = "time";
    else if (sort == SORT_PRI)
        s = "pri";
    fprintf(fp, "sort=%s\n", s);
    fprintf(fp, "show_cores=%d\n", show_cores);
    fprintf(fp, "show_full_cmd=%d\n", show_full_cmd);
    fprintf(fp, "show_threads=%d\n", show_threads);
    fprintf(fp, "show_idle=%d\n", show_idle);
    fprintf(fp, "show_forest=%d\n", show_forest);
    fprintf(fp, "color_enabled=%d\n", color_enabled);
    fprintf(fp, "columns=");
    for (int i = 0; i < COL_COUNT; i++) {
        fprintf(fp, "%d", columns[i].enabled);
        if (i < COL_COUNT - 1)
            fputc(',', fp);
    }
    fputc('\n', fp);
    fclose(fp);
    return 0;
}

static int column_visible(int idx) {
    if (columns[idx].id == COL_TID && !show_threads)
        return 0;
    return columns[idx].enabled;
}

static void update_column_titles(void) {
    columns[COL_CMD].title = show_full_cmd ? "COMMAND" : "NAME";
}

static enum column_id get_sort_column(void) {
    switch (current_sort) {
    case SORT_CPU:
        return COL_CPUP;
    case SORT_MEM:
        return COL_RSS;
    case SORT_TIME:
        return COL_TIME;
    case SORT_PRI:
        return COL_PRI;
    case SORT_PID:
    default:
        return COL_PID;
    }
}

static void draw_header(int row) {
    update_column_titles();
    int x = 0;
    enum column_id sort_col = get_sort_column();
    for (int i = 0; i < COL_COUNT; i++) {
        if (!column_visible(i))
            continue;
        if (color_enabled && columns[i].id == sort_col)
            attron(COLOR_PAIR(CP_SORT));
        mvprintw(row, x, "%-*s", columns[i].width, columns[i].title);
        if (color_enabled && columns[i].id == sort_col)
            attroff(COLOR_PAIR(CP_SORT));
        x += columns[i].width + 1;
    }
}

static void draw_process_row(int row, const struct process_info *p) {
    int x = 0;
    enum column_id sort_col = get_sort_column();
    if (color_enabled && p->state == 'R')
        attron(COLOR_PAIR(CP_RUNNING));
    for (int i = 0; i < COL_COUNT; i++) {
        if (!column_visible(i))
            continue;
        if (color_enabled && columns[i].id == sort_col)
            attron(COLOR_PAIR(CP_SORT));
        switch (columns[i].id) {
        case COL_PID:
            mvprintw(row, x, columns[i].left ? "%-*d" : "%*d",
                     columns[i].width, p->pid);
            break;
        case COL_TID:
            mvprintw(row, x, columns[i].left ? "%-*d" : "%*d",
                     columns[i].width, p->tid);
            break;
        case COL_USER:
            mvprintw(row, x, columns[i].left ? "%-*s" : "%*s",
                     columns[i].width, p->user);
            break;
        case COL_CMD: {
            const char *d = show_full_cmd && p->cmdline[0] ? p->cmdline : p->name;
            char buf[512];
            if (show_forest) {
                int indent = p->level * 2;
                if (indent < (int)sizeof(buf) - 1) {
                    snprintf(buf, sizeof(buf), "%*s%s", indent, "", d);
                    d = buf;
                }
            }
            mvprintw(row, x, columns[i].left ? "%-*.*s" : "%*.*s",
                     columns[i].width, columns[i].width, d);
            break;
        }
        case COL_STATE: {
            char buf[2] = {p->state, '\0'};
            mvprintw(row, x, columns[i].left ? "%-*s" : "%*s",
                     columns[i].width, buf);
            break;
        }
        case COL_PRI:
            mvprintw(row, x, columns[i].left ? "%-*ld" : "%*ld",
                     columns[i].width, p->priority);
            break;
        case COL_NICE:
            mvprintw(row, x, columns[i].left ? "%-*ld" : "%*ld",
                     columns[i].width, p->nice);
            break;
        case COL_VSIZE: {
            double vsz = scale_kb((p->vsize / 1024), proc_unit);
            mvprintw(row, x, columns[i].left ? "%-*.1f" : "%*.1f",
                     columns[i].width, vsz);
            break;
        }
        case COL_RSS: {
            double rss = scale_kb((unsigned long long)p->rss, proc_unit);
            mvprintw(row, x, columns[i].left ? "%-*.1f" : "%*.1f",
                     columns[i].width, rss);
            break;
        }
        case COL_RSSP:
            mvprintw(row, x, columns[i].left ? "%-*.2f" : "%*.2f",
                     columns[i].width, p->rss_percent);
            break;
        case COL_CPUP:
            mvprintw(row, x, columns[i].left ? "%-*.2f" : "%*.2f",
                     columns[i].width, p->cpu_usage);
            break;
        case COL_TIME:
            mvprintw(row, x, columns[i].left ? "%-*.0f" : "%*.0f",
                     columns[i].width, p->cpu_time);
            break;
        case COL_START:
            mvprintw(row, x, columns[i].left ? "%-*s" : "%*s",
                     columns[i].width, p->start_time);
            break;
        default:
            break;
        }
        if (color_enabled && columns[i].id == sort_col)
            attroff(COLOR_PAIR(CP_SORT));
        x += columns[i].width + 1;
    }
    if (color_enabled && p->state == 'R')
        attroff(COLOR_PAIR(CP_RUNNING));
}

static void field_manager(void) {
    const int n = COL_COUNT;
    int sel = 0;
    int h = n + 4;
    int w = 20;
    int startx = COLS > w ? (COLS - w) / 2 : 0;
    if (startx < 0)
        startx = 0;
    int starty = LINES > h ? (LINES - h) / 2 : 0;
    if (starty < 0)
        starty = 0;
    WINDOW *win = newwin(h, w, starty, startx);
    keypad(win, TRUE);
    nodelay(stdscr, FALSE);
    int ch = 0;
    while (ch != '\n' && ch != 'q' && ch != 27) {
        box(win, 0, 0);
        mvwprintw(win, 1, 2, "Toggle fields:");
        for (int i = 0; i < n; i++) {
            mvwprintw(win, i + 2, 2, "[%c] %s", columns[i].enabled ? 'x' : ' ',
                     columns[i].title);
        }
        wmove(win, sel + 2, 1);
        wrefresh(win);
        ch = wgetch(win);
        if (ch == KEY_UP && sel > 0)
            sel--;
        else if (ch == KEY_DOWN && sel < n - 1)
            sel++;
        else if (ch == ' ')
            columns[sel].enabled = !columns[sel].enabled;
    }
    delwin(win);
    nodelay(stdscr, TRUE);
}

static void show_help(void) {
    const int h = 25;
    const int w = 52;
    int startx = COLS > w ? (COLS - w) / 2 : 0;
    if (startx < 0)
        startx = 0;
    int starty = LINES > h ? (LINES - h) / 2 : 0;
    if (starty < 0)
        starty = 0;
    WINDOW *win = newwin(h, w, starty, startx);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "Key bindings:");
    mvwprintw(win, 3, 2, "q  Quit");
    mvwprintw(win, 4, 2, "F3/>/<  Change sort field");
    mvwprintw(win, 5, 2, "T       Sort by time");
    mvwprintw(win, 6, 2, "P       Sort by priority");
    mvwprintw(win, 7, 2, "F4/o    Toggle sort order");
    mvwprintw(win, 8, 2, "+/-     Adjust refresh delay");
    mvwprintw(win, 9, 2, "d/s     Set refresh delay");
    mvwprintw(win, 10, 2, "/       Filter by command name");
    mvwprintw(win, 11, 2, "u       Filter by user");
    mvwprintw(win, 12, 2, "k       Send signal to a process");
    mvwprintw(win, 13, 2, "r       Renice a process");
    mvwprintw(win, 14, 2, "c       Toggle per-core view");
    mvwprintw(win, 15, 2, "a       Toggle full command");
    mvwprintw(win, 16, 2, "H       Toggle thread view");
    mvwprintw(win, 17, 2, "i       Toggle idle processes");
    mvwprintw(win, 18, 2, "V       Toggle process tree");
    mvwprintw(win, 19, 2, "z       Toggle colors");
    mvwprintw(win, 20, 2, "f       Field manager");
    mvwprintw(win, 21, 2, "W       Save config");
    mvwprintw(win, 22, 2, "SPACE    Pause/resume");
    mvwprintw(win, 23, 2, "h       Show this help");
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
    case SORT_TIME:
        compare_procs = cmp_proc_time;
        set_sort_descending(1);
        break;
    case SORT_PRI:
        compare_procs = cmp_proc_priority;
        set_sort_descending(0);
        break;
    }
}

/* Build process list in parent->child order and set indentation levels */
static void add_subtree(size_t idx, struct process_info *src, size_t count,
                        int *used, struct process_info *dest, size_t *out,
                        int level) {
    used[idx] = 1;
    dest[*out] = src[idx];
    dest[*out].level = level;
    (*out)++;
    for (size_t i = 0; i < count; i++) {
        if (!used[i] && src[i].ppid == src[idx].pid) {
            add_subtree(i, src, count, used, dest, out, level + 1);
        }
    }
}

static void build_forest(struct process_info *procs, size_t count) {
    if (count == 0)
        return;
    struct process_info *tmp = malloc(count * sizeof(*tmp));
    int *used = calloc(count, sizeof(int));
    size_t out = 0;
    for (size_t i = 0; i < count; i++) {
        int parent_found = 0;
        for (size_t j = 0; j < count; j++) {
            if (procs[i].ppid == procs[j].pid) {
                parent_found = 1;
                break;
            }
        }
        if (!parent_found && !used[i]) {
            add_subtree(i, procs, count, used, tmp, &out, 0);
        }
    }
    for (size_t i = 0; i < count; i++) {
        if (!used[i])
            add_subtree(i, procs, count, used, tmp, &out, 0);
    }
    memcpy(procs, tmp, count * sizeof(*tmp));
    free(tmp);
    free(used);
}

int run_ui(unsigned int delay_ms, enum sort_field sort,
           unsigned int iterations, int columns) {
    initscr();
    if (columns > 0)
        resizeterm(LINES, columns);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_SORT, COLOR_YELLOW, -1);
        init_pair(CP_RUNNING, COLOR_GREEN, -1);
    }

    struct process_info *procs = NULL;
    size_t proc_cap = 0;
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
    set_show_idle(show_idle);
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

        if (!paused) {
            size_t need = count_processes();
            if (need > proc_cap) {
                struct process_info *tmp = realloc(procs, need * sizeof(*procs));
                if (tmp) {
                    procs = tmp;
                    proc_cap = need;
                }
            }
            count = list_processes(procs, proc_cap);
        }
        if (show_forest) {
            qsort(procs, count, sizeof(struct process_info), cmp_proc_pid);
            build_forest(procs, count);
        } else {
            qsort(procs, count, sizeof(struct process_info), compare_procs);
        }
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
        double swap_u = scale_kb(ms.swap_used, summary_unit);
        double swap_t = scale_kb(ms.swap_total, summary_unit);
        const char *unit = mem_unit_suffix(summary_unit);
        mvprintw(0, 0,
                 "load %.2f %.2f %.2f  up %.0fs  tasks %d/%d  cpu %5.1f%% us %.1f%% sy %.1f%% ni %.1f%% id %.1f%% wa %.1f%% hi %.1f%% si %.1f%% st %.1f%%  mem %5.1f%%  swap %.0f/%.0f%s %.1f%%  intv %.1fs%s%s",
                 misc.load1, misc.load5, misc.load15, misc.uptime,
                 misc.running_tasks, misc.total_tasks, cpu_usage,
                 cs.user_percent - cs.nice_percent, cs.system_percent - cs.irq_percent - cs.softirq_percent - cs.steal_percent,
                 cs.nice_percent, cs.idle_percent - cs.iowait_percent,
                 cs.iowait_percent, cs.irq_percent, cs.softirq_percent, cs.steal_percent,
                 mem_usage, swap_u, swap_t, unit, swap_usage,
                 interval / 1000.0, paused ? " [PAUSED]" : "", fbuf);

        double total = scale_kb(ms.total, summary_unit);
        double used = scale_kb(ms.total - ms.free, summary_unit);
        double free = scale_kb(ms.free, summary_unit);
        double bufs = scale_kb(ms.buffers, summary_unit);
        double cached = scale_kb(ms.cached, summary_unit);
        mvprintw(1, 0,
                 "mem total %.0f%s used %.0f%s free %.0f%s buf %.0f%s cache %.0f%s swap %.0f/%.0f%s",
                 total, unit, used, unit, free, unit, bufs, unit, cached, unit,
                 swap_u, swap_t, unit);

        int row = 2;
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
            mvprintw(row, 0, "%s", cbuf);
            row++;
        }
        draw_header(row);
        for (size_t i = 0; i < count && i < LINES - row - 2; i++) {
            draw_process_row(i + row + 1, &procs[i]);
        }
        refresh();
        usleep(interval * 1000);
        ch = getch();
        iter++;
        if (ch == KEY_F(3) || ch == '>') {
            if (current_sort == SORT_PRI)
                set_sort(SORT_PID);
            else
                set_sort(current_sort + 1);
        } else if (ch == '<') {
            if (current_sort == SORT_PID)
                set_sort(SORT_PRI);
            else
                set_sort(current_sort - 1);
        } else if (ch == '+') {
            if (interval + 100 <= MAX_DELAY_MS)
                interval += 100;
        } else if (ch == '-') {
            if (interval > MIN_DELAY_MS)
                interval -= 100;
        } else if (ch == 'd' || ch == 's') {
            char buf[16];
            nodelay(stdscr, FALSE);
            echo();
            curs_set(1);
            mvprintw(LINES - 1, 0, "Delay (s): ");
            getnstr(buf, sizeof(buf) - 1);
            double val = atof(buf);
            if (val > 0.0) {
                interval = (unsigned int)(val * 1000.0);
                if (interval < MIN_DELAY_MS)
                    interval = MIN_DELAY_MS;
                if (interval > MAX_DELAY_MS)
                    interval = MAX_DELAY_MS;
            }
            noecho();
            curs_set(0);
            nodelay(stdscr, TRUE);
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
            char buf1[16];
            char buf2[16];
            nodelay(stdscr, FALSE);
            echo();
            curs_set(1);
            mvprintw(LINES - 1, 0, "PID to signal: ");
            getnstr(buf1, sizeof(buf1) - 1);
            mvprintw(LINES - 1, 0, "Signal number: ");
            getnstr(buf2, sizeof(buf2) - 1);
            int pid = atoi(buf1);
            int sig = atoi(buf2);
            send_signal(pid, sig);
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
        } else if (ch == 'T') {
            set_sort(SORT_TIME);
        } else if (ch == 'P') {
            set_sort(SORT_PRI);
        } else if (ch == 'c') {
            show_cores = !show_cores;
        } else if (ch == 'a') {
            show_full_cmd = !show_full_cmd;
        } else if (ch == 'H') {
            show_threads = !show_threads;
            set_thread_mode(show_threads);
        } else if (ch == 'i') {
            show_idle = !show_idle;
            set_show_idle(show_idle);
        } else if (ch == 'V') {
            show_forest = !show_forest;
        } else if (ch == 'z') {
            color_enabled = !color_enabled;
            if (!color_enabled)
                attrset(A_NORMAL);
        } else if (ch == 'W') {
            ui_save_config(interval, current_sort);
        } else if (ch == 'f') {
            field_manager();
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
    free(procs);
    ui_save_config(interval, current_sort);
    return 0;
}
#endif /* WITH_UI */
