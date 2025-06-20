#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "version.h"
#include "ui.h"

static void usage(const char *prog) {
    printf("Usage: %s [-d seconds] [-s column]\n", prog);
    printf("  -d SECS   Refresh delay in seconds (default 3)\n");
    printf("  -s COL    Sort column: pid,name,vsize,rss (default pid)\n");
}

int main(int argc, char *argv[]) {
    unsigned int delay_ms = 3000; /* default 3 seconds */
    enum sort_field sort = SORT_PID;

    int opt;
    while ((opt = getopt(argc, argv, "d:s:h")) != -1) {
        switch (opt) {
        case 'd':
            delay_ms = (unsigned int)(strtod(optarg, NULL) * 1000);
            break;
        case 's':
            if (strcmp(optarg, "name") == 0)
                sort = SORT_NAME;
            else if (strcmp(optarg, "vsize") == 0)
                sort = SORT_VSIZE;
            else if (strcmp(optarg, "rss") == 0)
                sort = SORT_RSS;
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
