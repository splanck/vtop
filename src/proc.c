#include "proc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

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
    DIR *dir = opendir("/proc");
    if (!dir)
        return 0;
    struct dirent *ent;
    size_t count = 0;
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
            /* pid (comm) state ... vsize rss */
            char comm[256];
            char state;
            unsigned long long vsize;
            long rss;
            /* We only parse required fields */
            sscanf(line, "%*d (%255[^)]) %c %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %llu %ld",
                   comm, &state, &vsize, &rss);
            buf[count].pid = (int)pid;
            strncpy(buf[count].name, comm, sizeof(buf[count].name) - 1);
            buf[count].name[sizeof(buf[count].name) - 1] = '\0';
            buf[count].state = state;
            buf[count].vsize = vsize;
            buf[count].rss = rss;
            count++;
        }
        fclose(fp);
    }
    closedir(dir);
    return count;
}
