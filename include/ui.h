#ifndef UI_H
#define UI_H

enum sort_field {
    SORT_PID,
    SORT_CPU,
    SORT_MEM
};

int run_ui(unsigned int delay_ms, enum sort_field sort,
           unsigned int iterations);

#ifdef WITH_UI
/* Load configuration from ~/.vtoprc if available. The delay and sort
 * parameters are updated with the loaded values. */
int ui_load_config(unsigned int *delay_ms, enum sort_field *sort);

/* Save the current configuration to ~/.vtoprc. */
int ui_save_config(unsigned int delay_ms, enum sort_field sort);
#endif

#endif /* UI_H */
