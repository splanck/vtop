#ifndef CONTROL_H
#define CONTROL_H

extern int secure_mode;

int send_signal(int pid, int sig);
int change_priority(int pid, int niceval);

#endif /* CONTROL_H */
