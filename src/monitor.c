#include "monitor.h"
#include "scanner.h"
#include "hash_table.h"
#include "file_ops.h"
#include "empty_files.h"
#include "utils.h"
#include <stdio.h>
#include <wchar.h>
#include <windows.h>

// Global stop event for clean termination
static HANDLE g_stop_event = NULL;
static CRITICAL_SECTION g_stop_event_lock;
static BOOL g_monitor_initialized = FALSE;

DWORD WINAPI monitor_thread_func(LPVOID lpParam) {
    const char *dir_path = (const char*)lpParam;
    
    // Initialize stop event lock
    InitializeCriticalSection(&g_stop_event_lock);
    
    // Create stop event
    EnterCriticalSection(&g_stop_event_lock);
    g_stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_monitor_initialized = TRUE;
    LeaveCriticalSection(&g_stop_event_lock);
    
    HANDLE hDir = CreateFile(
        dir_path,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );
    
    if (hDir == INVALID_HANDLE_VALUE) {
        safe_printf("Failed to open directory for monitoring: %s\n", dir_path);
        EnterCriticalSection(&g_stop_event_lock);
        if (g_stop_event) {
            CloseHandle(g_stop_event);
            g_stop_event = NULL;
        }
        g_monitor_initialized = FALSE;
        LeaveCriticalSection(&g_stop_event_lock);
        DeleteCriticalSection(&g_stop_event_lock);
        return 1;
    }
    
    safe_printf("\n=== File System Monitor Started ===\n");
    safe_printf("Watching for changes during scan and after...\n\n");
    
    char buffer[4096];
    DWORD bytes_returned;
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    BOOL pending_read = FALSE;
    
    while (!g_stop_monitoring) {
        if (!pending_read) {
            ResetEvent(overlapped.hEvent);
            
            // Start async read
            BOOL result = ReadDirectoryChangesW(
                hDir,
                buffer,
                sizeof(buffer),
                TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | 
                FILE_NOTIFY_CHANGE_LAST_WRITE,
                &bytes_returned,
                &overlapped,
                NULL
            );
            
            if (!result) {
                DWORD error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    safe_printf("ReadDirectoryChangesW failed: %lu\n", error);
                    break;
                }
                pending_read = TRUE;
            }
        }
        
        // Wait for either file system change or stop signal
        HANDLE handles[2] = { overlapped.hEvent, g_stop_event };
        DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, 500);
        
        if (waitResult == WAIT_OBJECT_0 + 1 || g_stop_monitoring) {
            // Stop event signaled - cancel pending operation
            if (pending_read) {
                CancelIo(hDir);
                // Wait briefly for cancellation to complete
                WaitForSingleObject(overlapped.hEvent, 100);
            }
            break;
        }
        
        if (waitResult == WAIT_OBJECT_0) {
            // File system change detected
            pending_read = FALSE;
            
            if (!GetOverlappedResult(hDir, &overlapped, &bytes_returned, FALSE)) {
                DWORD error = GetLastError();
                if (error == ERROR_OPERATION_ABORTED) {
                    break; // Clean shutdown
                }
                continue;
            }
            
            if (bytes_returned == 0) {
                continue;
            }
            
            FILE_NOTIFY_INFORMATION *fni = (FILE_NOTIFY_INFORMATION*)buffer;
            
            do {
                WCHAR filename[MAX_PATH];
                wcsncpy(filename, fni->FileName, 
                       fni->FileNameLength / sizeof(WCHAR));
                filename[fni->FileNameLength / sizeof(WCHAR)] = L'\0';
                
                char full_path[MAX_PATH];
                char filename_utf8[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0, filename, -1, 
                                   filename_utf8, MAX_PATH, NULL, NULL);
                snprintf(full_path, MAX_PATH, "%s\\%s", dir_path, filename_utf8);
                
                if (should_ignore_file(filename_utf8)) {
                    if (fni->NextEntryOffset == 0) break;
                    fni = (FILE_NOTIFY_INFORMATION*)((char*)fni + fni->NextEntryOffset);
                    continue;
                }
                
                switch (fni->Action) {
                    case FILE_ACTION_ADDED: {
                        DWORD attrs = GetFileAttributes(full_path);
                        if (attrs != INVALID_FILE_ATTRIBUTES && 
                            !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                            Sleep(100);
                            process_file(full_path, "ADDED");
                        }
                        break;
                    }
                    
                    case FILE_ACTION_MODIFIED: {
                        DWORD attrs = GetFileAttributes(full_path);
                        if (attrs != INVALID_FILE_ATTRIBUTES && 
                            !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                            Sleep(100);
                            safe_printf("[MODIFIED] %s - Reprocessing...\n", full_path);
                            remove_file_from_table(g_hash_table, full_path);
                            remove_empty_file(full_path);
                            process_file(full_path, "MODIFIED");
                        }
                        break;
                    }
                    
                    case FILE_ACTION_REMOVED:
                        safe_printf("[DELETED] %s\n", full_path);
                        remove_file_from_table(g_hash_table, full_path);
                        remove_empty_file(full_path);
                        break;
                        
                    case FILE_ACTION_RENAMED_OLD_NAME:
                        safe_printf("[RENAMED FROM] %s\n", full_path);
                        remove_file_from_table(g_hash_table, full_path);
                        break;
                        
                    case FILE_ACTION_RENAMED_NEW_NAME: {
                        DWORD attrs = GetFileAttributes(full_path);
                        if (attrs != INVALID_FILE_ATTRIBUTES && 
                            !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                            process_file(full_path, "RENAMED TO");
                        }
                        break;
                    }
                }
                
                if (fni->NextEntryOffset == 0) break;
                fni = (FILE_NOTIFY_INFORMATION*)((char*)fni + fni->NextEntryOffset);
            } while (1);
        }
    }
    
    // Cleanup
    if (pending_read) {
        CancelIo(hDir);
        WaitForSingleObject(overlapped.hEvent, 500);
    }
    
    CloseHandle(overlapped.hEvent);
    CloseHandle(hDir);
    
    EnterCriticalSection(&g_stop_event_lock);
    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
    }
    g_monitor_initialized = FALSE;
    LeaveCriticalSection(&g_stop_event_lock);
    DeleteCriticalSection(&g_stop_event_lock);
    
    safe_printf("\n=== File System Monitor Stopped ===\n");
    return 0;
}

// Function to signal monitor thread to stop
void signal_monitor_stop(void) {
    EnterCriticalSection(&g_stop_event_lock);
    if (g_stop_event && g_monitor_initialized) {
        SetEvent(g_stop_event);
    }
    LeaveCriticalSection(&g_stop_event_lock);
}