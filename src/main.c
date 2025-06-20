#include <stdio.h>
#include "version.h"

int main(void) {
#ifdef _POSIX_VERSION
    /* Placeholder for future POSIX-specific code */
#endif
    printf("vtop version %s\n", VTOP_VERSION);
    return 0;
}
