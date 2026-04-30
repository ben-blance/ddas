// gui_theme.c - Dark theme resources, owner-draw buttons, DWM dark title bar
#include "gui_common.h"

// ----------------------------------------------------------------
// Global definitions (owned here)
// ----------------------------------------------------------------
HBRUSH g_brBg     = NULL;
HBRUSH g_brPanel  = NULL;
HBRUSH g_brBorder = NULL;
HFONT  g_fontUI      = NULL;
HFONT  g_fontUIBold  = NULL;
HFONT  g_fontTitle   = NULL;
HFONT  g_fontMono    = NULL;
int    g_hotBtnId    = 0;

// ----------------------------------------------------------------
// Resource initialisation (idempotent)
// ----------------------------------------------------------------

void EnsureThemeResources(void) {
    if (!g_brBg)     g_brBg     = CreateSolidBrush(CLR_BG);
    if (!g_brPanel)  g_brPanel  = CreateSolidBrush(CLR_PANEL);
    if (!g_brBorder) g_brBorder = CreateSolidBrush(CLR_BORDER);

    if (!g_fontUI)
        g_fontUI = CreateFont(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    if (!g_fontUIBold)
        g_fontUIBold = CreateFont(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    if (!g_fontTitle)
        g_fontTitle = CreateFont(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI Semibold");

    if (!g_fontMono)
        g_fontMono = CreateFont(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
}

// ----------------------------------------------------------------
// Dark title bar via DWM (loaded dynamically for MinGW compat)
// ----------------------------------------------------------------

void ApplyDarkTitleBar(HWND hwnd) {
    typedef HRESULT (WINAPI *DwmFn)(HWND, DWORD, LPCVOID, DWORD);
    static DwmFn pDwm = NULL;
    static int   loaded = 0;
    if (!loaded) {
        loaded = 1;
        HMODULE h = LoadLibraryA("dwmapi.dll");
        if (h) pDwm = (DwmFn)GetProcAddress(h, "DwmSetWindowAttribute");
    }
    if (!pDwm) return;
    BOOL dark = TRUE;
    if (FAILED(pDwm(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark))))
        pDwm(hwnd, 19, &dark, sizeof(dark));   // Windows 10 1809 fallback
}

// ----------------------------------------------------------------
// Button subclass — tracks hover for owner-draw highlight
// ----------------------------------------------------------------

static LRESULT CALLBACK BtnSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                        UINT_PTR id, DWORD_PTR refData) {
    (void)refData;
    switch (msg) {
        case WM_MOUSEMOVE:
            if (g_hotBtnId != (int)id) {
                g_hotBtnId = (int)id;
                InvalidateRect(hwnd, NULL, TRUE);
                TRACKMOUSEEVENT tme = { sizeof(tme) };
                tme.dwFlags   = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            break;
        case WM_MOUSELEAVE:
            if (g_hotBtnId == (int)id) {
                g_hotBtnId = 0;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ----------------------------------------------------------------
// Public button factory
// ----------------------------------------------------------------

HWND CreateModernButton(HWND parent, const char *text,
                        int x, int y, int w, int h, int id, int kind) {
    HWND b = CreateWindow("BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        x, y, w, h, parent, (HMENU)(LONG_PTR)id, GetModuleHandle(NULL), NULL);
    SetWindowLongPtr(b, GWLP_USERDATA, (LONG_PTR)kind);
    SendMessage(b, WM_SETFONT, (WPARAM)g_fontUIBold, TRUE);
    SetWindowSubclass(b, BtnSubclassProc, (UINT_PTR)id, 0);
    return b;
}

// ----------------------------------------------------------------
// Owner-draw rendering for buttons
// ----------------------------------------------------------------

void DrawModernButton(LPDRAWITEMSTRUCT dis) {
    int  kind     = (int)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
    BOOL pressed  = (dis->itemState & ODS_SELECTED) != 0;
    BOOL hot      = (g_hotBtnId == (int)dis->CtlID);
    BOOL focused  = (dis->itemState & ODS_FOCUS)    != 0;
    BOOL disabled = (dis->itemState & ODS_DISABLED) != 0;

    COLORREF fill, text;
    switch (kind) {
        case BK_PRIMARY:
            fill = pressed ? CLR_ACCENT_DOWN : (hot ? CLR_ACCENT_HOT : CLR_ACCENT);
            text = RGB(10, 14, 18);
            break;
        case BK_DANGER:
            fill = pressed ? RGB(180, 60, 60) : (hot ? CLR_DANGER_HOT : CLR_DANGER);
            text = RGB(255, 255, 255);
            break;
        case BK_GHOST:
        default:
            fill = pressed ? RGB(40, 46, 54) : (hot ? RGB(38, 44, 52) : CLR_PANEL);
            text = hot ? CLR_ACCENT_HOT : CLR_TEXT;
            break;
    }
    if (disabled) { fill = RGB(40, 44, 50); text = CLR_TEXT_DIM; }

    RECT r  = dis->rcItem;
    HDC  dc = dis->hDC;

    // Erase to window bg first so rounded corners blend seamlessly
    HBRUSH bgBrush = CreateSolidBrush(CLR_BG);
    FillRect(dc, &r, bgBrush);
    DeleteObject(bgBrush);

    HBRUSH fb  = CreateSolidBrush(fill);
    HPEN   pen = (kind == BK_GHOST)
        ? CreatePen(PS_SOLID, 1, hot ? CLR_ACCENT : CLR_BORDER)
        : CreatePen(PS_SOLID, 1, fill);

    HGDIOBJ oldB = SelectObject(dc, fb);
    HGDIOBJ oldP = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 8, 8);
    SelectObject(dc, oldB);
    SelectObject(dc, oldP);
    DeleteObject(fb);
    DeleteObject(pen);

    if (focused && !pressed) {
        HPEN fp = CreatePen(PS_SOLID, 1, CLR_ACCENT_HOT);
        HGDIOBJ ofp = SelectObject(dc, fp);
        HGDIOBJ obr = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, r.left + 2, r.top + 2, r.right - 2, r.bottom - 2, 6, 6);
        SelectObject(dc, ofp);
        SelectObject(dc, obr);
        DeleteObject(fp);
    }

    char buf[256];
    GetWindowText(dis->hwndItem, buf, sizeof(buf));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    HGDIOBJ oldF = SelectObject(dc, g_fontUIBold);
    DrawText(dc, buf, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldF);
}
