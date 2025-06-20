#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "version.h"
#include "ui.h"

static void usage(const char *prog) {
    printf("Usage: %s [-d seconds] [-s column]\n", prog);
    printf("  -d, --delay SECS   Refresh delay in seconds (default 3)\n");
    printf("  -s, --sort  COL    Sort column: pid,cpu,mem (default pid)\n");
}

int main(int argc, char *argv[]) {
    unsigned int delay_ms = 3000; /* default 3 seconds */
    enum sort_field sort = SORT_PID;

    static struct option long_opts[] = {
        {"delay", required_argument, NULL, 'd'},
        {"sort", required_argument, NULL, 's'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt, idx;
    while ((opt = getopt_long(argc, argv, "d:s:h", long_opts, &idx)) != -1) {
        switch (opt) {
        case 'd':
            delay_ms = (unsigned int)(strtod(optarg, NULL) * 1000);
            break;
        case 's':
            if (strcmp(optarg, "cpu") == 0)
                sort = SORT_CPU;
            else if (strcmp(optarg, "mem") == 0)
                sort = SORT_MEM;
            else
                sort = SORT_PID;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 0;
        }
    }

#ifdef WITH_UI
    return run_ui(delay_ms, sort);
#else
    (void)delay_ms; /* unused */
    (void)sort;     /* unused */
    printf("vtop version %s\n", VTOP_VERSION);
    return 0;
#endif
}
