#include "proc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>

/* store previous per-process CPU times between calls */
#define MAX_PREV 1024
struct prev_entry {
    int pid;
    unsigned long long utime;
    unsigned long long stime;
};
static struct prev_entry prev_table[MAX_PREV];
static size_t prev_count;

/* previous total CPU time for usage calculation */
static unsigned long long last_total_cpu;

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
    fclose(fp);
    /* parse first line: cpu  user nice system idle iowait irq softirq steal */
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
    return scanned >= 5 ? 0 : -1;
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
    return 0;
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
        char path[64];
        snprintf(path, sizeof(path), "/proc/%ld/stat", pid);
        FILE *fp = fopen(path, "r");
        if (!fp)
            continue;
        char line[1024];
        if (fgets(line, sizeof(line), fp)) {
            /* pid (comm) state ... utime stime ... starttime vsize rss */
            char comm[256];
            char state;
            unsigned long long utime, stime, starttime;
            unsigned long long vsize;
            long rss;
            sscanf(line,
                   "%*d (%255[^)]) %c %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %llu %llu %*s %*s %*s %*s %*s %*s %llu %llu %ld",
                   comm, &state, &utime, &stime, &starttime, &vsize, &rss);

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
                if (prev_table[i].pid == pid) {
                    old_utime = prev_table[i].utime;
                    old_stime = prev_table[i].stime;
                    prev_table[i].utime = utime;
                    prev_table[i].stime = stime;
                    break;
                }
            }
            if (old_utime == 0 && old_stime == 0 && prev_count < MAX_PREV) {
                prev_table[prev_count].pid = pid;
                prev_table[prev_count].utime = utime;
                prev_table[prev_count].stime = stime;
                prev_count++;
            }

            unsigned long long delta = (utime - old_utime) + (stime - old_stime);
            double usage = 100.0 * (double)delta / (double)total_delta;

            buf[count].pid = (int)pid;
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
            buf[count].state = state;
            buf[count].vsize = vsize;
            buf[count].rss = rss;
            buf[count].rss_percent = 100.0 * (double)rss * (double)page_kb /
                                   (double)ms.total;
            buf[count].utime = utime;
            buf[count].stime = stime;
            buf[count].cpu_usage = usage;
            buf[count].cpu_time = (double)(utime + stime) / (double)clk_tck;
            time_t start_epoch = (time_t)(boot_time +
                                         (double)starttime / (double)clk_tck);
            struct tm *tm = localtime(&start_epoch);
            if (tm)
                strftime(buf[count].start_time, sizeof(buf[count].start_time),
                         "%H:%M:%S", tm);
            else
                strncpy(buf[count].start_time, "??:??:??",
                        sizeof(buf[count].start_time));
            count++;
        }
        fclose(fp);
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

    stats->load1 = l1;
    stats->load5 = l5;
    stats->load15 = l15;
    stats->uptime = up;
    stats->running_tasks = running;
    stats->total_tasks = total;
    return 0;
}

int cmp_proc_pid(const void *a, const void *b) {
    const struct process_info *pa = a;
    const struct process_info *pb = b;
    return pa->pid - pb->pid;
}

int cmp_proc_cpu(const void *a, const void *b) {
    const struct process_info *pa = a;
    const struct process_info *pb = b;
    if (pa->cpu_usage < pb->cpu_usage)
        return 1;
    if (pa->cpu_usage > pb->cpu_usage)
        return -1;
    return 0;
}

int cmp_proc_mem(const void *a, const void *b) {
    const struct process_info *pa = a;
    const struct process_info *pb = b;
    if (pa->rss < pb->rss)
        return 1;
    if (pa->rss > pb->rss)
        return -1;
    return 0;
}
