#include "proc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <ctype.h>

/* store previous per-process CPU times between calls */
#define MAX_PREV 1024
struct prev_entry {
    int pid;
    int tid;
    unsigned long long utime;
    unsigned long long stime;
};
static struct prev_entry prev_table[MAX_PREV];
static size_t prev_count;

/* previous total CPU time for usage calculation */
static unsigned long long last_total_cpu;

/* per-core statistics parsed from /proc/stat */
static struct cpu_core_stats *core_stats;
static size_t core_count;

/* optional filters */
static char name_filter[256] = "";
static char user_filter[32] = "";
static char pid_filter[256] = "";
static int pid_list[64];
static size_t pid_list_count;
/* sort order: 0 = ascending, 1 = descending */
static int sort_descending;
/* show threads instead of processes */
static int thread_mode;
static int show_idle = 1;
static int show_accum_time;
static int cpu_irix_mode;
static char state_filter;

void set_sort_descending(int desc) { sort_descending = desc != 0; }
int get_sort_descending(void) { return sort_descending; }

void set_thread_mode(int on) { thread_mode = on != 0; }
int get_thread_mode(void) { return thread_mode; }

void set_show_idle(int on) { show_idle = on != 0; }
int get_show_idle(void) { return show_idle; }

void set_show_accum_time(int on) { show_accum_time = on != 0; }
int get_show_accum_time(void) { return show_accum_time; }

void set_cpu_irix_mode(int on) { cpu_irix_mode = on != 0; }
int get_cpu_irix_mode(void) { return cpu_irix_mode; }

void set_state_filter(char state) { state_filter = state; }
char get_state_filter(void) { return state_filter; }

void set_name_filter(const char *substr) {
    if (substr && *substr) {
        strncpy(name_filter, substr, sizeof(name_filter) - 1);
        name_filter[sizeof(name_filter) - 1] = '\0';
    } else {
        name_filter[0] = '\0';
    }
}

void set_user_filter(const char *user) {
    if (user && *user) {
        strncpy(user_filter, user, sizeof(user_filter) - 1);
        user_filter[sizeof(user_filter) - 1] = '\0';
    } else {
        user_filter[0] = '\0';
    }
}

const char *get_name_filter(void) { return name_filter; }
const char *get_user_filter(void) { return user_filter; }

