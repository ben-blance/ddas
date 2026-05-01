// gui_common.h - Shared types, constants, and extern declarations for DDAS GUI
#ifndef GUI_COMMON_H
#define GUI_COMMON_H

#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ----------------------------------------------------------------
// Window messages
// ----------------------------------------------------------------
#define WM_TRAYICON     (WM_USER + 1)
#define WM_PIPE_MESSAGE (WM_USER + 2)
#define WM_DIR_CHANGED  (WM_USER + 3)   // posted when engine changes directory

// Tray menu IDs
#define ID_TRAY_EXIT        1001
#define ID_TRAY_SHOW_WINDOW 1002
#define ID_TRAY_SETTINGS    1003   // replaces About

// Settings window control IDs
#define ID_SETTINGS_PATH    4001
#define ID_SETTINGS_BROWSE  4002
#define ID_SETTINGS_APPLY   4003
#define ID_SETTINGS_CANCEL  4004
#define ID_SETTINGS_STATUS  4005

// Context menu IDs
#define IDM_CTX_SELECT      3001

// IPC
#define PIPE_NAME "\\\\.\\pipe\\ddas_ipc"

// Alert limits
#define MAX_DUPLICATES 100
#define MAX_ALERTS     100
#define MAX_EMPTY_FILES 1000

// View modes for the report window
#define VIEW_MODE_DUPLICATES 0
#define VIEW_MODE_EMPTY      1

// ----------------------------------------------------------------
// DWM / ListView compat defines (not in all MinGW headers)
// ----------------------------------------------------------------
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef LVS_EX_DOUBLEBUFFER
#define LVS_EX_DOUBLEBUFFER 0x00010000
#endif

// ----------------------------------------------------------------
// Modern dark theme palette (cyan accent)
// ----------------------------------------------------------------
#define CLR_BG          RGB(18, 20, 24)
#define CLR_PANEL       RGB(26, 29, 34)
#define CLR_PANEL_ALT   RGB(32, 36, 42)
#define CLR_BORDER      RGB(48, 54, 62)
#define CLR_TEXT        RGB(228, 232, 238)
#define CLR_TEXT_DIM    RGB(150, 158, 170)
#define CLR_ACCENT      RGB(0, 200, 220)
#define CLR_ACCENT_HOT  RGB(64, 224, 240)
#define CLR_ACCENT_DOWN RGB(0, 160, 180)
#define CLR_DANGER      RGB(232, 92, 92)
#define CLR_DANGER_HOT  RGB(248, 120, 120)
#define CLR_SEL         RGB(0, 90, 105)

// ----------------------------------------------------------------
// Button draw kinds
// ----------------------------------------------------------------
#define BK_PRIMARY 0
#define BK_GHOST   1
#define BK_DANGER  2

// ----------------------------------------------------------------
// Data structures
// ----------------------------------------------------------------
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
    char filehash[65];
    FileInfo trigger_file;
    FileInfo duplicates[MAX_DUPLICATES];
    int duplicate_count;
    char timestamp[32];
    int files_remaining;
} DuplicateAlert;

typedef struct {
    char filepath[MAX_PATH];
    unsigned long long filesize;
    char last_modified[32];
} EmptyFileEntry;

// ----------------------------------------------------------------
// Globals — defined in their owning translation units
// ----------------------------------------------------------------

// gui_tray.c
extern HWND              g_hMainWnd;
extern HWND              g_hReportWnd;
extern HWND              g_hSettingsWnd;
extern NOTIFYICONDATA    g_nid;
extern HANDLE            g_hPipeThread;
extern HANDLE            g_hPipe;
extern volatile BOOL     g_running;

extern char              g_current_watch_dir[MAX_PATH];

// gui_alerts.c
extern DuplicateAlert    g_alerts[MAX_ALERTS];
extern int               g_alert_count;
extern int               g_current_alert_index;
extern CRITICAL_SECTION  g_alert_lock;

extern EmptyFileEntry    g_empty_entries[MAX_EMPTY_FILES];
extern int               g_empty_count;
extern int               g_view_mode;
extern volatile BOOL     g_scanning;

// gui_theme.c
extern HBRUSH g_brBg;
extern HBRUSH g_brPanel;
extern HBRUSH g_brBorder;
extern HFONT  g_fontUI;
extern HFONT  g_fontUIBold;
extern HFONT  g_fontTitle;
extern HFONT  g_fontMono;
extern int    g_hotBtnId;

// ----------------------------------------------------------------
// Function declarations
// ----------------------------------------------------------------

// gui_alerts.c
BOOL  FileExists(const char *filepath);
void  format_file_size(unsigned long long size, char *buffer, size_t buf_size);
int   CountRemainingFiles(DuplicateAlert *alert);
int   FindNextValidGroup(int current_index, int direction);
int   find_alert_by_hash(const char *filehash);
void  ParseAlertJSON(const char *json);
void  ParseEmptyFileJSON(const char *json);
void  CompactAlerts(void);
void  PromoteDuplicate(DuplicateAlert *alert);
void  RemoveAlertAt(int index);
void  RemoveEmptyEntry(const char *filepath);

// gui_pipe.c
DWORD WINAPI PipeReaderThread(LPVOID param);
void  ShowTrayNotification(const char *title, const char *message);

// gui_theme.c
void  EnsureThemeResources(void);
void  ApplyDarkTitleBar(HWND hwnd);
HWND  CreateModernButton(HWND parent, const char *text, int x, int y, int w, int h, int id, int kind);
void  DrawModernButton(LPDRAWITEMSTRUCT dis);

// gui_report.c
void  UpdateReportWindow(void);
void  ShowReportWindow(void);
LRESULT CALLBACK ReportWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// gui_settings.c
void  ShowSettingsWindow(void);
BOOL  SendChangeDirectoryCommand(const char *path);
LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// gui_tray.c
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

#endif // GUI_COMMON_H
