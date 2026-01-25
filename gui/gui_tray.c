// gui_tray.c - DDAS Tray Application with Group-Based Duplicate Tracking
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")

#define WM_TRAYICON (WM_USER + 1)
#define WM_PIPE_MESSAGE (WM_USER + 2)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_SHOW_WINDOW 1002
#define ID_TRAY_ABOUT 1003

#define PIPE_NAME "\\\\.\\pipe\\ddas_ipc"
#define MAX_DUPLICATES 100
#define MAX_ALERTS 100

typedef struct {
    char filepath[MAX_PATH];
    char filename[MAX_PATH];
    char filehash[65];
    unsigned long long filesize;
    char last_modified[32];
    unsigned long long file_index;
    BOOL selected;
} FileInfo;

typedef struct {
    char filehash[65];  // Group identifier
    FileInfo trigger_file;
    FileInfo duplicates[MAX_DUPLICATES];
    int duplicate_count;
    char timestamp[32];
    int files_remaining;  // Count of files that still exist
} DuplicateAlert;

// Global variables
HWND g_hMainWnd = NULL;
HWND g_hReportWnd = NULL;
NOTIFYICONDATA g_nid = {0};
HANDLE g_hPipeThread = NULL;
HANDLE g_hPipe = INVALID_HANDLE_VALUE;
volatile BOOL g_running = TRUE;

// Alerts storage (group-based)
DuplicateAlert g_alerts[MAX_ALERTS];
int g_alert_count = 0;
int g_current_alert_index = 0;
CRITICAL_SECTION g_alert_lock;

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ReportWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
DWORD WINAPI PipeReaderThread(LPVOID param);
void ShowTrayNotification(const char *title, const char *message);
void ShowReportWindow(void);
void ParseAlertJSON(const char *json);
BOOL FileExists(const char *filepath);
void UpdateReportWindow(void);
int CountRemainingFiles(DuplicateAlert *alert);
int FindNextValidGroup(int current_index, int direction);

// Check if file exists
BOOL FileExists(const char *filepath) {
    DWORD attrs = GetFileAttributes(filepath);
    return (attrs != INVALID_FILE_ATTRIBUTES);
}

// Format file size
void format_file_size(unsigned long long size, char *buffer, size_t buf_size) {
    if (size < 1024) {
        snprintf(buffer, buf_size, "%llu bytes", size);
    } else if (size < 1024 * 1024) {
        snprintf(buffer, buf_size, "%.2f KB", size / 1024.0);
    } else if (size < 1024 * 1024 * 1024) {
        snprintf(buffer, buf_size, "%.2f MB", size / (1024.0 * 1024.0));
    } else {
        snprintf(buffer, buf_size, "%.2f GB", size / (1024.0 * 1024.0 * 1024.0));
    }
}

// Count how many files still exist in this group
int CountRemainingFiles(DuplicateAlert *alert) {
    int count = 0;
    
    if (FileExists(alert->trigger_file.filepath)) {
        count++;
    }
    
    for (int i = 0; i < alert->duplicate_count; i++) {
        if (FileExists(alert->duplicates[i].filepath)) {
            count++;
        }
    }
    
    return count;
}

// Find next valid group (with at least 2 files remaining)
// direction: 1 = next, -1 = previous
int FindNextValidGroup(int current_index, int direction) {
    int index = current_index;
    int steps = 0;
    int max_steps = g_alert_count;  // Prevent infinite loop
    
    while (steps < max_steps) {
        index += direction;
        steps++;
        
        // Wrap around or stop at boundaries
        if (index < 0) {
            return current_index;  // Stay at current if no previous valid group
        }
        if (index >= g_alert_count) {
            return current_index;  // Stay at current if no next valid group
        }
        
        // Check if this group has at least 2 files remaining
        int remaining = CountRemainingFiles(&g_alerts[index]);
        if (remaining >= 2) {
            return index;
        }
    }
    
    return current_index;
}

// Find existing alert by hash or return -1
int find_alert_by_hash(const char *filehash) {
    for (int i = 0; i < g_alert_count; i++) {
        if (strcmp(g_alerts[i].filehash, filehash) == 0) {
            return i;
        }
    }
    return -1;
}

