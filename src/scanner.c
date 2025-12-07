#include "scanner.h"
#include "file_ops.h"
#include "empty_files.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

volatile int g_scanning_complete = 0;
volatile int g_stop_monitoring = 0;
char g_monitor_path[MAX_PATH];

void scan_directory(const char *dir_path, HashTable *table, int *file_count) {
    WIN32_FIND_DATA find_data;
    HANDLE hFind;
    char search_path[MAX_PATH];
    
    snprintf(search_path, MAX_PATH, "%s\\*", dir_path);
    
    hFind = FindFirstFile(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }
    
    do {
        if (g_stop_monitoring) break;
        
        if (strcmp(find_data.cFileName, ".") == 0 || 
            strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }
        
        char full_path[MAX_PATH];
        snprintf(full_path, MAX_PATH, "%s\\%s", dir_path, find_data.cFileName);
        
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_directory(full_path, table, file_count);
        } else {
            if (should_ignore_file(find_data.cFileName)) {
                safe_printf("[SKIP] %s\n", full_path);
                continue;
            }
            
            process_file(full_path, "SCAN");
            (*file_count)++;
        }
    } while (FindNextFile(hFind, &find_data));
    
    FindClose(hFind);
}

DWORD WINAPI scanner_thread_func(LPVOID lpParam) {
    int file_count = 0;
    scan_directory(g_monitor_path, g_hash_table, &file_count);
    
    safe_printf("\n=== Initial Scan Complete ===\n");
    safe_printf("Processed %d files.\n", file_count);
    
    g_scanning_complete = 1;
    
    find_duplicates(g_hash_table);
    print_empty_files();
    
    return 0;
}