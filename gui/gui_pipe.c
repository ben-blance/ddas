// gui_pipe.c - Named pipe IPC reader thread and tray notifications
#include "gui_common.h"

DWORD WINAPI PipeReaderThread(LPVOID param) {
    (void)param;
    char buffer[65536];

    while (g_running) {
        g_hPipe = CreateFile(
            PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0, NULL,
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
            DWORD error  = GetLastError();

            if (!success && error != ERROR_IO_PENDING) break;

            if (success && bytes_read > 0) {
                buffer[bytes_read] = '\0';
                if (strstr(buffer, "\"type\":\"ALERT\"") && strstr(buffer, "\"DUPLICATE_DETECTED\"")) {
                    ParseAlertJSON(buffer);
                    PostMessage(g_hMainWnd, WM_PIPE_MESSAGE, 0, 0);
                } else if (strstr(buffer, "\"EMPTY_FILE\"")) {
                    ParseEmptyFileJSON(buffer);
                    PostMessage(g_hMainWnd, WM_PIPE_MESSAGE, 2, 0);
                } else if (strstr(buffer, "\"SCAN_COMPLETE\"")) {
                    PostMessage(g_hMainWnd, WM_PIPE_MESSAGE, 1, 0);
                }
                continue;
            }

            // Wait up to 10 s — do NOT CancelIo on timeout; that would
            // break the pipe from the kernel side and log a false disconnect.
            DWORD wait = WaitForSingleObject(overlapped.hEvent, 10000);

            if (wait == WAIT_TIMEOUT) continue;
            if (wait != WAIT_OBJECT_0) break;
            if (!GetOverlappedResult(g_hPipe, &overlapped, &bytes_read, FALSE)) break;
            if (bytes_read == 0) break;

            buffer[bytes_read] = '\0';
            if (strstr(buffer, "\"type\":\"ALERT\"") && strstr(buffer, "\"DUPLICATE_DETECTED\"")) {
                ParseAlertJSON(buffer);
                PostMessage(g_hMainWnd, WM_PIPE_MESSAGE, 0, 0);
            } else if (strstr(buffer, "\"EMPTY_FILE\"")) {
                ParseEmptyFileJSON(buffer);
                PostMessage(g_hMainWnd, WM_PIPE_MESSAGE, 2, 0);
            } else if (strstr(buffer, "\"SCAN_COMPLETE\"")) {
                PostMessage(g_hMainWnd, WM_PIPE_MESSAGE, 1, 0);
            }
        }

        CloseHandle(overlapped.hEvent);
        CloseHandle(g_hPipe);
        g_hPipe = INVALID_HANDLE_VALUE;

        if (!g_running) break;
        Sleep(1000);
    }

    return 0;
}

void ShowTrayNotification(const char *title, const char *message) {
    NOTIFYICONDATA nid = {0};
    nid.cbSize     = sizeof(NOTIFYICONDATA);
    nid.hWnd       = g_hMainWnd;
    nid.uID        = 1;
    nid.uFlags     = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    nid.uTimeout   = 5000;
    strncpy(nid.szInfoTitle, title,   sizeof(nid.szInfoTitle) - 1);
    strncpy(nid.szInfo,      message, sizeof(nid.szInfo) - 1);
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}
