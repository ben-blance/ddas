// gui_tray.c - DDAS Tray Application
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
    FileInfo trigger_file;
    FileInfo duplicates[MAX_DUPLICATES];
    int duplicate_count;
    char timestamp[32];
} DuplicateAlert;

// Global variables
HWND g_hMainWnd = NULL;
HWND g_hReportWnd = NULL;
NOTIFYICONDATA g_nid = {0};
HANDLE g_hPipeThread = NULL;
HANDLE g_hPipe = INVALID_HANDLE_VALUE;
volatile BOOL g_running = TRUE;
DuplicateAlert g_current_alert = {0};
CRITICAL_SECTION g_alert_lock;

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ReportWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
DWORD WINAPI PipeReaderThread(LPVOID param);
void ShowTrayNotification(const char *title, const char *message);
void ShowReportWindow(void);
void ParseAlertJSON(const char *json);
BOOL FileExists(const char *filepath);

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

// Simple JSON parser for our specific format
void ParseAlertJSON(const char *json) {
    EnterCriticalSection(&g_alert_lock);
    
    memset(&g_current_alert, 0, sizeof(DuplicateAlert));
    
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
                    strncpy(g_current_alert.trigger_file.filepath, filepath, len);
                    g_current_alert.trigger_file.filepath[len] = '\0';
                    
                    // Extract filename from path
                    const char *filename = strrchr(g_current_alert.trigger_file.filepath, '\\');
                    filename = filename ? filename + 1 : g_current_alert.trigger_file.filepath;
                    strncpy(g_current_alert.trigger_file.filename, filename, MAX_PATH - 1);
                }
            }
        }
        
        const char *filehash = strstr(trigger_start, "\"filehash\":\"");
        if (filehash) {
            filehash += 12;
            const char *hash_end = strchr(filehash, '"');
            if (hash_end) {
                size_t len = hash_end - filehash;
                if (len < 65) {
                    strncpy(g_current_alert.trigger_file.filehash, filehash, len);
                    g_current_alert.trigger_file.filehash[len] = '\0';
                }
            }
        }
        
        const char *filesize = strstr(trigger_start, "\"filesize\":");
        if (filesize) {
            sscanf(filesize + 11, "%llu", &g_current_alert.trigger_file.filesize);
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
            
            dup += 13;
            const char *dup_end = strchr(dup, '"');
            if (dup_end) {
                size_t len = dup_end - dup;
                if (len < MAX_PATH) {
                    strncpy(g_current_alert.duplicates[count].filepath, dup, len);
                    g_current_alert.duplicates[count].filepath[len] = '\0';
                    
                    const char *filename = strrchr(g_current_alert.duplicates[count].filepath, '\\');
                    filename = filename ? filename + 1 : g_current_alert.duplicates[count].filepath;
                    strncpy(g_current_alert.duplicates[count].filename, filename, MAX_PATH - 1);
                }
            }
            
            const char *size_str = strstr(dup, "\"filesize\":");
            if (size_str) {
                sscanf(size_str + 11, "%llu", &g_current_alert.duplicates[count].filesize);
            }
            
            count++;
            dup = dup_end;
        }
        
        g_current_alert.duplicate_count = count;
    }
    
    LeaveCriticalSection(&g_alert_lock);
}