// Parse alert JSON and update groups
void ParseAlertJSON(const char *json) {
    EnterCriticalSection(&g_alert_lock);
    
    char filehash[65] = {0};
    
    // Parse filehash first to identify the group
    const char *hash_start = strstr(json, "\"filehash\":\"");
    if (hash_start) {
        hash_start += 12;
        const char *hash_end = strchr(hash_start, '"');
        if (hash_end) {
            size_t len = hash_end - hash_start;
            if (len < 65) {
                strncpy(filehash, hash_start, len);
                filehash[len] = '\0';
            }
        }
    }
    
    if (strlen(filehash) == 0) {
        LeaveCriticalSection(&g_alert_lock);
        return;  // Invalid JSON
    }
    
    // Find existing alert or create new one
    int alert_index = find_alert_by_hash(filehash);
    DuplicateAlert *alert;
    
    if (alert_index >= 0) {
        // Update existing alert
        alert = &g_alerts[alert_index];
        memset(alert, 0, sizeof(DuplicateAlert));  // Clear and rebuild
        strncpy(alert->filehash, filehash, 64);
    } else {
        // Create new alert
        if (g_alert_count >= MAX_ALERTS) {
            // Shift all alerts down by one (discard oldest)
            for (int i = 0; i < MAX_ALERTS - 1; i++) {
                g_alerts[i] = g_alerts[i + 1];
            }
            g_alert_count = MAX_ALERTS - 1;
            
            // Adjust current index if needed
            if (g_current_alert_index > 0) {
                g_current_alert_index--;
            }
        }
        
        alert = &g_alerts[g_alert_count];
        memset(alert, 0, sizeof(DuplicateAlert));
        strncpy(alert->filehash, filehash, 64);
        g_alert_count++;
        alert_index = g_alert_count - 1;
    }
    
    // Parse trigger file
    const char *trigger_start = strstr(json, "\"trigger_file\":");
    if (trigger_start) {
        const char *filepath = strstr(trigger_start, "\"filepath\":\"");
        if (filepath) {
            filepath += 12;
            const char *filepath_end = strchr(filepath, '"');
            if (filepath_end) {
                size_t len = filepath_end - filepath;
                if (len < MAX_PATH) {
                    strncpy(alert->trigger_file.filepath, filepath, len);
                    alert->trigger_file.filepath[len] = '\0';
                    
                    const char *filename = strrchr(alert->trigger_file.filepath, '\\');
                    filename = filename ? filename + 1 : alert->trigger_file.filepath;
                    strncpy(alert->trigger_file.filename, filename, MAX_PATH - 1);
                }
            }
        }
        
        const char *filehash_field = strstr(trigger_start, "\"filehash\":\"");
        if (filehash_field && filehash_field < strstr(trigger_start, "},")) {
            filehash_field += 12;
            const char *hash_end = strchr(filehash_field, '"');
            if (hash_end) {
                size_t len = hash_end - filehash_field;
                if (len < 65) {
                    strncpy(alert->trigger_file.filehash, filehash_field, len);
                    alert->trigger_file.filehash[len] = '\0';
                }
            }
        }
        
        const char *filesize = strstr(trigger_start, "\"filesize\":");
        if (filesize && filesize < strstr(trigger_start, "},")) {
            sscanf(filesize + 11, "%llu", &alert->trigger_file.filesize);
        }
        
        const char *last_mod = strstr(trigger_start, "\"last_mod\":\"");
        if (last_mod && last_mod < strstr(trigger_start, "},")) {
            last_mod += 12;
            const char *mod_end = strchr(last_mod, '"');
            if (mod_end) {
                size_t len = mod_end - last_mod;
                if (len < 32) {
                    strncpy(alert->trigger_file.last_modified, last_mod, len);
                    alert->trigger_file.last_modified[len] = '\0';
                }
            }
        }
    }
    
    // Parse duplicates array
    const char *duplicates_start = strstr(json, "\"duplicates\":[");
    if (duplicates_start) {
        const char *dup = duplicates_start + 14;
        int count = 0;
        
        while (count < MAX_DUPLICATES) {
            dup = strstr(dup, "{\"filepath\":\"");
            if (!dup) break;
            
            // Make sure we're still in the duplicates array
            const char *array_end = strstr(duplicates_start, "],\"timestamp\"");
            if (!array_end || dup > array_end) break;
            
            dup += 13;
            const char *dup_end = strchr(dup, '"');
            if (dup_end) {
                size_t len = dup_end - dup;
                if (len < MAX_PATH) {
                    strncpy(alert->duplicates[count].filepath, dup, len);
                    alert->duplicates[count].filepath[len] = '\0';
                    
                    const char *filename = strrchr(alert->duplicates[count].filepath, '\\');
                    filename = filename ? filename + 1 : alert->duplicates[count].filepath;
                    strncpy(alert->duplicates[count].filename, filename, MAX_PATH - 1);
                }
            }
            
            const char *next_brace = strchr(dup, '}');
            if (!next_brace) break;
            
            const char *size_str = strstr(dup, "\"filesize\":");
            if (size_str && size_str < next_brace) {
                sscanf(size_str + 11, "%llu", &alert->duplicates[count].filesize);
            }
            
            const char *last_mod = strstr(dup, "\"last_mod\":\"");
            if (last_mod && last_mod < next_brace) {
                last_mod += 12;
                const char *mod_end = strchr(last_mod, '"');
                if (mod_end && mod_end < next_brace) {
                    size_t len = mod_end - last_mod;
                    if (len < 32) {
                        strncpy(alert->duplicates[count].last_modified, last_mod, len);
                        alert->duplicates[count].last_modified[len] = '\0';
                    }
                }
            }
            
            count++;
            dup = next_brace + 1;
        }
        
        alert->duplicate_count = count;
    }
    
    // Parse timestamp
    const char *timestamp_start = strstr(json, "\"timestamp\":\"");
    if (timestamp_start) {
        timestamp_start += 13;
        const char *timestamp_end = strchr(timestamp_start, '"');
        if (timestamp_end) {
            size_t len = timestamp_end - timestamp_start;
            if (len < 32) {
                strncpy(alert->timestamp, timestamp_start, len);
                alert->timestamp[len] = '\0';
            }
        }
    }
    
    // Count remaining files
    alert->files_remaining = CountRemainingFiles(alert);
    
    // Set current view to this alert (newest/updated)
    g_current_alert_index = alert_index;
    
    LeaveCriticalSection(&g_alert_lock);
}