void set_pid_filter(const char *list) {
    pid_list_count = 0;
    if (list && *list) {
        strncpy(pid_filter, list, sizeof(pid_filter) - 1);
        pid_filter[sizeof(pid_filter) - 1] = '\0';
        char tmp[256];
        strncpy(tmp, list, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char *tok = strtok(tmp, ",");
        while (tok && pid_list_count < (sizeof(pid_list)/sizeof(pid_list[0]))) {
            pid_list[pid_list_count++] = atoi(tok);
            tok = strtok(NULL, ",");
        }
    } else {
        pid_filter[0] = '\0';
    }
}

const char *get_pid_filter(void) { return pid_filter; }

size_t get_cpu_core_count(void) { return core_count; }

const struct cpu_core_stats *get_cpu_core_stats(void) { return core_stats; }

static int match_filter(int pid, const char *name, const char *user, char state) {
    if (pid_list_count > 0) {
        int found = 0;
        for (size_t i = 0; i < pid_list_count; i++) {
            if (pid_list[i] == pid) {
                found = 1;
                break;
            }
        }
        if (!found)
            return 0;
    }
    if (user_filter[0] && strcmp(user_filter, user) != 0)
        return 0;
    if (state_filter && state_filter != state)
        return 0;
    if (name_filter[0]) {
        /* simple case-insensitive substring search */
        const char *h = name;
        const char *n = name_filter;
        size_t nlen = strlen(n);
        for (; *h; h++) {
            size_t i = 0;
            while (i < nlen && h[i] &&
                   tolower((unsigned char)h[i]) ==
                       tolower((unsigned char)n[i]))
                i++;
            if (i == nlen)
                return 1;
        }
        return 0;
    }
    return 1;
}

int read_cpu_stats(struct cpu_stats *stats) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        return -1;
    }

    char buf[256];
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return -1;
    }
    /* parse overall cpu line */
    char cpu_label[16];
    int scanned = sscanf(buf, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                         cpu_label,
                         &stats->user,
                         &stats->nice,
                         &stats->system,
                         &stats->idle,
                         &stats->iowait,
                         &stats->irq,
                         &stats->softirq,
                         &stats->steal);
    if (scanned < 5) {
        fclose(fp);
        return -1;
    }

    /* read per-core lines */
    free(core_stats);
    core_stats = NULL;
    core_count = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        if (strncmp(buf, "cpu", 3) == 0 && isdigit((unsigned char)buf[3])) {
            struct cpu_core_stats tmp;
            scanned = sscanf(buf, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                             cpu_label,
                             &tmp.user,
                             &tmp.nice,
                             &tmp.system,
                             &tmp.idle,
                             &tmp.iowait,
                             &tmp.irq,
                             &tmp.softirq,
                             &tmp.steal);
            if (scanned >= 5) {
                struct cpu_core_stats *new_arr =
                    realloc(core_stats, (core_count + 1) * sizeof(*core_stats));
                if (new_arr) {
                    core_stats = new_arr;
                    core_stats[core_count++] = tmp;
                }
            }
        } else {
            break;
        }
    }
    fclose(fp);

    /* calculate percentages based on previous totals */
    static unsigned long long prev_user = 0;
    static unsigned long long prev_nice = 0;
    static unsigned long long prev_system = 0;
    static unsigned long long prev_idle = 0;
    static unsigned long long prev_iowait = 0;
    static unsigned long long prev_irq = 0;
    static unsigned long long prev_softirq = 0;
    static unsigned long long prev_steal = 0;

    unsigned long long cur_total = stats->user + stats->nice + stats->system +
                                   stats->idle + stats->iowait + stats->irq +
                                   stats->softirq + stats->steal;
    unsigned long long prev_total = prev_user + prev_nice + prev_system +
                                    prev_idle + prev_iowait + prev_irq +
                                    prev_softirq + prev_steal;
    unsigned long long d_total = cur_total - prev_total;

    unsigned long long d_user = stats->user - prev_user;
    unsigned long long d_nice = stats->nice - prev_nice;
    unsigned long long d_system = stats->system - prev_system;
    unsigned long long d_idle = stats->idle - prev_idle;
    unsigned long long d_iowait = stats->iowait - prev_iowait;
    unsigned long long d_irq = stats->irq - prev_irq;
    unsigned long long d_softirq = stats->softirq - prev_softirq;
    unsigned long long d_steal = stats->steal - prev_steal;

    double u_perc = 0.0, n_perc = 0.0, s_perc = 0.0, i_perc = 0.0;
    double iw_perc = 0.0, ir_perc = 0.0, sir_perc = 0.0, st_perc = 0.0;
    if (d_total > 0) {
        u_perc = 100.0 * (double)d_user / (double)d_total;
        n_perc = 100.0 * (double)d_nice / (double)d_total;
        s_perc = 100.0 * (double)d_system / (double)d_total;
        i_perc = 100.0 * (double)d_idle / (double)d_total;
        iw_perc = 100.0 * (double)d_iowait / (double)d_total;
        ir_perc = 100.0 * (double)d_irq / (double)d_total;
        sir_perc = 100.0 * (double)d_softirq / (double)d_total;
        st_perc = 100.0 * (double)d_steal / (double)d_total;
    }

    stats->user_percent = u_perc + n_perc;
    stats->nice_percent = n_perc;
    stats->system_percent = s_perc + ir_perc + sir_perc + st_perc;
    stats->idle_percent = i_perc + iw_perc;
    stats->iowait_percent = iw_perc;
    stats->irq_percent = ir_perc;
    stats->softirq_percent = sir_perc;
    stats->steal_percent = st_perc;

    prev_user = stats->user;
    prev_nice = stats->nice;
    prev_system = stats->system;
    prev_idle = stats->idle;
    prev_iowait = stats->iowait;
    prev_irq = stats->irq;
    prev_softirq = stats->softirq;
    prev_steal = stats->steal;

    return 0;
}

static int read_mem_value(const char *key, unsigned long long *val) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp)
        return -1;
    char line[256];
    size_t keylen = strlen(key);
    int found = -1;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key, keylen) == 0) {
            unsigned long long tmp = 0;
            if (sscanf(line + keylen, ": %llu", &tmp) == 1) {
                *val = tmp;
                found = 0;
                break;
            }
        }
    }
    fclose(fp);
    return found;
}

