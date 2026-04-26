//monitor.c
#include "monitor.h"
#include "scanner.h"
#include "hash_table.h"
#include "file_ops.h"
#include "empty_files.h"
#include "ipc_pipe.h"
#include "utils.h"
#include <stdio.h>
#include <wchar.h>
#include <windows.h>

// Global stop event for clean termination
static HANDLE g_stop_event = NULL;
static CRITICAL_SECTION g_stop_event_lock;
static BOOL g_monitor_initialized = FALSE;

// Helper function to count files in a directory (non-recursive)
static int count_files_in_directory(const char *dir_path) {
    WIN32_FIND_DATA find_data;
    HANDLE hFind;
    char search_path[MAX_PATH];
    int count = 0;
    
    snprintf(search_path, MAX_PATH, "%s\\*", dir_path);
    
    hFind = FindFirstFile(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return 0;
    }
    
    do {
        if (strcmp(find_data.cFileName, ".") == 0 || 
            strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }
        count++;
    } while (FindNextFile(hFind, &find_data));
    
    FindClose(hFind);
    return count;
}

// Wait for directory copy operation to complete by monitoring file count stability
static BOOL wait_for_directory_stable(const char *dir_path, int max_wait_seconds) {
    int prev_count = -1;
    int stable_count = 0;
    int checks = 0;
    int max_checks = max_wait_seconds * 10; // Check every 100ms
    
    while (checks < max_checks) {
        int current_count = count_files_in_directory(dir_path);
        
        if (current_count == prev_count && current_count > 0) {
            stable_count++;
            // If count is stable for 3 consecutive checks (300ms), consider it done
            if (stable_count >= 3) {
                return TRUE;
            }
        } else {
            stable_count = 0;
        }
        
        prev_count = current_count;
        Sleep(100);
        checks++;
        
        // Stop if monitoring is cancelled
        if (g_stop_monitoring) {
            return FALSE;
        }
    }
    
    // Timeout - proceed anyway
    return TRUE;
}

// Helper function to recursively scan a newly created/copied directory
static void scan_new_directory(const char *dir_path) {
    WIN32_FIND_DATA find_data;
    HANDLE hFind;
    char search_path[MAX_PATH];
    
    snprintf(search_path, MAX_PATH, "%s\\*", dir_path);
    
    hFind = FindFirstFile(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }
    
    do {
        if (strcmp(find_data.cFileName, ".") == 0 || 
            strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }
        
        char full_path[MAX_PATH];
        snprintf(full_path, MAX_PATH, "%s\\%s", dir_path, find_data.cFileName);
        
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recursively scan subdirectories
            scan_new_directory(full_path);
        } else {
            // Process file
            if (!should_ignore_file(find_data.cFileName)) {
                process_file(full_path, "ADDED");
            }
        }
    } while (FindNextFile(hFind, &find_data));
    
    FindClose(hFind);
}