// Pipe reader thread - uses overlapped I/O for non-blocking reads
DWORD WINAPI PipeReaderThread(LPVOID param) {
    (void)param;
    char buffer[65536];
    
    while (g_running) {
        // Connect to pipe
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
        
        // Set message mode
        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(g_hPipe, &mode, NULL, NULL);
        
        // Create event for overlapped I/O
        OVERLAPPED overlapped = {0};
        overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        // Read messages with timeout
        while (g_running) {
            DWORD bytes_read;
            ResetEvent(overlapped.hEvent);
            
            // Start async read
            BOOL success = ReadFile(g_hPipe, buffer, sizeof(buffer) - 1, &bytes_read, &overlapped);
            DWORD error = GetLastError();
            
            if (!success && error != ERROR_IO_PENDING) {
                break;
            }
            
            // Wait for read to complete or timeout (100ms)
            DWORD wait_result = WaitForSingleObject(overlapped.hEvent, 100);
            
            if (wait_result == WAIT_TIMEOUT) {
                continue;
            }
            
            if (wait_result != WAIT_OBJECT_0) {
                break;
            }
            
            // Get result
            if (!GetOverlappedResult(g_hPipe, &overlapped, &bytes_read, FALSE)) {
                break;
            }
            
            if (bytes_read == 0) {
                break;
            }
            
            buffer[bytes_read] = '\0';
            
            // Check message type and post to main window
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

// Report Window Procedure
LRESULT CALLBACK ReportWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Create controls
            HWND hList = CreateWindowEx(
                0, WC_LISTVIEW, "",
                WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                10, 50, 760, 400,
                hwnd, (HMENU)2001, GetModuleHandle(NULL), NULL
            );
            
            // Setup columns
            LVCOLUMN lvc = {0};
            lvc.mask = LVCF_TEXT | LVCF_WIDTH;
            
            lvc.pszText = "File";
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
            
            // Create buttons
            CreateWindow("BUTTON", "Open File Location",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        10, 460, 150, 30, hwnd, (HMENU)2002, GetModuleHandle(NULL), NULL);
            
            CreateWindow("BUTTON", "Delete Selected",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        170, 460, 150, 30, hwnd, (HMENU)2003, GetModuleHandle(NULL), NULL);
            
            CreateWindow("BUTTON", "Refresh",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        330, 460, 100, 30, hwnd, (HMENU)2006, GetModuleHandle(NULL), NULL);
            
            CreateWindow("BUTTON", "Close",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        620, 460, 150, 30, hwnd, (HMENU)2004, GetModuleHandle(NULL), NULL);
            
            // Static text for info
            CreateWindow("STATIC", "Trigger File:",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        10, 10, 100, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
            
            CreateWindow("STATIC", "",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        120, 10, 650, 20, hwnd, (HMENU)2005, GetModuleHandle(NULL), NULL);
            
            CreateWindow("STATIC", "Duplicate Files (same content):",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        10, 30, 760, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
            
            // Populate list
            PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(2006, 0), 0);  // Trigger refresh
            break;
        }
        
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            
            switch (wmId) {
                case 2006: {  // Refresh button
                    HWND hListView = GetDlgItem(hwnd, 2001);
                    ListView_DeleteAllItems(hListView);
                    
                    EnterCriticalSection(&g_alert_lock);
                    
                    // Check if trigger file still exists
                    BOOL trigger_exists = FileExists(g_current_alert.trigger_file.filepath);
                    
                    if (trigger_exists) {
                        SetDlgItemText(hwnd, 2005, g_current_alert.trigger_file.filepath);
                    } else {
                        SetDlgItemText(hwnd, 2005, "[DELETED] ");
                        char temp[MAX_PATH + 20];
                        snprintf(temp, sizeof(temp), "[DELETED] %s", g_current_alert.trigger_file.filepath);
                        SetDlgItemText(hwnd, 2005, temp);
                    }
                    
                    // Add only files that still exist
                    int valid_index = 0;
                    for (int i = 0; i < g_current_alert.duplicate_count; i++) {
                        if (FileExists(g_current_alert.duplicates[i].filepath)) {
                            LVITEM lvi = {0};
                            lvi.mask = LVIF_TEXT;
                            lvi.iItem = valid_index;
                            lvi.iSubItem = 0;
                            lvi.pszText = g_current_alert.duplicates[i].filepath;
                            ListView_InsertItem(hListView, &lvi);
                            
                            char size_str[64];
                            format_file_size(g_current_alert.duplicates[i].filesize, size_str, sizeof(size_str));
                            ListView_SetItemText(hListView, valid_index, 1, size_str);
                            
                            ListView_SetItemText(hListView, valid_index, 2, "Duplicate");
                            ListView_SetItemText(hListView, valid_index, 3, g_current_alert.duplicates[i].last_modified);
                            
                            valid_index++;
                        }
                    }
                    
                    if (valid_index == 0) {
                        // No files left
                        SetDlgItemText(hwnd, 2005, "All files have been deleted");
                    }
                    
                    LeaveCriticalSection(&g_alert_lock);
                    break;
                }
                
                case 2002: { // Open File Location
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
                            // Refresh the list
                            PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(2006, 0), 0);
                        }
                    } else {
                        MessageBox(hwnd, "Please select a file first.", "Info", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                }
                
                case 2003: { // Delete Selected
                    HWND hListView = GetDlgItem(hwnd, 2001);
                    int selected = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                    
                    if (selected != -1) {
                        char filepath[MAX_PATH];
                        ListView_GetItemText(hListView, selected, 0, filepath, MAX_PATH);
                        
                        // Check if file exists first
                        if (!FileExists(filepath)) {
                            MessageBox(hwnd, "File has already been deleted.", "Info", MB_OK | MB_ICONINFORMATION);
                            PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(2006, 0), 0);  // Refresh
                            break;
                        }
                        
                        char msg[MAX_PATH + 100];
                        snprintf(msg, sizeof(msg), 
                                "Delete this file?\n\n%s\n\nThis will move it to Recycle Bin.",
                                filepath);
                        
                        if (MessageBox(hwnd, msg, "Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
                            // Double-null terminated string for SHFileOperation
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
                                // Refresh the list to remove deleted file
                                PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(2006, 0), 0);
                            } else {
                                // Check if file was actually deleted despite error code
                                Sleep(100);  // Give Windows time to update
                                if (!FileExists(filepath)) {
                                    MessageBox(hwnd, "File was successfully deleted.", "Success", MB_OK | MB_ICONINFORMATION);
                                    PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(2006, 0), 0);
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
                
                case 2004: // Close
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
        0, "DDASReportWindow", "DDAS - Duplicate File Report",
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
            // Setup tray icon
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
            // Message from pipe thread
            if (wParam == 0) {
                // Duplicate detected
                EnterCriticalSection(&g_alert_lock);
                char notification[512];
                snprintf(notification, sizeof(notification), 
                        "Duplicate found: %s\n%d matching file(s)",
                        g_current_alert.trigger_file.filename,
                        g_current_alert.duplicate_count);
                LeaveCriticalSection(&g_alert_lock);
                
                ShowTrayNotification("DDAS - Duplicate Detected", notification);
            } else if (wParam == 1) {
                // Scan complete
                ShowTrayNotification("DDAS", "Initial scan complete");
            }
            break;
        
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK) {
                // Double-click on tray icon - show report
                if (g_current_alert.duplicate_count > 0) {
                    ShowReportWindow();
                }
            } else if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                
                HMENU hMenu = CreatePopupMenu();
                AppendMenu(hMenu, MF_STRING, ID_TRAY_SHOW_WINDOW, "Show Last Alert");
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
                    if (g_current_alert.duplicate_count > 0) {
                        ShowReportWindow();
                    } else {
                        MessageBox(NULL, "No duplicates detected yet.", "DDAS", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                
                case ID_TRAY_ABOUT:
                    MessageBox(NULL, 
                              "DDAS - Duplicate Detection & Alert System\n"
                              "Version 1.0\n\n"
                              "Real-time file duplicate detection with GUI alerts.",
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
    
    // Register window class
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DDASTrayClass";
    
    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, "Window Registration Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    // Create hidden window for tray icon
    g_hMainWnd = CreateWindowEx(
        0, "DDASTrayClass", "DDAS Tray",
        0, 0, 0, 0, 0,
        NULL, NULL, hInstance, NULL
    );
    
    if (!g_hMainWnd) {
        MessageBox(NULL, "Window Creation Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    // Start pipe reader thread
    g_hPipeThread = CreateThread(NULL, 0, PipeReaderThread, NULL, 0, NULL);
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
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