int read_mem_stats(struct mem_stats *stats) {
    if (read_mem_value("MemTotal", &stats->total) < 0)
        return -1;
    if (read_mem_value("MemFree", &stats->free) < 0)
        return -1;
    if (read_mem_value("MemAvailable", &stats->available) < 0)
        return -1;
    if (read_mem_value("Buffers", &stats->buffers) < 0)
        return -1;
    if (read_mem_value("Cached", &stats->cached) < 0)
        return -1;
    if (read_mem_value("SwapTotal", &stats->swap_total) < 0)
        return -1;
    unsigned long long swap_free = 0;
    if (read_mem_value("SwapFree", &swap_free) < 0)
        return -1;
    if (stats->swap_total >= swap_free)
        stats->swap_used = stats->swap_total - swap_free;
    else
        stats->swap_used = 0;
    return 0;
}

size_t count_processes(void) {
    DIR *dir = opendir("/proc");
    if (!dir)
        return 0;
    struct dirent *ent;
    size_t count = 0;
    while ((ent = readdir(dir)) != NULL) {
        char *endptr;
        long pid = strtol(ent->d_name, &endptr, 10);
        if (*endptr != '\0')
            continue;
        if (get_thread_mode()) {
            char tpath[64];
            snprintf(tpath, sizeof(tpath), "/proc/%ld/task", pid);
            DIR *tdir = opendir(tpath);
            if (!tdir)
                continue;
            struct dirent *tent;
            while ((tent = readdir(tdir)) != NULL) {
                char *e2;
                strtol(tent->d_name, &e2, 10);
                if (*e2 == '\0')
                    count++;
            }
            closedir(tdir);
        } else {
            count++;
        }
    }
    closedir(dir);
    return count;
}

