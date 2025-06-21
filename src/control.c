#include "control.h"
#include <signal.h>
#include <sys/resource.h>
#include <errno.h>

int secure_mode;

int send_signal(int pid, int sig) {
    if (secure_mode) {
        errno = EPERM;
        return -1;
    }
    return kill(pid, sig);
}

int change_priority(int pid, int niceval) {
    if (secure_mode) {
        errno = EPERM;
        return -1;
    }
    return setpriority(PRIO_PROCESS, pid, niceval);
}
