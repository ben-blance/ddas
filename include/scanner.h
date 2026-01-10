//scanner.h
#ifndef SCANNER_H
#define SCANNER_H

#include "hash_table.h"
#include <windows.h>

// Global variables for scanner
extern volatile int g_scanning_complete;
extern volatile int g_stop_monitoring;
extern char g_monitor_path[MAX_PATH];

// Recursively scan directory
void scan_directory(const char *dir_path, HashTable *table, int *file_count);

// Scanner thread function
DWORD WINAPI scanner_thread_func(LPVOID lpParam);

#endif // SCANNER_H