#include "ui.h"
#include <ctype.h>

enum mem_unit summary_unit = MEM_UNIT_K;
enum mem_unit proc_unit = MEM_UNIT_K;

static double factor(enum mem_unit u) {
    switch (u) {
    case MEM_UNIT_M: return 1024.0;
    case MEM_UNIT_G: return 1024.0 * 1024.0;
    case MEM_UNIT_T: return 1024.0 * 1024.0 * 1024.0;
    case MEM_UNIT_P: return 1024.0 * 1024.0 * 1024.0 * 1024.0;
    case MEM_UNIT_E: return 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0;
    case MEM_UNIT_K:
    default:
        return 1.0;
    }
}

double scale_kb(unsigned long long kb, enum mem_unit unit) {
    return (double)kb / factor(unit);
}

const char *mem_unit_suffix(enum mem_unit u) {
    switch (u) {
    case MEM_UNIT_M: return "M";
    case MEM_UNIT_G: return "G";
    case MEM_UNIT_T: return "T";
    case MEM_UNIT_P: return "P";
    case MEM_UNIT_E: return "E";
    case MEM_UNIT_K:
    default:
        return "K";
    }
}

