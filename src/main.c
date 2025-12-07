/* 
 * File Duplicate Detector using BLAKE3 hashing
 * Main entry point
 */

#include "utils.h"
#include "hash_table.h"
#include "empty_files.h"
#include "scanner.h"
#include "monitor.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

// Ctrl+C handler
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        safe_printf("\n\nStopping monitoring...\n");
        g_stop_monitoring = 1;
        signal_monitor_stop();  // Signal the monitor thread immediately
        Sleep(200);  // Give threads time to stop gracefully
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        printf("Usage: %s <directory> [--watch]\n", argv[0]);
        printf(" --watch: Continue monitoring after initial scan\n");
        return 1;
    }
    
    const char *directory = argv[1];
    int watch_mode = (argc == 3 && strcmp(argv[2], "--watch") == 0);
    
    strncpy(g_monitor_path, directory, MAX_PATH);
    
    // Initialize all modules
    init_utils();
    
    safe_printf("=== File Duplicate Detector with Real-time Monitoring ===\n");
    safe_printf("Directory: %s\n", directory);
    safe_printf("Mode: %s\n\n", watch_mode ? "Scan + Watch" : "Scan Only");
    
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    
    g_hash_table = create_hash_table(10007);
    init_empty_files_list();
    
    // Start monitor thread
    HANDLE hMonitorThread = CreateThread(NULL, 0, monitor_thread_func, 
                                         g_monitor_path, 0, NULL);
    if (!hMonitorThread) {
        safe_printf("Failed to create monitor thread\n");
        return 1;
    }
    
    // Give monitor thread time to start
    Sleep(200);
    
    // Start scanner thread
    HANDLE hScannerThread = CreateThread(NULL, 0, scanner_thread_func, 
                                         NULL, 0, NULL);
    if (!hScannerThread) {
        safe_printf("Failed to create scanner thread\n");
        g_stop_monitoring = 1;
        WaitForSingleObject(hMonitorThread, INFINITE);
        return 1;
    }
    
    // Wait for scanner to complete
    WaitForSingleObject(hScannerThread, INFINITE);
    CloseHandle(hScannerThread);
    
    if (watch_mode) {
        safe_printf("\n=== Continuing to monitor (Press Ctrl+C to stop) ===\n\n");
        WaitForSingleObject(hMonitorThread, INFINITE);
    } else {
        g_stop_monitoring = 1;
        signal_monitor_stop();  // Signal monitor to stop immediately
        WaitForSingleObject(hMonitorThread, INFINITE);
    }
    
    CloseHandle(hMonitorThread);
    
    // Small delay to ensure threads are fully cleaned up
    Sleep(100);
    
    // Cleanup in reverse order of initialization
    free_hash_table(g_hash_table);
    g_hash_table = NULL;
    
    free_empty_files_list();
    
    cleanup_utils();
    
    safe_printf("\nProgram terminated.\n");
    return 0;
}