size_t list_processes(struct process_info *buf, size_t max) {
    struct cpu_stats cs;
    unsigned long long total_delta = 1;
    if (read_cpu_stats(&cs) == 0) {
        unsigned long long total = cs.user + cs.nice + cs.system + cs.idle +
                                   cs.iowait + cs.irq + cs.softirq + cs.steal;
        if (last_total_cpu != 0)
            total_delta = total - last_total_cpu;
        last_total_cpu = total;
        if (total_delta == 0)
            total_delta = 1;
    }

    struct mem_stats ms;
    if (read_mem_stats(&ms) != 0)
        ms.total = 1; /* avoid divide by zero */
    long page_kb = getpagesize() / 1024;
    if (page_kb <= 0)
        page_kb = 4;

    DIR *dir = opendir("/proc");
    if (!dir)
        return 0;
    struct dirent *ent;
    size_t count = 0;
    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0)
        clk_tck = 100;

    FILE *upt = fopen("/proc/uptime", "r");
    double up_secs = 0.0;
    if (upt) {
        fscanf(upt, "%lf", &up_secs);
        fclose(upt);
    }
    time_t now = time(NULL);
    double boot_time = (double)now - up_secs;
    while ((ent = readdir(dir)) != NULL && count < max) {
        char *endptr;
        long pid = strtol(ent->d_name, &endptr, 10);
        if (*endptr != '\0')
            continue; /* not a pid */
        char tpath[64];
        if (get_thread_mode()) {
            snprintf(tpath, sizeof(tpath), "/proc/%ld/task", pid);
            DIR *tdir = opendir(tpath);
            if (!tdir)
                continue;
            struct dirent *tent;
            while ((tent = readdir(tdir)) != NULL && count < max) {
                long tid = strtol(tent->d_name, &endptr, 10);
                if (*endptr != '\0')
                    continue;
                char path[64];
                snprintf(path, sizeof(path), "/proc/%ld/task/%ld/stat", pid, tid);
                FILE *fp = fopen(path, "r");
                if (!fp)
                    continue;
                char line[1024];
                if (fgets(line, sizeof(line), fp)) {
                    char comm[256];
                    char state;
                    int ppid;
                    unsigned long long utime, stime, cutime, cstime, starttime;
                    long priority, niceval;
                    unsigned long long vsize;
                    long rss;
                    int cpu = 0;
                    sscanf(line,
                           "%*d (%255[^)]) %c %d %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %llu %llu %llu %llu %ld %ld %*s %*s %llu %llu %ld"
                           " %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %d",
                           comm, &state, &ppid, &utime, &stime, &cutime, &cstime,
                           &priority, &niceval, &starttime, &vsize, &rss, &cpu);

                    unsigned int uid = 0;
                    snprintf(path, sizeof(path), "/proc/%ld/status", pid);
                    FILE *fs = fopen(path, "r");
                    if (fs) {
                        char line2[256];
                        while (fgets(line2, sizeof(line2), fs)) {
                            if (strncmp(line2, "Uid:", 4) == 0) {
                                sscanf(line2 + 4, "%u", &uid);
                                break;
                            }
                        }
                        fclose(fs);
                    }

                    unsigned long long old_utime = 0, old_stime = 0;
                    for (size_t i = 0; i < prev_count; i++) {
                        if (prev_table[i].pid == pid && prev_table[i].tid == tid) {
                            old_utime = prev_table[i].utime;
                            old_stime = prev_table[i].stime;
                            prev_table[i].utime = utime;
                            prev_table[i].stime = stime;
                            break;
                        }
                    }
                    if (old_utime == 0 && old_stime == 0 && prev_count < MAX_PREV) {
                        prev_table[prev_count].pid = pid;
                        prev_table[prev_count].tid = tid;
                        prev_table[prev_count].utime = utime;
                        prev_table[prev_count].stime = stime;
                        prev_count++;
                    }

                    unsigned long long delta = (utime - old_utime) + (stime - old_stime);
                    double usage = 100.0 * (double)delta / (double)total_delta;
                    if (get_cpu_irix_mode()) {
                        size_t ncpu = get_cpu_core_count();
                        if (ncpu > 0)
                            usage *= (double)ncpu;
                    }
                    if (!show_idle && delta == 0) {
                        fclose(fp);
                        continue;
                    }

                    buf[count].pid = (int)pid;
                    buf[count].tid = (int)tid;
                    buf[count].ppid = ppid;
                    buf[count].uid = uid;
                    struct passwd *pw = getpwuid((uid_t)uid);
                    if (pw) {
                        strncpy(buf[count].user, pw->pw_name, sizeof(buf[count].user) - 1);
                        buf[count].user[sizeof(buf[count].user) - 1] = '\0';
                    } else {
                        snprintf(buf[count].user, sizeof(buf[count].user), "%u", uid);
                    }
                    strncpy(buf[count].name, comm, sizeof(buf[count].name) - 1);
                    buf[count].name[sizeof(buf[count].name) - 1] = '\0';

                    snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid);
                    FILE *fc = fopen(path, "r");
                    if (fc) {
                        size_t r = fread(buf[count].cmdline, 1,
                                         sizeof(buf[count].cmdline) - 1, fc);
                        fclose(fc);
                        size_t j = 0;
                        for (size_t i = 0; i < r && j < sizeof(buf[count].cmdline) - 1; i++) {
                            char c = buf[count].cmdline[i];
                            if (c == '\0') {
                                if (j > 0 && buf[count].cmdline[j - 1] != ' ')
                                    buf[count].cmdline[j++] = ' ';
                            } else {
                                buf[count].cmdline[j++] = c;
                            }
                        }
                        if (j > 0 && buf[count].cmdline[j - 1] == ' ')
                            j--; /* strip trailing space */
                        buf[count].cmdline[j] = '\0';
                    } else {
                        buf[count].cmdline[0] = '\0';
                    }

                    if (!match_filter((int)pid, buf[count].name, buf[count].user, state)) {
                        fclose(fp);
                        continue;
                    }

                    buf[count].state = state;
                    buf[count].priority = priority;
                    buf[count].nice = niceval;
                    buf[count].vsize = vsize;
                    long rss_kb = rss * page_kb;
                    buf[count].rss = rss_kb;
                    unsigned long long shared_kb = 0;
                    snprintf(path, sizeof(path), "/proc/%ld/statm", pid);
                    FILE *fm = fopen(path, "r");
                    if (fm) {
                        unsigned long dummy, res, shr;
                        if (fscanf(fm, "%lu %lu %lu", &dummy, &res, &shr) >= 3)
                            shared_kb = shr * page_kb;
                        fclose(fm);
                    }
                    buf[count].shared = shared_kb;
                    buf[count].rss_percent = 100.0 * (double)rss_kb /
                                           (double)ms.total;
                    unsigned long long rb = 0, wb = 0;
                    snprintf(path, sizeof(path), "/proc/%ld/task/%ld/io", pid, tid);
                    FILE *fio = fopen(path, "r");
                    if (fio) {
                        char lineio[256];
                        while (fgets(lineio, sizeof(lineio), fio)) {
                            if (sscanf(lineio, "read_bytes: %llu", &rb) == 1)
                                continue;
                            if (sscanf(lineio, "write_bytes: %llu", &wb) == 1)
                                continue;
                        }
                        fclose(fio);
                    }
                    buf[count].read_bytes = rb;
                    buf[count].write_bytes = wb;
                    buf[count].utime = utime;
                    buf[count].stime = stime;
                    buf[count].cpu_usage = usage;
                    unsigned long long tt = utime + stime;
                    if (get_show_accum_time())
                        tt += cutime + cstime;
                    buf[count].cpu_time = (double)tt / (double)clk_tck;
                    time_t start_epoch = (time_t)(boot_time +
                                                 (double)starttime / (double)clk_tck);
                    struct tm *tm = localtime(&start_epoch);
                    if (tm)
                        strftime(buf[count].start_time, sizeof(buf[count].start_time),
                                 "%H:%M:%S", tm);
                    else
                        strncpy(buf[count].start_time, "??:??:??",
                                sizeof(buf[count].start_time));
                    buf[count].start_timestamp = (double)start_epoch;
                    buf[count].cpu = cpu;
                    buf[count].level = 0;
                    count++;
                }
                fclose(fp);
            }
            closedir(tdir);
        } else {
            char path[64];
            snprintf(path, sizeof(path), "/proc/%ld/stat", pid);
            FILE *fp = fopen(path, "r");
            if (!fp)
                continue;
            char line[1024];
            if (fgets(line, sizeof(line), fp)) {
                char comm[256];
                char state;
                int ppid;
                unsigned long long utime, stime, cutime, cstime, starttime;
                long priority, niceval;
                unsigned long long vsize;
                long rss;
                int cpu = 0;
                sscanf(line,
                       "%*d (%255[^)]) %c %d %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %llu %llu %llu %llu %ld %ld %*s %*s %llu %llu %ld"
                       " %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %d",
                       comm, &state, &ppid, &utime, &stime, &cutime, &cstime,
                       &priority, &niceval, &starttime, &vsize, &rss, &cpu);

                unsigned int uid = 0;
                snprintf(path, sizeof(path), "/proc/%ld/status", pid);
                FILE *fs = fopen(path, "r");
                if (fs) {
                    char line2[256];
                    while (fgets(line2, sizeof(line2), fs)) {
                        if (strncmp(line2, "Uid:", 4) == 0) {
                            sscanf(line2 + 4, "%u", &uid);
                            break;
                        }
                    }
                    fclose(fs);
                }

                unsigned long long old_utime = 0, old_stime = 0;
                for (size_t i = 0; i < prev_count; i++) {
                    if (prev_table[i].pid == pid && prev_table[i].tid == pid) {
                        old_utime = prev_table[i].utime;
                        old_stime = prev_table[i].stime;
                        prev_table[i].utime = utime;
                        prev_table[i].stime = stime;
                        break;
                    }
                }
                if (old_utime == 0 && old_stime == 0 && prev_count < MAX_PREV) {
                    prev_table[prev_count].pid = pid;
                    prev_table[prev_count].tid = pid;
                    prev_table[prev_count].utime = utime;
                    prev_table[prev_count].stime = stime;
                    prev_count++;
                }

                unsigned long long delta = (utime - old_utime) + (stime - old_stime);
                double usage = 100.0 * (double)delta / (double)total_delta;
                if (get_cpu_irix_mode()) {
                    size_t ncpu = get_cpu_core_count();
                    if (ncpu > 0)
                        usage *= (double)ncpu;
                }
                if (!show_idle && delta == 0) {
                    fclose(fp);
                    continue;
                }

                buf[count].pid = (int)pid;
                buf[count].tid = (int)pid;
                buf[count].ppid = ppid;
                buf[count].uid = uid;
                struct passwd *pw = getpwuid((uid_t)uid);
                if (pw) {
                    strncpy(buf[count].user, pw->pw_name, sizeof(buf[count].user) - 1);
                    buf[count].user[sizeof(buf[count].user) - 1] = '\0';
                } else {
                    snprintf(buf[count].user, sizeof(buf[count].user), "%u", uid);
                }
                strncpy(buf[count].name, comm, sizeof(buf[count].name) - 1);
                buf[count].name[sizeof(buf[count].name) - 1] = '\0';

                snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid);
                FILE *fc = fopen(path, "r");
                if (fc) {
                    size_t r = fread(buf[count].cmdline, 1,
                                     sizeof(buf[count].cmdline) - 1, fc);
                    fclose(fc);
                    size_t j = 0;
                    for (size_t i = 0; i < r && j < sizeof(buf[count].cmdline) - 1; i++) {
                        char c = buf[count].cmdline[i];
                        if (c == '\0') {
                            if (j > 0 && buf[count].cmdline[j - 1] != ' ')
                                buf[count].cmdline[j++] = ' ';
                        } else {
                            buf[count].cmdline[j++] = c;
                        }
                    }
                    if (j > 0 && buf[count].cmdline[j - 1] == ' ')
                        j--; /* strip trailing space */
                    buf[count].cmdline[j] = '\0';
                } else {
                    buf[count].cmdline[0] = '\0';
                }

                if (!match_filter((int)pid, buf[count].name, buf[count].user, state)) {
                    fclose(fp);
                    continue;
                }

                buf[count].state = state;
                buf[count].priority = priority;
                buf[count].nice = niceval;
                buf[count].vsize = vsize;
                long rss_kb = rss * page_kb;
                buf[count].rss = rss_kb;
                unsigned long long shared_kb = 0;
                snprintf(path, sizeof(path), "/proc/%ld/statm", pid);
                FILE *fm = fopen(path, "r");
                if (fm) {
                    unsigned long dummy, res, shr;
                    if (fscanf(fm, "%lu %lu %lu", &dummy, &res, &shr) >= 3)
                        shared_kb = shr * page_kb;
                    fclose(fm);
                }
                buf[count].shared = shared_kb;
                buf[count].rss_percent = 100.0 * (double)rss_kb /
                                       (double)ms.total;
                unsigned long long rb = 0, wb = 0;
                snprintf(path, sizeof(path), "/proc/%ld/io", pid);
                FILE *fio = fopen(path, "r");
                if (fio) {
                    char lineio[256];
                    while (fgets(lineio, sizeof(lineio), fio)) {
                        if (sscanf(lineio, "read_bytes: %llu", &rb) == 1)
                            continue;
                        if (sscanf(lineio, "write_bytes: %llu", &wb) == 1)
                            continue;
                    }
                    fclose(fio);
                }
                buf[count].read_bytes = rb;
                buf[count].write_bytes = wb;
                buf[count].utime = utime;
                buf[count].stime = stime;
                buf[count].cpu_usage = usage;
                unsigned long long tt = utime + stime;
                if (get_show_accum_time())
                    tt += cutime + cstime;
                buf[count].cpu_time = (double)tt / (double)clk_tck;
                time_t start_epoch = (time_t)(boot_time +
                                             (double)starttime / (double)clk_tck);
                struct tm *tm = localtime(&start_epoch);
                if (tm)
                    strftime(buf[count].start_time, sizeof(buf[count].start_time),
                             "%H:%M:%S", tm);
                else
                    strncpy(buf[count].start_time, "??:??:??",
                            sizeof(buf[count].start_time));
                buf[count].start_timestamp = (double)start_epoch;
                buf[count].cpu = cpu;
                buf[count].level = 0;
                count++;
            }
            fclose(fp);
        }
    }
    closedir(dir);
    return count;
}

