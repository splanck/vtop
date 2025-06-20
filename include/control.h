#ifndef CONTROL_H
#define CONTROL_H

int send_signal(int pid, int sig);
int change_priority(int pid, int niceval);

#endif /* CONTROL_H */
