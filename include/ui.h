#ifndef UI_H
#define UI_H

enum sort_field {
    SORT_PID,
    SORT_CPU,
    SORT_MEM
};

int run_ui(unsigned int delay_ms, enum sort_field sort);

#endif /* UI_H */