int read_misc_stats(struct misc_stats *stats) {
    FILE *fp = fopen("/proc/loadavg", "r");
    if (!fp)
        return -1;
    double l1 = 0.0, l5 = 0.0, l15 = 0.0;
    int running = 0, total = 0;
    if (fscanf(fp, "%lf %lf %lf %d/%d", &l1, &l5, &l15, &running, &total) < 5) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    fp = fopen("/proc/uptime", "r");
    if (!fp)
        return -1;
    double up = 0.0;
    if (fscanf(fp, "%lf", &up) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    int sleeping = 0;
    int stopped = 0;
    int zombie = 0;
    DIR *dir = opendir("/proc");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            char *endptr;
            long pid = strtol(ent->d_name, &endptr, 10);
            if (*endptr != '\0')
                continue;
            char path[64];
            snprintf(path, sizeof(path), "/proc/%ld/status", pid);
            FILE *fs = fopen(path, "r");
            if (!fs)
                continue;
            char line[256];
            while (fgets(line, sizeof(line), fs)) {
                if (strncmp(line, "State:", 6) == 0) {
                    char st;
                    if (sscanf(line + 6, " %c", &st) == 1) {
                        switch (st) {
                        case 'R':
                            break;
                        case 'S':
                        case 'D':
                            sleeping++;
                            break;
                        case 'T':
                        case 't':
                            stopped++;
                            break;
                        case 'Z':
                            zombie++;
                            break;
                        default:
                            break;
                        }
                    }
                    break;
                }
            }
            fclose(fs);
        }
        closedir(dir);
    }

    stats->load1 = l1;
    stats->load5 = l5;
    stats->load15 = l15;
    stats->uptime = up;
    stats->running_tasks = running;
    stats->total_tasks = total;
    stats->sleeping_tasks = sleeping;
    stats->stopped_tasks = stopped;
    stats->zombie_tasks = zombie;
    return 0;
}

