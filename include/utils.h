#ifndef UTILS_H
#define UTILS_H

#include <windows.h>

// MinGW compatibility
#ifndef _MSC_VER
    #define _strdup strdup
#endif

// Global critical section for thread-safe printing
extern CRITICAL_SECTION g_print_lock;

// Thread-safe printf wrapper
void safe_printf(const char *format, ...);

// Initialize utils
void init_utils(void);

// Cleanup utils
void cleanup_utils(void);

#endif // UTILS_H