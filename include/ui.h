#ifndef UI_H
#define UI_H

enum sort_field {
    SORT_PID,
    SORT_NAME,
    SORT_VSIZE,
    SORT_RSS
};

int run_ui(unsigned int delay_ms, enum sort_field sort);

#endif /* UI_H */