int cmp_proc_pid(const void *a, const void *b) {
    const struct process_info *pa = a;
    const struct process_info *pb = b;
    int diff = pa->pid - pb->pid;
    if (diff == 0) {
        if (get_thread_mode())
            diff = pa->tid - pb->tid;
    }
    if (diff == 0)
        return 0;
    return sort_descending ? -diff : diff;
}

int cmp_proc_cpu(const void *a, const void *b) {
    const struct process_info *pa = a;
    const struct process_info *pb = b;
    int res = 0;
    if (pa->cpu_usage < pb->cpu_usage)
        res = -1;
    else if (pa->cpu_usage > pb->cpu_usage)
        res = 1;
    if (sort_descending)
        res = -res;
    return res;
}

int cmp_proc_mem(const void *a, const void *b) {
    const struct process_info *pa = a;
    const struct process_info *pb = b;
    int res = 0;
    if (pa->rss < pb->rss)
        res = -1;
    else if (pa->rss > pb->rss)
        res = 1;
    if (sort_descending)
        res = -res;
    return res;
}

int cmp_proc_vsize(const void *a, const void *b) {
    const struct process_info *pa = a;
    const struct process_info *pb = b;
    int res = 0;
    if (pa->vsize < pb->vsize)
        res = -1;
    else if (pa->vsize > pb->vsize)
        res = 1;
    if (sort_descending)
        res = -res;
    return res;
}