static void scan_for_new_files_in_dir(const char *dir_path) {
    WIN32_FIND_DATA find_data;
    char search_path[MAX_PATH];
    snprintf(search_path, MAX_PATH, "%s\\*", dir_path);

    HANDLE hFind = FindFirstFile(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(find_data.cFileName, ".") == 0 ||
            strcmp(find_data.cFileName, "..") == 0) continue;
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (should_ignore_file(find_data.cFileName)) continue;

        char full_path[MAX_PATH];
        snprintf(full_path, MAX_PATH, "%s\\%s", dir_path, find_data.cFileName);

        // Only process files not already in the hash table — these are the
        // rename targets that Windows never sent a RENAMED_NEW event for.
        if (!filepath_in_hash_table(g_hash_table, full_path)) {
            Sleep(50);
            process_file(full_path, "RENAMED TO");
        }
    } while (FindNextFile(hFind, &find_data));

    FindClose(hFind);
}

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
    
    // 65536 bytes — large enough to absorb bursts of events from indexers/AV
    // without triggering ERROR_NOTIFY_ENUM_DIR (buffer-overflow) drops.
    // Declared static so it lives in the data segment, not the stack.
    static char buffer[65536];
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
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | 
                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
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
                if (error == ERROR_NOTIFY_ENUM_DIR) {
                    // Buffer overflowed — Windows discarded all queued events.
                    // This is the primary cause of missed rename events in deep
                    // directory trees when indexers/AV generate concurrent events.
                    safe_printf("[WARNING] Monitor buffer overflow (ERROR_NOTIFY_ENUM_DIR) - some file events were lost\n");
                }
                continue;
            }
            
            if (bytes_returned == 0) {
                continue;
            }
            
            FILE_NOTIFY_INFORMATION *fni;

            // TWO-PASS processing of the event batch.
            //
            // For nested-directory renames Windows reports events in this order
            // within a single batch:
            //   action=3 (MODIFIED) on the parent directory
            //   action=3 (MODIFIED) on the old filename (which no longer exists)
            //
            // If we process them top-to-bottom, the directory-scan (pass 1) adds
            // the new filename to the IPC group while the old name is still in
            // files[0] as the trigger. send_duplicate_group fires with the stale
            // trigger, and the GUI shows "no trigger / all duplicates" even after
            // the old name is later removed.
            //
            // Fix: Run PASS 1 (removals only) across the entire batch first so
            // stale paths are evicted and files[0] is already promoted before
            // PASS 2 (additions) calls scan_for_new_files_in_dir / process_file.

            // ── PASS 1: removals ─────────────────────────────────────────────
            fni = (FILE_NOTIFY_INFORMATION*)buffer;
            do {
                WCHAR filename_w[MAX_PATH];
                wcsncpy(filename_w, fni->FileName,
                        fni->FileNameLength / sizeof(WCHAR));
                filename_w[fni->FileNameLength / sizeof(WCHAR)] = L'\0';

                char full_path[MAX_PATH];
                char filename_utf8[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0, filename_w, -1,
                                   filename_utf8, MAX_PATH, NULL, NULL);
                snprintf(full_path, MAX_PATH, "%s\\%s", dir_path, filename_utf8);

                const char *sep = strrchr(filename_utf8, '\\');
                const char *fname_only = sep ? sep + 1 : filename_utf8;

                if (!should_ignore_file(fname_only)) {
                    switch (fni->Action) {
                        case FILE_ACTION_RENAMED_OLD_NAME:
                            safe_printf("[RENAMED FROM] %s\n", full_path);
                            remove_file_from_table(g_hash_table, full_path);
                            remove_empty_file(full_path);
                            remove_filepath_from_ipc_groups(full_path);
                            break;

                        case FILE_ACTION_REMOVED: {
                            DWORD attrs = GetFileAttributes(full_path);
                            if (attrs == INVALID_FILE_ATTRIBUTES) {
                                safe_printf("[DELETED] %s\n", full_path);
                                remove_file_from_table(g_hash_table, full_path);
                                remove_empty_file(full_path);
                                remove_filepath_from_ipc_groups(full_path);
                            }
                            break;
                        }

                        case FILE_ACTION_MODIFIED: {
                            // Only handle the "nonexistent file" sub-case here
                            // (old name of a nested rename).  Directory and
                            // real-file modifications are handled in pass 2.
                            DWORD attrs = GetFileAttributes(full_path);
                            if (attrs == INVALID_FILE_ATTRIBUTES) {
                                safe_printf("[RENAMED FROM] %s\n", full_path);
                                remove_file_from_table(g_hash_table, full_path);
                                remove_empty_file(full_path);
                                remove_filepath_from_ipc_groups(full_path);
                            }
                            break;
                        }

                        default:
                            break;
                    }
                }

                if (fni->NextEntryOffset == 0) break;
                fni = (FILE_NOTIFY_INFORMATION*)((char*)fni + fni->NextEntryOffset);
            } while (1);

            // ── PASS 2: additions / modifications ────────────────────────────
            fni = (FILE_NOTIFY_INFORMATION*)buffer;
            do {
                WCHAR filename_w[MAX_PATH];
                wcsncpy(filename_w, fni->FileName,
                        fni->FileNameLength / sizeof(WCHAR));
                filename_w[fni->FileNameLength / sizeof(WCHAR)] = L'\0';

                char full_path[MAX_PATH];
                char filename_utf8[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0, filename_w, -1,
                                   filename_utf8, MAX_PATH, NULL, NULL);
                snprintf(full_path, MAX_PATH, "%s\\%s", dir_path, filename_utf8);

                const char *sep = strrchr(filename_utf8, '\\');
                const char *fname_only = sep ? sep + 1 : filename_utf8;

                if (!should_ignore_file(fname_only)) {
                    switch (fni->Action) {
                        case FILE_ACTION_ADDED: {
                            DWORD attrs = GetFileAttributes(full_path);
                            if (attrs != INVALID_FILE_ATTRIBUTES) {
                                if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
                                    safe_printf("[DIRECTORY ADDED] %s - Waiting for copy to complete...\n", full_path);
                                    if (wait_for_directory_stable(full_path, 60)) {
                                        safe_printf("[DIRECTORY STABLE] %s - Scanning contents...\n", full_path);
                                        scan_new_directory(full_path);
                                    } else {
                                        safe_printf("[DIRECTORY TIMEOUT] %s - Scanning anyway...\n", full_path);
                                        scan_new_directory(full_path);
                                    }
                                } else {
                                    Sleep(100);
                                    process_file(full_path, "ADDED");
                                }
                            }
                            break;
                        }

                        case FILE_ACTION_MODIFIED: {
                            DWORD attrs = GetFileAttributes(full_path);
                            if (attrs == INVALID_FILE_ATTRIBUTES) {
                                // Already handled in pass 1 — skip.
                            } else if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
                                // Directory MODIFIED: a nested file was renamed.
                                // The old name was already removed in pass 1, so
                                // files[0] is already the correct promoted trigger.
                                Sleep(100);
                                scan_for_new_files_in_dir(full_path);
                            } else {
                                Sleep(100);
                                safe_printf("[MODIFIED] %s - Reprocessing...\n", full_path);
                                remove_file_from_table(g_hash_table, full_path);
                                remove_empty_file(full_path);
                                process_file(full_path, "MODIFIED");
                            }
                            break;
                        }

                        case FILE_ACTION_RENAMED_NEW_NAME: {
                            DWORD attrs = GetFileAttributes(full_path);
                            if (attrs != INVALID_FILE_ATTRIBUTES) {
                                if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
                                    safe_printf("[DIRECTORY RENAMED TO] %s - Waiting for stability...\n", full_path);
                                    if (wait_for_directory_stable(full_path, 60)) {
                                        safe_printf("[DIRECTORY STABLE] %s - Scanning contents...\n", full_path);
                                        scan_new_directory(full_path);
                                    } else {
                                        safe_printf("[DIRECTORY TIMEOUT] %s - Scanning anyway...\n", full_path);
                                        scan_new_directory(full_path);
                                    }
                                } else {
                                    Sleep(100);
                                    process_file(full_path, "RENAMED TO");
                                }
                            }
                            break;
                        }

                        default:
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