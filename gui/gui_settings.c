// gui_settings.c - Settings window: directory picker and apply
#include "gui_common.h"
#include <shlobj.h>
#include <shobjidl.h>   // IFileOpenDialog (Vista+ modern picker)
// ---- Minimal COM glue for IFileOpenDialog (Vista+) ----
// Defined here because TDM-GCC-32 MinGW shobjidl.h is often incomplete.

#ifndef SIGDN_FILESYSPATH
typedef int SIGDN;
#define SIGDN_FILESYSPATH ((SIGDN)0x80058000)
#endif

#ifndef FOS_PICKFOLDERS
#define FOS_NOCHANGEDIR     0x00000008UL
#define FOS_PICKFOLDERS     0x00000020UL
#define FOS_FORCEFILESYSTEM 0x00000040UL
#define FOS_PATHMUSTEXIST   0x00000800UL
#endif

typedef struct IShellItem IShellItem;
typedef struct IShellItemVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IShellItem*,REFIID,void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IShellItem*);
    ULONG   (STDMETHODCALLTYPE *Release)(IShellItem*);
    HRESULT (STDMETHODCALLTYPE *BindToHandler)(IShellItem*,void*,REFGUID,REFIID,void**);
    HRESULT (STDMETHODCALLTYPE *GetParent)(IShellItem*,IShellItem**);
    HRESULT (STDMETHODCALLTYPE *GetDisplayName)(IShellItem*,SIGDN,LPWSTR*);
    HRESULT (STDMETHODCALLTYPE *GetAttributes)(IShellItem*,DWORD,DWORD*);
    HRESULT (STDMETHODCALLTYPE *Compare)(IShellItem*,IShellItem*,DWORD,int*);
} IShellItemVtbl;
struct IShellItem { IShellItemVtbl *lpVtbl; };

typedef struct IFileOpenDialog IFileOpenDialog;
typedef struct IFileOpenDialogVtbl {
    /* IUnknown */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IFileOpenDialog*,REFIID,void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IFileOpenDialog*);
    ULONG   (STDMETHODCALLTYPE *Release)(IFileOpenDialog*);
    /* IModalWindow */
    HRESULT (STDMETHODCALLTYPE *Show)(IFileOpenDialog*,HWND);
    /* IFileDialog */
    HRESULT (STDMETHODCALLTYPE *SetFileTypes)(IFileOpenDialog*,UINT,const void*);
    HRESULT (STDMETHODCALLTYPE *SetFileTypeIndex)(IFileOpenDialog*,UINT);
    HRESULT (STDMETHODCALLTYPE *GetFileTypeIndex)(IFileOpenDialog*,UINT*);
    HRESULT (STDMETHODCALLTYPE *Advise)(IFileOpenDialog*,void*,DWORD*);
    HRESULT (STDMETHODCALLTYPE *Unadvise)(IFileOpenDialog*,DWORD);
    HRESULT (STDMETHODCALLTYPE *SetOptions)(IFileOpenDialog*,DWORD);
    HRESULT (STDMETHODCALLTYPE *GetOptions)(IFileOpenDialog*,DWORD*);
    HRESULT (STDMETHODCALLTYPE *SetDefaultFolder)(IFileOpenDialog*,IShellItem*);
    HRESULT (STDMETHODCALLTYPE *SetFolder)(IFileOpenDialog*,IShellItem*);
    HRESULT (STDMETHODCALLTYPE *GetFolder)(IFileOpenDialog*,IShellItem**);
    HRESULT (STDMETHODCALLTYPE *GetCurrentSelection)(IFileOpenDialog*,IShellItem**);
    HRESULT (STDMETHODCALLTYPE *SetFileName)(IFileOpenDialog*,LPCWSTR);
    HRESULT (STDMETHODCALLTYPE *GetFileName)(IFileOpenDialog*,LPWSTR*);
    HRESULT (STDMETHODCALLTYPE *SetTitle)(IFileOpenDialog*,LPCWSTR);
    HRESULT (STDMETHODCALLTYPE *SetOkButtonLabel)(IFileOpenDialog*,LPCWSTR);
    HRESULT (STDMETHODCALLTYPE *SetFileNameLabel)(IFileOpenDialog*,LPCWSTR);
    HRESULT (STDMETHODCALLTYPE *GetResult)(IFileOpenDialog*,IShellItem**);
    HRESULT (STDMETHODCALLTYPE *AddPlace)(IFileOpenDialog*,IShellItem*,int);
    HRESULT (STDMETHODCALLTYPE *SetDefaultExtension)(IFileOpenDialog*,LPCWSTR);
    HRESULT (STDMETHODCALLTYPE *Close)(IFileOpenDialog*,HRESULT);
    HRESULT (STDMETHODCALLTYPE *SetClientGuid)(IFileOpenDialog*,REFGUID);
    HRESULT (STDMETHODCALLTYPE *ClearClientData)(IFileOpenDialog*);
    HRESULT (STDMETHODCALLTYPE *SetFilter)(IFileOpenDialog*,void*);
    /* IFileOpenDialog */
    HRESULT (STDMETHODCALLTYPE *GetResults)(IFileOpenDialog*,void**);
    HRESULT (STDMETHODCALLTYPE *GetSelectedItems)(IFileOpenDialog*,void**);
} IFileOpenDialogVtbl;
struct IFileOpenDialog { IFileOpenDialogVtbl *lpVtbl; };