int cmp_proc_time(const void *a, const void *b) {
    const struct process_info *pa = a;
    const struct process_info *pb = b;
    int res = 0;
    if (pa->cpu_time < pb->cpu_time)
        res = -1;
    else if (pa->cpu_time > pb->cpu_time)
        res = 1;
    if (sort_descending)
        res = -res;
    return res;
}

int cmp_proc_priority(const void *a, const void *b) {
    const struct process_info *pa = a;
    const struct process_info *pb = b;
    long diff = pa->priority - pb->priority;
    int res = 0;
    if (diff < 0)
        res = -1;
    else if (diff > 0)
        res = 1;
    if (sort_descending)
        res = -res;
    return res;
}

int cmp_proc_user(const void *a, const void *b) {
    const struct process_info *pa = a;
    const struct process_info *pb = b;
    int res = strcasecmp(pa->user, pb->user);
    if (res == 0)
        res = cmp_proc_pid(a, b);
    if (sort_descending)
        res = -res;
    return res;
}

int cmp_proc_start(const void *a, const void *b) {
    const struct process_info *pa = a;
    const struct process_info *pb = b;
    int res = 0;
    if (pa->start_timestamp < pb->start_timestamp)
        res = -1;
    else if (pa->start_timestamp > pb->start_timestamp)
        res = 1;
    if (sort_descending)
        res = -res;
    return res;
}