// Pipe reader thread
DWORD WINAPI PipeReaderThread(LPVOID param) {
    (void)param;
    char buffer[65536];
    
    while (g_running) {
        g_hPipe = CreateFile(
            PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL
        );
        
        if (g_hPipe == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_PIPE_BUSY) {
                WaitNamedPipe(PIPE_NAME, 1000);
                continue;
            }
            Sleep(2000);
            continue;
        }
        
        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(g_hPipe, &mode, NULL, NULL);
        
        OVERLAPPED overlapped = {0};
        overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        while (g_running) {
            DWORD bytes_read;
            ResetEvent(overlapped.hEvent);
            
            BOOL success = ReadFile(g_hPipe, buffer, sizeof(buffer) - 1, &bytes_read, &overlapped);
            DWORD error = GetLastError();
            
            if (!success && error != ERROR_IO_PENDING) {
                break;
            }
            
            if (success && bytes_read > 0) {
                buffer[bytes_read] = '\0';
                
                if (strstr(buffer, "\"type\":\"ALERT\"") && strstr(buffer, "\"DUPLICATE_DETECTED\"")) {
                    ParseAlertJSON(buffer);
                    PostMessage(g_hMainWnd, WM_PIPE_MESSAGE, 0, 0);
                } else if (strstr(buffer, "\"SCAN_COMPLETE\"")) {
                    PostMessage(g_hMainWnd, WM_PIPE_MESSAGE, 1, 0);
                }
                
                continue;
            }
            
            DWORD wait_result = WaitForSingleObject(overlapped.hEvent, 1000);
            
            if (wait_result == WAIT_TIMEOUT) {
                CancelIo(g_hPipe);
                continue;
            }
            
            if (wait_result != WAIT_OBJECT_0) {
                break;
            }
            
            if (!GetOverlappedResult(g_hPipe, &overlapped, &bytes_read, FALSE)) {
                break;
            }
            
            if (bytes_read == 0) {
                break;
            }
            
            buffer[bytes_read] = '\0';
            
            if (strstr(buffer, "\"type\":\"ALERT\"") && strstr(buffer, "\"DUPLICATE_DETECTED\"")) {
                ParseAlertJSON(buffer);
                PostMessage(g_hMainWnd, WM_PIPE_MESSAGE, 0, 0);
            } else if (strstr(buffer, "\"SCAN_COMPLETE\"")) {
                PostMessage(g_hMainWnd, WM_PIPE_MESSAGE, 1, 0);
            }
        }
        
        CloseHandle(overlapped.hEvent);
        CloseHandle(g_hPipe);
        g_hPipe = INVALID_HANDLE_VALUE;
        
        if (!g_running) {
            break;
        }
        
        Sleep(1000);
    }
    
    return 0;
}

