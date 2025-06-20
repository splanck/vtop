#include <stdio.h>
#include "version.h"
#ifdef WITH_UI
#include "ui.h"
#endif

int main(void) {
#ifdef WITH_UI
    return run_ui();
#else
    printf("vtop version %s\n", VTOP_VERSION);
    return 0;
#endif
}
