//utils.c
#include "utils.h"
#include <stdio.h>
#include <stdarg.h>

CRITICAL_SECTION g_print_lock;

void safe_printf(const char *format, ...) {
    EnterCriticalSection(&g_print_lock);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
    LeaveCriticalSection(&g_print_lock);
}

void init_utils(void) {
    InitializeCriticalSection(&g_print_lock);
}

void cleanup_utils(void) {
    DeleteCriticalSection(&g_print_lock);
}