// Show tray notification
void ShowTrayNotification(const char *title, const char *message) {
    NOTIFYICONDATA nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = g_hMainWnd;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    nid.uTimeout = 5000;
    
    strncpy(nid.szInfoTitle, title, sizeof(nid.szInfoTitle) - 1);
    strncpy(nid.szInfo, message, sizeof(nid.szInfo) - 1);
    
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

// Update report window
void UpdateReportWindow(void) {
    if (!g_hReportWnd) return;
    
    HWND hListView = GetDlgItem(g_hReportWnd, 2001);
    ListView_DeleteAllItems(hListView);
    
    EnterCriticalSection(&g_alert_lock);
    
    if (g_alert_count == 0) {
        SetDlgItemText(g_hReportWnd, 2005, "No duplicate alerts");
        SetDlgItemText(g_hReportWnd, 2007, "");
        LeaveCriticalSection(&g_alert_lock);
        return;
    }
    
    // Find a valid group to display (with at least 2 files remaining)
    int valid_group_found = 0;
    int search_index = g_current_alert_index;
    
    for (int i = 0; i < g_alert_count; i++) {
        DuplicateAlert *alert = &g_alerts[search_index];
        int remaining = CountRemainingFiles(alert);
        
        if (remaining >= 2) {
            g_current_alert_index = search_index;
            valid_group_found = 1;
            break;
        }
        
        search_index = (search_index + 1) % g_alert_count;
    }
    
    if (!valid_group_found) {
        SetDlgItemText(g_hReportWnd, 2005, "All duplicate groups have been resolved");
        SetDlgItemText(g_hReportWnd, 2007, "");
        LeaveCriticalSection(&g_alert_lock);
        return;
    }
    
    DuplicateAlert *alert = &g_alerts[g_current_alert_index];
    
    // Count valid groups
    int valid_groups = 0;
    for (int i = 0; i < g_alert_count; i++) {
        if (CountRemainingFiles(&g_alerts[i]) >= 2) {
            valid_groups++;
        }
    }
    
    // Update navigation text
    char nav_text[150];
    snprintf(nav_text, sizeof(nav_text), "Group %d of %d (%d files remaining) - Hash: %.8s...", 
             g_current_alert_index + 1, g_alert_count, alert->files_remaining, alert->filehash);
    SetDlgItemText(g_hReportWnd, 2007, nav_text);
    
    BOOL trigger_exists = FileExists(alert->trigger_file.filepath);
    
    // Display trigger file
    if (trigger_exists) {
        char trigger_text[MAX_PATH + 100];
        char size_str[64];
        format_file_size(alert->trigger_file.filesize, size_str, sizeof(size_str));
        snprintf(trigger_text, sizeof(trigger_text), "%s (%s)", 
                alert->trigger_file.filepath, size_str);
        SetDlgItemText(g_hReportWnd, 2005, trigger_text);
    } else {
        char temp[MAX_PATH + 30];
        snprintf(temp, sizeof(temp), "[DELETED] %s", alert->trigger_file.filepath);
        SetDlgItemText(g_hReportWnd, 2005, temp);
    }
    
    // Add duplicate files to listview (only those that still exist)
    int valid_index = 0;
    for (int i = 0; i < alert->duplicate_count; i++) {
        if (FileExists(alert->duplicates[i].filepath)) {
            LVITEM lvi = {0};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = valid_index;
            lvi.iSubItem = 0;
            lvi.pszText = alert->duplicates[i].filepath;
            ListView_InsertItem(hListView, &lvi);
            
            char size_str[64];
            format_file_size(alert->duplicates[i].filesize, size_str, sizeof(size_str));
            ListView_SetItemText(hListView, valid_index, 1, size_str);
            
            ListView_SetItemText(hListView, valid_index, 2, "Duplicate");
            
            if (strlen(alert->duplicates[i].last_modified) > 0) {
                ListView_SetItemText(hListView, valid_index, 3, alert->duplicates[i].last_modified);
            } else {
                ListView_SetItemText(hListView, valid_index, 3, "Unknown");
            }
            
            valid_index++;
        }
    }
    
    if (valid_index == 0 && !trigger_exists) {
        SetDlgItemText(g_hReportWnd, 2005, "All files in this group have been deleted");
    }
    
    LeaveCriticalSection(&g_alert_lock);
}

// Report Window Procedure
LRESULT CALLBACK ReportWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HWND hList = CreateWindowEx(
                0, WC_LISTVIEW, "",
                WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                10, 70, 760, 380,
                hwnd, (HMENU)2001, GetModuleHandle(NULL), NULL
            );
            
            LVCOLUMN lvc = {0};
            lvc.mask = LVCF_TEXT | LVCF_WIDTH;
            
            lvc.pszText = "File Path";
            lvc.cx = 400;
            ListView_InsertColumn(hList, 0, &lvc);
            
            lvc.pszText = "Size";
            lvc.cx = 120;
            ListView_InsertColumn(hList, 1, &lvc);
            
            lvc.pszText = "Type";
            lvc.cx = 100;
            ListView_InsertColumn(hList, 2, &lvc);
            
            lvc.pszText = "Modified";
            lvc.cx = 140;
            ListView_InsertColumn(hList, 3, &lvc);
            
            CreateWindow("BUTTON", "Previous Group",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        10, 460, 120, 30, hwnd, (HMENU)2008, GetModuleHandle(NULL), NULL);
            
            CreateWindow("BUTTON", "Next Group",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        140, 460, 120, 30, hwnd, (HMENU)2009, GetModuleHandle(NULL), NULL);
            
            CreateWindow("BUTTON", "Open Location",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        280, 460, 150, 30, hwnd, (HMENU)2002, GetModuleHandle(NULL), NULL);
            
            CreateWindow("BUTTON", "Delete Selected",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        440, 460, 150, 30, hwnd, (HMENU)2003, GetModuleHandle(NULL), NULL);
            
            CreateWindow("BUTTON", "Refresh",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        600, 460, 80, 30, hwnd, (HMENU)2006, GetModuleHandle(NULL), NULL);
            
            CreateWindow("BUTTON", "Close",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        690, 460, 80, 30, hwnd, (HMENU)2004, GetModuleHandle(NULL), NULL);
            
            CreateWindow("STATIC", "Trigger File:",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        10, 10, 100, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
            
            CreateWindow("STATIC", "",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        120, 10, 650, 20, hwnd, (HMENU)2005, GetModuleHandle(NULL), NULL);
            
            CreateWindow("STATIC", "",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        10, 35, 760, 20, hwnd, (HMENU)2007, GetModuleHandle(NULL), NULL);
            
            CreateWindow("STATIC", "Duplicate Files (same content):",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        10, 50, 760, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
            
            PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(2006, 0), 0);
            break;
        }
        
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            
            switch (wmId) {
                case 2008: {  // Previous Group
                    EnterCriticalSection(&g_alert_lock);
                    int new_index = FindNextValidGroup(g_current_alert_index, -1);
                    g_current_alert_index = new_index;
                    LeaveCriticalSection(&g_alert_lock);
                    UpdateReportWindow();
                    break;
                }
                
                case 2009: {  // Next Group
                    EnterCriticalSection(&g_alert_lock);
                    int new_index = FindNextValidGroup(g_current_alert_index, 1);
                    g_current_alert_index = new_index;
                    LeaveCriticalSection(&g_alert_lock);
                    UpdateReportWindow();
                    break;
                }
                
                case 2006:  // Refresh
                    UpdateReportWindow();
                    break;
                
                case 2002: {  // Open File Location
                    HWND hListView = GetDlgItem(hwnd, 2001);
                    int selected = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    
                    if (selected != -1) {
                        char filepath[MAX_PATH];
                        ListView_GetItemText(hListView, selected, 0, filepath, MAX_PATH);
                        
                        if (FileExists(filepath)) {
                            char command[MAX_PATH + 50];
                            snprintf(command, sizeof(command), "/select,\"%s\"", filepath);
                            ShellExecute(NULL, "open", "explorer.exe", command, NULL, SW_SHOW);
                        } else {
                            MessageBox(hwnd, "File no longer exists!", "Error", MB_OK | MB_ICONERROR);
                            UpdateReportWindow();
                        }
                    } else {
                        MessageBox(hwnd, "Please select a file first.", "Info", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                }
                
                case 2003: {  // Delete Selected
                    HWND hListView = GetDlgItem(hwnd, 2001);
                    int selected = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    
                    if (selected != -1) {
                        char filepath[MAX_PATH];
                        ListView_GetItemText(hListView, selected, 0, filepath, MAX_PATH);
                        
                        if (!FileExists(filepath)) {
                            MessageBox(hwnd, "File has already been deleted.", "Info", MB_OK | MB_ICONINFORMATION);
                            UpdateReportWindow();
                            break;
                        }
                        
                        char msg[MAX_PATH + 100];
                        snprintf(msg, sizeof(msg), 
                                "Delete this file?\n\n%s\n\nThis will move it to Recycle Bin.",
                                filepath);
                        
                        if (MessageBox(hwnd, msg, "Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
                            char from_path[MAX_PATH + 2];
                            memset(from_path, 0, sizeof(from_path));
                            strncpy(from_path, filepath, MAX_PATH);
                            
                            SHFILEOPSTRUCT fileOp = {0};
                            fileOp.hwnd = hwnd;
                            fileOp.wFunc = FO_DELETE;
                            fileOp.pFrom = from_path;
                            fileOp.pTo = NULL;
                            fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
                            
                            int result = SHFileOperation(&fileOp);
                            
                            if (result == 0 && !fileOp.fAnyOperationsAborted) {
                                MessageBox(hwnd, "File moved to Recycle Bin.", "Success", MB_OK | MB_ICONINFORMATION);
                                UpdateReportWindow();
                            } else {
                                Sleep(100);
                                if (!FileExists(filepath)) {
                                    MessageBox(hwnd, "File was successfully deleted.", "Success", MB_OK | MB_ICONINFORMATION);
                                    UpdateReportWindow();
                                } else {
                                    char error_msg[256];
                                    snprintf(error_msg, sizeof(error_msg), 
                                            "Failed to delete file.\nError code: %d\n\nThe file may be locked or in use.", 
                                            result);
                                    MessageBox(hwnd, error_msg, "Error", MB_OK | MB_ICONERROR);
                                }
                            }
                        }
                    } else {
                        MessageBox(hwnd, "Please select a file first.", "Info", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                }
                
                case 2004:  // Close
                    DestroyWindow(hwnd);
                    g_hReportWnd = NULL;
                    break;
            }
            break;
        }
        
        case WM_CLOSE:
            DestroyWindow(hwnd);
            g_hReportWnd = NULL;
            return 0;
        
        case WM_DESTROY:
            g_hReportWnd = NULL;
            return 0;
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Show report window
void ShowReportWindow(void) {
    if (g_hReportWnd) {
        SetForegroundWindow(g_hReportWnd);
        UpdateReportWindow();
        return;
    }
    
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = ReportWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "DDASReportWindow";
    
    if (!GetClassInfoEx(GetModuleHandle(NULL), "DDASReportWindow", &wc)) {
        RegisterClassEx(&wc);
    }
    
    g_hReportWnd = CreateWindowEx(
        0, "DDASReportWindow", "DDAS - Duplicate File Groups",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 550,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );
    
    ShowWindow(g_hReportWnd, SW_SHOW);
    UpdateWindow(g_hReportWnd);
    SetForegroundWindow(g_hReportWnd);
}

// Main Window Procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g_nid.cbSize = sizeof(NOTIFYICONDATA);
            g_nid.hWnd = hwnd;
            g_nid.uID = 1;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_nid.uCallbackMessage = WM_TRAYICON;
            g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            strcpy(g_nid.szTip, "DDAS - Duplicate Detector");
            Shell_NotifyIcon(NIM_ADD, &g_nid);
            break;
        
        case WM_PIPE_MESSAGE:
            if (wParam == 0) {
                // Duplicate detected
                EnterCriticalSection(&g_alert_lock);
                if (g_alert_count > 0) {
                    DuplicateAlert *alert = &g_alerts[g_current_alert_index];
                    char notification[512];
                    snprintf(notification, sizeof(notification), 
                            "Duplicate group updated: %s\n%d file(s) with same content",
                            alert->trigger_file.filename,
                            alert->files_remaining);
                    LeaveCriticalSection(&g_alert_lock);
                    
                    ShowTrayNotification("DDAS - Duplicate Group", notification);
                    
                    if (g_hReportWnd) {
                        UpdateReportWindow();
                    }
                } else {
                    LeaveCriticalSection(&g_alert_lock);
                }
            } else if (wParam == 1) {
                // Scan complete
                EnterCriticalSection(&g_alert_lock);
                int count = g_alert_count;
                LeaveCriticalSection(&g_alert_lock);
                
                char notification[256];
                snprintf(notification, sizeof(notification), 
                        "Initial scan complete. Found %d duplicate group(s).", count);
                ShowTrayNotification("DDAS", notification);
            }
            break;
        
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK) {
                EnterCriticalSection(&g_alert_lock);
                int has_alerts = (g_alert_count > 0);
                LeaveCriticalSection(&g_alert_lock);
                
                if (has_alerts) {
                    ShowReportWindow();
                }
            } else if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                
                HMENU hMenu = CreatePopupMenu();
                AppendMenu(hMenu, MF_STRING, ID_TRAY_SHOW_WINDOW, "Show Alerts");
                AppendMenu(hMenu, MF_STRING, ID_TRAY_ABOUT, "About");
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit");
                
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, 
                              pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            break;
        
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_SHOW_WINDOW:
                    EnterCriticalSection(&g_alert_lock);
                    if (g_alert_count > 0) {
                        LeaveCriticalSection(&g_alert_lock);
                        ShowReportWindow();
                    } else {
                        LeaveCriticalSection(&g_alert_lock);
                        MessageBox(NULL, "No duplicates detected yet.", "DDAS", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                
                case ID_TRAY_ABOUT:
                    MessageBox(NULL, 
                              "DDAS - Duplicate Detection & Alert System\n"
                              "Version 2.0 - Group-Based Tracking\n\n"
                              "Tracks duplicate file groups.\n"
                              "Updates groups when new duplicates are detected.",
                              "About DDAS", MB_OK | MB_ICONINFORMATION);
                    break;
                
                case ID_TRAY_EXIT:
                    PostQuitMessage(0);
                    break;
            }
            break;
        
        case WM_DESTROY:
            Shell_NotifyIcon(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            break;
        
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    
    return 0;
}

// WinMain entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    InitializeCriticalSection(&g_alert_lock);
    InitCommonControls();
    
    memset(g_alerts, 0, sizeof(g_alerts));
    g_alert_count = 0;
    g_current_alert_index = 0;
    
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DDASTrayClass";
    
    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, "Window Registration Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    g_hMainWnd = CreateWindowEx(
        0, "DDASTrayClass", "DDAS Tray",
        0, 0, 0, 0, 0,
        NULL, NULL, hInstance, NULL
    );
    
    if (!g_hMainWnd) {
        MessageBox(NULL, "Window Creation Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    g_hPipeThread = CreateThread(NULL, 0, PipeReaderThread, NULL, 0, NULL);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    g_running = FALSE;
    if (g_hPipeThread) {
        WaitForSingleObject(g_hPipeThread, 2000);
        CloseHandle(g_hPipeThread);
    }
    
    if (g_hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hPipe);
    }
    
    DeleteCriticalSection(&g_alert_lock);
    
    return (int)msg.wParam;
}