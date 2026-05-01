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

// One full pass: scan + optionally watch.  Returns TRUE if a directory change
// was requested while running so the caller can loop.
static BOOL run_scan_and_watch(const char *directory, int watch_mode) {
    strncpy(g_monitor_path, directory, MAX_PATH - 1);
    g_monitor_path[MAX_PATH - 1] = '\0';
    g_stop_monitoring   = 0;
    g_scanning_complete = 0;
    g_dir_change_pending = FALSE;

    safe_printf("=== File Duplicate Detector with Real-time Monitoring ===\n");
    safe_printf("Directory: %s\n", directory);
    safe_printf("Mode: %s\n\n", watch_mode ? "Scan + Watch" : "Scan Only");

    // Clear internal state so new scan is fresh
    clear_ipc_state();

    // Free and re-create hash table
    if (g_hash_table) {
        free_hash_table(g_hash_table);
        g_hash_table = NULL;
    }
    g_hash_table = create_hash_table(10007);

    // Re-initialise empty files list
    free_empty_files_list();
    init_empty_files_list();

    // Notify the GUI *before* starting the scanner so the UI clears its
    // state and shows "Scanning in progress..." while the scan runs.
    if (watch_mode) {
        char ts[32];
        get_iso8601_timestamp(ts, sizeof(ts));
        char msg[MAX_PATH + 256];
        snprintf(msg, sizeof(msg),
            "{\"type\":\"ALERT\",\"event\":\"DIRECTORY_CHANGED\","
            "\"path\":\"%s\",\"timestamp\":\"%s\"}\n",
            directory, ts);
        send_raw_notification(msg);
    }

    HANDLE hMonitorThread = CreateThread(NULL, 0, monitor_thread_func,
                                         g_monitor_path, 0, NULL);
    if (!hMonitorThread) {
        safe_printf("Failed to create monitor thread\n");
        return FALSE;
    }

    Sleep(200);

    HANDLE hScannerThread = CreateThread(NULL, 0, scanner_thread_func,
                                         NULL, 0, NULL);
    if (!hScannerThread) {
        safe_printf("Failed to create scanner thread\n");
        g_stop_monitoring = 1;
        WaitForSingleObject(hMonitorThread, INFINITE);
        CloseHandle(hMonitorThread);
        return FALSE;
    }

    WaitForSingleObject(hScannerThread, INFINITE);
    CloseHandle(hScannerThread);

    if (watch_mode) {
        safe_printf("\n=== Continuing to monitor (Press Ctrl+C to stop) ===\n\n");

        // Wait for monitor thread, but break early if directory change requested
        while (!g_dir_change_pending) {
            if (WaitForSingleObject(hMonitorThread, 500) == WAIT_OBJECT_0)
                break;
        }
    }

    g_stop_monitoring = 1;
    signal_monitor_stop();
    WaitForSingleObject(hMonitorThread, INFINITE);
    CloseHandle(hMonitorThread);

    BOOL changed = g_dir_change_pending;
    return changed;
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        printf("Usage: %s <directory> [--watch]\n", argv[0]);
        printf(" --watch: Continue monitoring after initial scan\n");
        return 1;
    }

    char directory[MAX_PATH];
    strncpy(directory, argv[1], MAX_PATH - 1);
    directory[MAX_PATH - 1] = '\0';
    int watch_mode = (argc == 3 && strcmp(argv[2], "--watch") == 0);

    init_utils();
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    // Initialize IPC pipe server once; it persists across directory changes
    if (!init_pipe_server()) {
        safe_printf("[WARNING] Failed to initialize IPC server. GUI alerts will not work.\n");
    }

    g_hash_table = NULL;   // run_scan_and_watch creates it

    // Main run loop – re-enters when user requests a directory change
    while (1) {
        BOOL dir_changed = run_scan_and_watch(directory, watch_mode);

        if (!dir_changed) break;   // normal exit or scan-only mode

        // Pick up the new directory from scanner globals
        strncpy(directory, (const char*)g_pending_dir, MAX_PATH - 1);
        directory[MAX_PATH - 1] = '\0';
        g_dir_change_pending = FALSE;

        safe_printf("[MAIN] Restarting with new directory: %s\n", directory);
    }

    // Cleanup
    shutdown_pipe_server();
    if (g_hash_table) {
        free_hash_table(g_hash_table);
        g_hash_table = NULL;
    }
    free_empty_files_list();
    cleanup_utils();

    safe_printf("\nProgram terminated.\n");
    return 0;
}