//main.c
#include "utils.h"
#include "hash_table.h"
#include "empty_files.h"
#include "scanner.h"
#include "monitor.h"
#include "ipc_pipe.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        safe_printf("\n\nStopping monitoring and IPC server...\n");
        g_stop_monitoring = 1;
        signal_monitor_stop();
        shutdown_pipe_server();
        Sleep(200);
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
    
    init_utils();
    
    safe_printf("=== File Duplicate Detector with Real-time Monitoring ===\n");
    safe_printf("Directory: %s\n", directory);
    safe_printf("Mode: %s\n\n", watch_mode ? "Scan + Watch" : "Scan Only");
    
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    
    // Initialize IPC pipe server
    if (!init_pipe_server()) {
        safe_printf("[WARNING] Failed to initialize IPC server. GUI alerts will not work.\n");
    }
    
    g_hash_table = create_hash_table(10007);
    init_empty_files_list();
    
    HANDLE hMonitorThread = CreateThread(NULL, 0, monitor_thread_func, 
                                         g_monitor_path, 0, NULL);
    if (!hMonitorThread) {
        safe_printf("Failed to create monitor thread\n");
        shutdown_pipe_server();
        return 1;
    }
    
    Sleep(200);
    
    HANDLE hScannerThread = CreateThread(NULL, 0, scanner_thread_func, 
                                         NULL, 0, NULL);
    if (!hScannerThread) {
        safe_printf("Failed to create scanner thread\n");
        g_stop_monitoring = 1;
        WaitForSingleObject(hMonitorThread, INFINITE);
        shutdown_pipe_server();
        return 1;
    }
    
    WaitForSingleObject(hScannerThread, INFINITE);
    CloseHandle(hScannerThread);
    
    if (watch_mode) {
        safe_printf("\n=== Continuing to monitor (Press Ctrl+C to stop) ===\n\n");
        WaitForSingleObject(hMonitorThread, INFINITE);
    } else {
        g_stop_monitoring = 1;
        signal_monitor_stop();
        WaitForSingleObject(hMonitorThread, INFINITE);
    }
    
    CloseHandle(hMonitorThread);
    
    Sleep(100);
    
    // Cleanup IPC server
    shutdown_pipe_server();
    
    free_hash_table(g_hash_table);
    g_hash_table = NULL;
    
    free_empty_files_list();
    
    cleanup_utils();
    
    safe_printf("\nProgram terminated.\n");
    return 0;
}