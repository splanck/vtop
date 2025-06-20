#include "control.h"
#include <signal.h>
#include <sys/resource.h>

int send_signal(int pid, int sig) {
    return kill(pid, sig);
}

int change_priority(int pid, int niceval) {
    return setpriority(PRIO_PROCESS, pid, niceval);
}