static const CLSID s_CLSID_FOD =
    {0xDC1C5A9C,0xE88A,0x4DDE,{0xA5,0xA1,0x60,0xF8,0x2A,0x20,0xAE,0xF7}};
static const IID   s_IID_FOD  =
    {0xD57C7288,0xD4AD,0x4768,{0xBE,0x02,0x9D,0x96,0x95,0x32,0xD9,0x60}};
static const IID   s_IID_SI   =
    {0x43826D1E,0xE718,0x42EE,{0xBC,0x55,0xA1,0xE2,0x61,0xC3,0x7B,0xFE}};

typedef HRESULT (WINAPI *PFN_SHCreateItemFromParsingName)(LPCWSTR,void*,REFIID,void**);

// Modern Explorer-style folder picker. Returns TRUE and fills out[] on success.
static BOOL PickFolderModern(HWND parent, const char *seed, char *out, int outLen) {
    IFileOpenDialog *pDlg = NULL;
    HRESULT hr = CoCreateInstance(&s_CLSID_FOD, NULL,
        CLSCTX_INPROC_SERVER, &s_IID_FOD, (void**)&pDlg);
    if (FAILED(hr)) return FALSE;

    DWORD opts = 0;
    pDlg->lpVtbl->GetOptions(pDlg, &opts);
    pDlg->lpVtbl->SetOptions(pDlg,
        opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    pDlg->lpVtbl->SetTitle(pDlg, L"Select folder to watch");

    // Pre-seed the dialog to the current path
    if (seed && seed[0]) {
        WCHAR wseed[MAX_PATH] = {0};
        MultiByteToWideChar(CP_ACP, 0, seed, -1, wseed, MAX_PATH);
        HMODULE hShell = GetModuleHandleA("shell32.dll");
        PFN_SHCreateItemFromParsingName pfn = hShell ?
            (PFN_SHCreateItemFromParsingName)GetProcAddress(
                hShell, "SHCreateItemFromParsingName") : NULL;
        if (pfn) {
            IShellItem *pSeed = NULL;
            if (SUCCEEDED(pfn(wseed, NULL, &s_IID_SI, (void**)&pSeed))) {
                pDlg->lpVtbl->SetFolder(pDlg, pSeed);
                pSeed->lpVtbl->Release(pSeed);
            }
        }
    }

    BOOL picked = FALSE;
    hr = pDlg->lpVtbl->Show(pDlg, parent);
    if (SUCCEEDED(hr)) {
        IShellItem *pItem = NULL;
        hr = pDlg->lpVtbl->GetResult(pDlg, &pItem);
        if (SUCCEEDED(hr)) {
            LPWSTR pPath = NULL;
            hr = pItem->lpVtbl->GetDisplayName(pItem, SIGDN_FILESYSPATH, &pPath);
            if (SUCCEEDED(hr) && pPath) {
                WideCharToMultiByte(CP_ACP, 0, pPath, -1, out, outLen, NULL, NULL);
                CoTaskMemFree(pPath);
                picked = TRUE;
            }
            pItem->lpVtbl->Release(pItem);
        }
    }
    pDlg->lpVtbl->Release(pDlg);
    return picked;
}

// g_hSettingsWnd is defined in gui_tray.c (declared extern in gui_common.h)

// ----------------------------------------------------------------
// SendChangeDirectoryCommand — sends JSON over the open pipe
// ----------------------------------------------------------------

BOOL SendChangeDirectoryCommand(const char *path) {
    char escaped[MAX_PATH * 2 + 4] = {0};
    int wi = 0;

    if (g_hPipe == INVALID_HANDLE_VALUE) return FALSE;

    for (const char *p = path; *p && wi < (int)sizeof(escaped) - 2; p++) {
        if (*p == '\\') {
            escaped[wi++] = '\\';
            escaped[wi++] = '\\';
        } else {
            escaped[wi++] = *p;
        }
    }
    escaped[wi] = '\0';

    char json[MAX_PATH * 2 + 128];
    int len = snprintf(json, sizeof(json),
        "{\"type\":\"COMMAND\",\"action\":\"CHANGE_DIRECTORY\",\"path\":\"%s\"}\n",
        escaped);
    if (len <= 0) return FALSE;

    DWORD written = 0;
    BOOL ok = WriteFile(g_hPipe, json, (DWORD)len, &written, NULL);
    return ok && written == (DWORD)len;
}

// ----------------------------------------------------------------
// Settings window procedure
// ----------------------------------------------------------------

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        EnsureThemeResources();
        ApplyDarkTitleBar(hwnd);

        // Title
        HWND hT = CreateWindow("STATIC", "Settings",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 18, 400, 32, hwnd, (HMENU)0, GetModuleHandle(NULL), NULL);
        SendMessage(hT, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

        // Accent strip
        // (drawn in WM_ERASEBKGND below)

        // Section label
        HWND hSec = CreateWindow("STATIC", "WATCH DIRECTORY",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 78, 500, 18, hwnd, (HMENU)0, GetModuleHandle(NULL), NULL);
        SendMessage(hSec, WM_SETFONT, (WPARAM)g_fontUIBold, TRUE);

        // Path text box
        HWND hPath = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT",
            g_current_watch_dir[0] ? g_current_watch_dir : "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            24, 104, 460, 28, hwnd, (HMENU)ID_SETTINGS_PATH,
            GetModuleHandle(NULL), NULL);
        SendMessage(hPath, WM_SETFONT, (WPARAM)g_fontUI, TRUE);

        // Browse button
        CreateModernButton(hwnd, "Browse...", 494, 104, 90, 28,
            ID_SETTINGS_BROWSE, BK_GHOST);

        // Hint
        HWND hHint = CreateWindow("STATIC",
            "Choose the folder DDAS should watch for duplicates and empty files.",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 140, 560, 32, hwnd, (HMENU)0, GetModuleHandle(NULL), NULL);
        SendMessage(hHint, WM_SETFONT, (WPARAM)g_fontUI, TRUE);

        // Status label
        HWND hStatus = CreateWindow("STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 178, 560, 20, hwnd, (HMENU)ID_SETTINGS_STATUS,
            GetModuleHandle(NULL), NULL);
        SendMessage(hStatus, WM_SETFONT, (WPARAM)g_fontUI, TRUE);

        // Buttons
        CreateModernButton(hwnd, "Apply Settings", 352, 220, 150, 38,
            ID_SETTINGS_APPLY, BK_PRIMARY);
        CreateModernButton(hwnd, "Cancel", 512, 220, 72, 38,
            ID_SETTINGS_CANCEL, BK_GHOST);
        break;
    }

    case WM_ERASEBKGND: {
        HDC dc = (HDC)wParam;
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, g_brBg);

        // Cyan accent strip
        RECT strip = { 24, 68, 80, 70 };
        HBRUSH ab = CreateSolidBrush(CLR_ACCENT);
        FillRect(dc, &strip, ab);
        DeleteObject(ab);

        // Panel behind controls
        RECT panel = { 16, 70, 600, 210 };
        FillRect(dc, &panel, g_brPanel);
        FrameRect(dc, &panel, g_brBorder);
        return 1;
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wParam;
        SetBkMode(dc, TRANSPARENT);
        int id = GetDlgCtrlID((HWND)lParam);
        if (id == ID_SETTINGS_STATUS) SetTextColor(dc, CLR_ACCENT);
        else                          SetTextColor(dc, CLR_TEXT);
        return (LRESULT)g_brBg;
    }

    // Make the edit box match the dark theme
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wParam;
        SetBkColor(dc, RGB(32, 36, 42));
        SetTextColor(dc, CLR_TEXT);
        static HBRUSH hEditBr = NULL;
        if (!hEditBr) hEditBr = CreateSolidBrush(RGB(32, 36, 42));
        return (LRESULT)hEditBr;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType == ODT_BUTTON) {
            DrawModernButton(dis);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);

        if (id == ID_SETTINGS_BROWSE) {
            char cur[MAX_PATH]    = {0};
            char chosen[MAX_PATH] = {0};
            GetDlgItemText(hwnd, ID_SETTINGS_PATH, cur, MAX_PATH);
            if (PickFolderModern(hwnd, cur, chosen, MAX_PATH)) {
                SetDlgItemText(hwnd, ID_SETTINGS_PATH, chosen);
                SetDlgItemText(hwnd, ID_SETTINGS_STATUS, "");
            }
            break;
        }

        if (id == ID_SETTINGS_APPLY) {
            char path[MAX_PATH] = {0};
            GetDlgItemText(hwnd, ID_SETTINGS_PATH, path, MAX_PATH);

            // Trim trailing spaces
            int n = (int)strlen(path);
            while (n > 0 && path[n-1] == ' ') path[--n] = '\0';

            if (path[0] == '\0') {
                SetDlgItemText(hwnd, ID_SETTINGS_STATUS,
                    "Please enter or browse to a folder path.");
                break;
            }

            // Verify the path exists
            DWORD attr = GetFileAttributes(path);
            if (attr == INVALID_FILE_ATTRIBUTES ||
                !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                SetDlgItemText(hwnd, ID_SETTINGS_STATUS,
                    "Folder not found. Please check the path.");
                break;
            }

            // Store and send
            strncpy(g_current_watch_dir, path, MAX_PATH - 1);
            g_current_watch_dir[MAX_PATH - 1] = '\0';

            // Mark as scanning before opening the report window so it
            // immediately shows "Scanning in progress..." rather than "No files found"
            g_scanning = TRUE;

            if (SendChangeDirectoryCommand(path)) {
                // Close settings, open report window to show scan progress
                DestroyWindow(hwnd);
                ShowReportWindow();
            } else {
                g_scanning = FALSE;
                SetDlgItemText(hwnd, ID_SETTINGS_STATUS,
                    "Could not reach engine (is it running?). Path saved.");
            }
            break;
        }

        if (id == ID_SETTINGS_CANCEL) {
            DestroyWindow(hwnd);
            g_hSettingsWnd = NULL;
            break;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        g_hSettingsWnd = NULL;
        return 0;

    case WM_DESTROY:
        g_hSettingsWnd = NULL;
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ----------------------------------------------------------------
// Show (or bring to front) the settings window
// ----------------------------------------------------------------

void ShowSettingsWindow(void) {
    if (g_hSettingsWnd) {
        SetForegroundWindow(g_hSettingsWnd);
        return;
    }

    EnsureThemeResources();

    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = SettingsWndProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = "DDASSettingsWindow";

    if (!GetClassInfoEx(GetModuleHandle(NULL), "DDASSettingsWindow", &wc))
        RegisterClassEx(&wc);

    g_hSettingsWnd = CreateWindowEx(
        WS_EX_DLGMODALFRAME,
        "DDASSettingsWindow", "DDAS - Settings",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 290,
        g_hMainWnd, NULL, GetModuleHandle(NULL), NULL);

    ApplyDarkTitleBar(g_hSettingsWnd);
    SetForegroundWindow(g_hSettingsWnd);
}
