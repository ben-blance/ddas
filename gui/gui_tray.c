// gui_tray.c - System tray window, WinMain entry point
// Globals owned here: window handles, tray icon, pipe handle, running flag.
#include "gui_common.h"

// ----------------------------------------------------------------
// Global definitions (owned here)
// ----------------------------------------------------------------
HWND           g_hMainWnd    = NULL;
HWND           g_hReportWnd  = NULL;
HWND           g_hSettingsWnd = NULL;
NOTIFYICONDATA g_nid         = {0};
HANDLE         g_hPipeThread = NULL;
HANDLE         g_hPipe       = INVALID_HANDLE_VALUE;
volatile BOOL  g_running     = TRUE;
char           g_current_watch_dir[MAX_PATH] = {0};

// ----------------------------------------------------------------
// Main (hidden) tray window procedure
// ----------------------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE:
        g_nid.cbSize          = sizeof(NOTIFYICONDATA);
        g_nid.hWnd            = hwnd;
        g_nid.uID             = 1;
        g_nid.uFlags          = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon           = LoadIcon(NULL, IDI_APPLICATION);
        strcpy(g_nid.szTip, "DDAS - Duplicate Detector");
        Shell_NotifyIcon(NIM_ADD, &g_nid);
        break;

    case WM_PIPE_MESSAGE:
        if (wParam == 0) {
            // Duplicate detected
            EnterCriticalSection(&g_alert_lock);
            if (g_alert_count > 0) {
                DuplicateAlert *a = &g_alerts[g_current_alert_index];
                char note[512];
                snprintf(note, sizeof(note),
                    "Duplicate group updated: %s\n%d file(s) with same content",
                    a->trigger_file.filename, a->files_remaining);
                LeaveCriticalSection(&g_alert_lock);
                ShowTrayNotification("DDAS - Duplicate Group", note);
                if (g_hReportWnd) UpdateReportWindow();
            } else {
                LeaveCriticalSection(&g_alert_lock);
            }
        } else if (wParam == 1) {
            // Initial scan complete
            g_scanning = FALSE;
            EnterCriticalSection(&g_alert_lock);
            int cnt = g_alert_count;
            LeaveCriticalSection(&g_alert_lock);
            char note[256];
            snprintf(note, sizeof(note),
                "Initial scan complete. Found %d duplicate group(s).", cnt);
            ShowTrayNotification("DDAS", note);
            if (g_hReportWnd) UpdateReportWindow();
        } else if (wParam == 2) {
            // Empty file detected
            if (g_hReportWnd) UpdateReportWindow();
        }
        break;

    case WM_DIR_CHANGED: {
        // Engine started a new directory scan; clear all GUI data
        g_scanning = TRUE;
        EnterCriticalSection(&g_alert_lock);
        memset(g_alerts, 0, sizeof(g_alerts));
        g_alert_count         = 0;
        g_current_alert_index = 0;
        memset(g_empty_entries, 0, sizeof(g_empty_entries));
        g_empty_count = 0;
        LeaveCriticalSection(&g_alert_lock);

        if (g_hReportWnd) UpdateReportWindow();
        break;
    }

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowReportWindow();
        } else if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_TRAY_SHOW_WINDOW, "Show Alerts");
            AppendMenu(hMenu, MF_STRING, ID_TRAY_SETTINGS,    "Settings");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT,        "Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_SHOW_WINDOW:
            ShowReportWindow();
            break;
        case ID_TRAY_SETTINGS:
            ShowSettingsWindow();
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

// ----------------------------------------------------------------
// Entry point
// ----------------------------------------------------------------

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // Opt into native DPI rendering so GDI is crisp, not bitmap-stretched.
    {
        typedef BOOL (WINAPI *SetDPIAwareFn)(void);
        HMODULE u32 = GetModuleHandleA("user32.dll");
        if (u32) {
            SetDPIAwareFn fn = (SetDPIAwareFn)GetProcAddress(u32, "SetProcessDPIAware");
            if (fn) fn();
        }
    }

    InitializeCriticalSection(&g_alert_lock);
    InitCommonControls();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    memset(g_alerts, 0, sizeof(g_alerts));
    g_alert_count         = 0;
    g_current_alert_index = 0;

    WNDCLASSEX wc = {0};
    wc.cbSize       = sizeof(WNDCLASSEX);
    wc.lpfnWndProc  = WndProc;
    wc.hInstance    = hInstance;
    wc.lpszClassName = "DDASTrayClass";

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, "Window Registration Failed!", "Error",
            MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    g_hMainWnd = CreateWindowEx(0, "DDASTrayClass", "DDAS Tray",
        0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    if (!g_hMainWnd) {
        MessageBox(NULL, "Window Creation Failed!", "Error",
            MB_ICONEXCLAMATION | MB_OK);
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
    if (g_hPipe != INVALID_HANDLE_VALUE) CloseHandle(g_hPipe);

    DeleteCriticalSection(&g_alert_lock);
    CoUninitialize();
    return (int)msg.wParam;
}

