#ifndef UI_H
#define UI_H

#include <stddef.h>

enum sort_field {
    SORT_PID,
    SORT_CPU,
    SORT_MEM,
    SORT_USER,
    SORT_START,
    SORT_TIME,
    SORT_PRI
};

int run_ui(unsigned int delay_ms, enum sort_field sort,
           unsigned int iterations, int columns, size_t max_entries);

enum mem_unit {
    MEM_UNIT_K,
    MEM_UNIT_M,
    MEM_UNIT_G,
    MEM_UNIT_T,
    MEM_UNIT_P,
    MEM_UNIT_E
};

extern enum mem_unit summary_unit;
extern enum mem_unit proc_unit;

double scale_kb(unsigned long long kb, enum mem_unit unit);
const char *mem_unit_suffix(enum mem_unit unit);
enum mem_unit next_mem_unit(enum mem_unit unit);

#ifdef WITH_UI
void ui_set_show_full_cmd(int on);
void ui_set_show_idle(int on);
void ui_set_show_cores(int on);
/* Load configuration from ~/.vtoprc if available. The delay and sort
 * parameters are updated with the loaded values. */
int ui_load_config(unsigned int *delay_ms, enum sort_field *sort);

/* Save the current configuration to ~/.vtoprc. */
int ui_save_config(unsigned int delay_ms, enum sort_field sort);

/* Print the available column titles, one per line. */
void ui_list_fields(void);
#endif

#endif /* UI_H */
