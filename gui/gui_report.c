// gui_report.c - Report window: creation, painting, list update, user actions
#include "gui_common.h"

// Index of the item last right-clicked (for context-menu handler)
static int  s_ctxItemIdx    = -1;
// TRUE once user enters selection mode via right-click > Select
static BOOL s_selectionMode = FALSE;
// Per-row check state (managed manually, no LVS_EX_CHECKBOXES needed)
static BOOL s_checked[MAX_DUPLICATES + 1];

// ----------------------------------------------------------------
// UpdateReportWindow — repopulate the file list for the current group
// ----------------------------------------------------------------

void UpdateReportWindow(void) {
    if (!g_hReportWnd) return;

    HWND hList = GetDlgItem(g_hReportWnd, 2001);
    ListView_DeleteAllItems(hList);

    // Reset selection state whenever the list is rebuilt
    s_selectionMode = FALSE;
    memset(s_checked, 0, sizeof(s_checked));

    EnterCriticalSection(&g_alert_lock);

    if (g_alert_count == 0) {
        SetDlgItemText(g_hReportWnd, 2007,
            "No duplicate alerts yet - DDAS is watching for duplicates.");
        SetDlgItemText(g_hReportWnd, 2005, "");
        LeaveCriticalSection(&g_alert_lock);
        return;
    }

    DuplicateAlert *alert = &g_alerts[g_current_alert_index];

    char nav[256];
    snprintf(nav, sizeof(nav),
        "Group %d of %d  |  %d files remaining  |  hash %.12s...",
        g_current_alert_index + 1, g_alert_count,
        alert->files_remaining, alert->filehash);
    SetDlgItemText(g_hReportWnd, 2007, nav);
    SetDlgItemText(g_hReportWnd, 2005, "");

    int vi = 0;

    // Trigger file
    if (FileExists(alert->trigger_file.filepath)) {
        LVITEM lvi = {0};
        lvi.mask     = LVIF_TEXT;
        lvi.iItem    = vi;
        lvi.iSubItem = 0;
        lvi.pszText  = alert->trigger_file.filepath;
        ListView_InsertItem(hList, &lvi);

        char sz[64];
        format_file_size(alert->trigger_file.filesize, sz, sizeof(sz));
        ListView_SetItemText(hList, vi, 1, sz);
        ListView_SetItemText(hList, vi, 2, "Trigger");
        ListView_SetItemText(hList, vi, 3,
            strlen(alert->trigger_file.last_modified) > 0
                ? alert->trigger_file.last_modified : "Unknown");
        vi++;
    }

    // Duplicate files
    for (int i = 0; i < alert->duplicate_count; i++) {
        if (!FileExists(alert->duplicates[i].filepath)) continue;

        LVITEM lvi = {0};
        lvi.mask     = LVIF_TEXT;
        lvi.iItem    = vi;
        lvi.iSubItem = 0;
        lvi.pszText  = alert->duplicates[i].filepath;
        ListView_InsertItem(hList, &lvi);

        char sz[64];
        format_file_size(alert->duplicates[i].filesize, sz, sizeof(sz));
        ListView_SetItemText(hList, vi, 1, sz);
        ListView_SetItemText(hList, vi, 2, "Duplicate");
        ListView_SetItemText(hList, vi, 3,
            strlen(alert->duplicates[i].last_modified) > 0
                ? alert->duplicates[i].last_modified : "Unknown");
        vi++;
    }

    LeaveCriticalSection(&g_alert_lock);
}

// ----------------------------------------------------------------
// Report window procedure
// ----------------------------------------------------------------

LRESULT CALLBACK ReportWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    // ------------------------------------------------------------
    case WM_CREATE: {
        EnsureThemeResources();
        ApplyDarkTitleBar(hwnd);

        HWND hTitle = CreateWindow("STATIC", "Duplicate Groups",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 18, 500, 32, hwnd, (HMENU)2010, GetModuleHandle(NULL), NULL);
        SendMessage(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

        HWND hSub = CreateWindow("STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 52, 760, 20, hwnd, (HMENU)2007, GetModuleHandle(NULL), NULL);
        SendMessage(hSub, WM_SETFONT, (WPARAM)g_fontUI, TRUE);

        // Hidden compat field
        HWND hStatus = CreateWindow("STATIC", "",
            WS_CHILD | SS_LEFT,
            0, 0, 0, 0, hwnd, (HMENU)2005, GetModuleHandle(NULL), NULL);
        SendMessage(hStatus, WM_SETFONT, (WPARAM)g_fontUI, TRUE);

        HWND hSection = CreateWindow("STATIC", "FILES IN GROUP",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 88, 760, 18, hwnd, (HMENU)2011, GetModuleHandle(NULL), NULL);
        SendMessage(hSection, WM_SETFONT, (WPARAM)g_fontUIBold, TRUE);

        HWND hList = CreateWindowEx(0, WC_LISTVIEW, "",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDRAWFIXED,
            24, 112, 832, 388, hwnd, (HMENU)2001, GetModuleHandle(NULL), NULL);
        ListView_SetExtendedListViewStyle(hList,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        SetWindowTheme(hList, L"", L"");
        ListView_SetBkColor(hList, CLR_PANEL);
        ListView_SetTextBkColor(hList, CLR_PANEL);
        ListView_SetTextColor(hList, CLR_TEXT);
        SendMessage(hList, WM_SETFONT, (WPARAM)g_fontUI, TRUE);

        HWND hHdr = ListView_GetHeader(hList);
        SetWindowTheme(hHdr, L"", L"");
        SendMessage(hHdr, WM_SETFONT, (WPARAM)g_fontUIBold, TRUE);

        LVCOLUMN lvc = {0};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = "File Path"; lvc.cx = 470; ListView_InsertColumn(hList, 0, &lvc);
        lvc.pszText = "Size";      lvc.cx = 110; ListView_InsertColumn(hList, 1, &lvc);
        lvc.pszText = "Type";      lvc.cx = 100; ListView_InsertColumn(hList, 2, &lvc);
        lvc.pszText = "Modified";  lvc.cx = 145; ListView_InsertColumn(hList, 3, &lvc);

        int by = 520;
        CreateModernButton(hwnd, "< Previous",     24,  by, 130, 38, 2008, BK_GHOST);
        CreateModernButton(hwnd, "Next >",         160, by, 130, 38, 2009, BK_GHOST);
        CreateModernButton(hwnd, "Open Location",  310, by, 150, 38, 2002, BK_GHOST);
        CreateModernButton(hwnd, "Refresh",        470, by, 110, 38, 2006, BK_GHOST);
        CreateModernButton(hwnd, "Delete Selected",640, by, 160, 38, 2003, BK_DANGER);
        CreateModernButton(hwnd, "Close",          810, by,  70, 38, 2004, BK_PRIMARY);

        PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(2006, 0), 0);
        break;
    }

    // ------------------------------------------------------------
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wParam;
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, g_brBg);

        // Cyan accent strip under title
        RECT strip = { 24, 78, 80, 80 };
        HBRUSH ab = CreateSolidBrush(CLR_ACCENT);
        FillRect(dc, &strip, ab);
        DeleteObject(ab);

        // Dark panel behind list
        RECT panel = { 16, 104, 864, 506 };
        FillRect(dc, &panel, g_brPanel);
        FrameRect(dc, &panel, g_brBorder);
        return 1;
    }

    // ------------------------------------------------------------
    case WM_CTLCOLORSTATIC: {
        HDC  dc  = (HDC)wParam;
        int  id  = GetDlgCtrlID((HWND)lParam);
        SetBkMode(dc, TRANSPARENT);
        if      (id == 2010) SetTextColor(dc, CLR_TEXT);
        else if (id == 2011) SetTextColor(dc, CLR_ACCENT);
        else if (id == 2007) SetTextColor(dc, CLR_TEXT_DIM);
        else                 SetTextColor(dc, CLR_TEXT);
        return (LRESULT)g_brBg;
    }

    // ------------------------------------------------------------
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;

        if (dis->CtlType == ODT_BUTTON) {
            DrawModernButton(dis);
            return TRUE;
        }

        if (dis->CtlType == ODT_LISTVIEW) {
            HDC  dc       = dis->hDC;
            BOOL selected = (dis->itemState & ODS_SELECTED) != 0;
            BOOL focus    = (dis->itemState & ODS_FOCUS)    != 0;
            HWND hList    = dis->hwndItem;
            RECT row      = dis->rcItem;

            COLORREF bg = (dis->itemID & 1) ? CLR_PANEL_ALT : CLR_PANEL;
            if (selected) bg = CLR_SEL;
            HBRUSH rb = CreateSolidBrush(bg);
            FillRect(dc, &row, rb);
            DeleteObject(rb);

            if (selected) {
                RECT bar = { row.left, row.top, row.left + 3, row.bottom };
                HBRUSH ab = CreateSolidBrush(CLR_ACCENT);
                FillRect(dc, &bar, ab);
                DeleteObject(ab);
            }

            // Draw checkbox only when in selection mode
            if (s_selectionMode) {
                int rh   = row.bottom - row.top;
                int cbSz = 14;
                int cbY  = row.top + (rh - cbSz) / 2;
                RECT cb  = { row.left + 5, cbY,
                             row.left + 5 + cbSz, cbY + cbSz };
                BOOL chk = s_checked[dis->itemID];

                HBRUSH cbFill = CreateSolidBrush(chk ? CLR_ACCENT : CLR_PANEL);
                HPEN   cbPen  = CreatePen(PS_SOLID, 1,
                    chk ? CLR_ACCENT : CLR_BORDER);
                HGDIOBJ ocbB = SelectObject(dc, cbFill);
                HGDIOBJ ocbP = SelectObject(dc, cbPen);
                RoundRect(dc, cb.left, cb.top, cb.right, cb.bottom, 4, 4);
                SelectObject(dc, ocbB);
                SelectObject(dc, ocbP);
                DeleteObject(cbFill);
                DeleteObject(cbPen);

                if (chk) {
                    // Tick mark
                    HPEN tick = CreatePen(PS_SOLID, 2, RGB(10, 14, 18));
                    HGDIOBJ ot = SelectObject(dc, tick);
                    MoveToEx(dc, cb.left + 3,  cb.top + 7,  NULL);
                    LineTo  (dc, cb.left + 6,  cb.top + 10);
                    LineTo  (dc, cb.left + 11, cb.top + 3);
                    SelectObject(dc, ot);
                    DeleteObject(tick);
                }
            }

            int cols = Header_GetItemCount(ListView_GetHeader(hList));
            SetBkMode(dc, TRANSPARENT);
            HGDIOBJ oldFont = SelectObject(dc, g_fontUI);

            for (int c = 0; c < cols; c++) {
                RECT cr;
                ListView_GetSubItemRect(hList, dis->itemID, c, LVIR_BOUNDS, &cr);
                if (c == 0) {
                    RECT hr;
                    Header_GetItemRect(ListView_GetHeader(hList), 0, &hr);
                    cr.right = cr.left + (hr.right - hr.left);
                }
                cr.left += (c == 0 && s_selectionMode) ? 26 : 10; cr.right -= 6;

                char text[1024] = {0};
                LVITEM lvi = {0};
                lvi.iSubItem    = c;
                lvi.cchTextMax  = sizeof(text);
                lvi.pszText     = text;
                SendMessage(hList, LVM_GETITEMTEXT, dis->itemID, (LPARAM)&lvi);

                COLORREF tc = CLR_TEXT;
                if (c == 2) {
                    tc = (strcmp(text, "Trigger") == 0) ? CLR_ACCENT : CLR_TEXT_DIM;
                } else if (c == 1 || c == 3) {
                    tc = CLR_TEXT_DIM;
                }
                SetTextColor(dc, tc);
                DrawText(dc, text, -1, &cr,
                    DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            SelectObject(dc, oldFont);

            if (focus && !selected) {
                HPEN p = CreatePen(PS_DOT, 1, CLR_ACCENT);
                HGDIOBJ op = SelectObject(dc, p);
                HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Rectangle(dc, row.left, row.top, row.right, row.bottom);
                SelectObject(dc, op);
                SelectObject(dc, ob);
                DeleteObject(p);
            }
            return TRUE;
        }
        break;
    }

    // ------------------------------------------------------------
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        // Custom-draw the ListView header for dark theme
        if (nm->idFrom == 0 && nm->code == NM_CUSTOMDRAW) {
            LPNMCUSTOMDRAW cd = (LPNMCUSTOMDRAW)lParam;
            switch (cd->dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    HDC  dc = cd->hdc;
                    RECT r  = cd->rc;
                    HBRUSH b = CreateSolidBrush(CLR_BG);
                    FillRect(dc, &r, b);
                    DeleteObject(b);

                    RECT line = { r.left, r.bottom - 2, r.right, r.bottom };
                    HBRUSH lb = CreateSolidBrush(CLR_BORDER);
                    FillRect(dc, &line, lb);
                    DeleteObject(lb);

                    char text[256] = {0};
                    HD_ITEM hi = {0};
                    hi.mask        = HDI_TEXT;
                    hi.pszText     = text;
                    hi.cchTextMax  = sizeof(text);
                    SendMessage(nm->hwndFrom, HDM_GETITEM, cd->dwItemSpec, (LPARAM)&hi);

                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, CLR_TEXT_DIM);
                    HGDIOBJ of = SelectObject(dc, g_fontUIBold);
                    RECT tr = r; tr.left += 12;
                    DrawText(dc, text, -1, &tr,
                        DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    SelectObject(dc, of);
                    return CDRF_SKIPDEFAULT;
                }
            }
        }

        // Click inside the checkbox column (leftmost 26px) toggles check
        if (nm->idFrom == 2001 && nm->code == NM_CLICK) {
            if (s_selectionMode) {
                LPNMITEMACTIVATE nmia = (LPNMITEMACTIVATE)lParam;
                if (nmia->iItem >= 0 && nmia->ptAction.x < 26) {
                    s_checked[nmia->iItem] = !s_checked[nmia->iItem];
                    // Exit selection mode if nothing is checked any more
                    HWND hList = GetDlgItem(hwnd, 2001);
                    BOOL any   = FALSE;
                    int  n     = ListView_GetItemCount(hList);
                    for (int i = 0; i < n; i++)
                        if (s_checked[i]) { any = TRUE; break; }
                    if (!any) s_selectionMode = FALSE;
                    InvalidateRect(hList, NULL, FALSE);
                }
            }
        }
        break;
    }

    // ------------------------------------------------------------
    case WM_CONTEXTMENU: {
        HWND hList = GetDlgItem(hwnd, 2001);
        if ((HWND)wParam != hList) break;

        // Convert screen coords to ListView client coords for hit-test
        POINT pt   = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
        POINT ptCl = pt;
        ScreenToClient(hList, &ptCl);

        LVHITTESTINFO ht = {0};
        ht.pt = ptCl;
        int item = ListView_HitTest(hList, &ht);
        if (item < 0) break;

        s_ctxItemIdx = item;
        BOOL chk = s_selectionMode && s_checked[item];

        HMENU hCtx = CreatePopupMenu();
        AppendMenu(hCtx, MF_STRING, IDM_CTX_SELECT,
            chk ? "Deselect" : "Select");
        SetForegroundWindow(hwnd);
        TrackPopupMenu(hCtx, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
            pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(hCtx);
        break;
    }

    // ------------------------------------------------------------
    case WM_COMMAND: {
        int id = LOWORD(wParam);

        switch (id) {
        case IDM_CTX_SELECT: {  // Enter/toggle selection mode
            if (s_ctxItemIdx < 0) break;
            HWND hList = GetDlgItem(hwnd, 2001);
            s_selectionMode = TRUE;
            s_checked[s_ctxItemIdx] = !s_checked[s_ctxItemIdx];
            s_ctxItemIdx = -1;
            InvalidateRect(hList, NULL, FALSE);
            break;
        }
        case 2008: {  // Previous group
            EnterCriticalSection(&g_alert_lock);
            g_current_alert_index = FindNextValidGroup(g_current_alert_index, -1);
            LeaveCriticalSection(&g_alert_lock);
            UpdateReportWindow();
            break;
        }
        case 2009: {  // Next group
            EnterCriticalSection(&g_alert_lock);
            g_current_alert_index = FindNextValidGroup(g_current_alert_index, 1);
            LeaveCriticalSection(&g_alert_lock);
            UpdateReportWindow();
            break;
        }
        case 2006:  // Refresh
            EnterCriticalSection(&g_alert_lock);
            CompactAlerts();
            LeaveCriticalSection(&g_alert_lock);
            UpdateReportWindow();
            break;

        case 2002: {  // Open file location
            HWND hList = GetDlgItem(hwnd, 2001);
            int  sel   = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel != -1) {
                char fp[MAX_PATH];
                ListView_GetItemText(hList, sel, 0, fp, MAX_PATH);
                if (FileExists(fp)) {
                    char cmd[MAX_PATH + 50];
                    snprintf(cmd, sizeof(cmd), "/select,\"%s\"", fp);
                    ShellExecute(NULL, "open", "explorer.exe", cmd, NULL, SW_SHOW);
                } else {
                    MessageBox(hwnd, "File no longer exists!", "Error", MB_OK | MB_ICONERROR);
                    UpdateReportWindow();
                }
            } else {
                MessageBox(hwnd, "Please select a file first.", "Info", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }

        case 2003: {  // Delete checked files (multi-select)
            HWND hList = GetDlgItem(hwnd, 2001);
            int  itemCount = ListView_GetItemCount(hList);

            // Collect all checked file paths
            char checkedPaths[MAX_DUPLICATES + 1][MAX_PATH];
            int  checkedCount = 0;
            for (int i = 0; i < itemCount && checkedCount <= MAX_DUPLICATES; i++) {
                if (s_checked[i]) {
                    ListView_GetItemText(hList, i, 0,
                        checkedPaths[checkedCount], MAX_PATH);
                    checkedCount++;
                }
            }

            if (checkedCount == 0) {
                MessageBox(hwnd,
                    "No files selected for deletion.\n\n"
                    "Right-click a file and choose 'Select' to enter\n"
                    "selection mode, then click checkboxes to mark files.",
                    "Nothing Selected", MB_OK | MB_ICONINFORMATION);
                break;
            }

            // Is this a whole-group deletion?
            EnterCriticalSection(&g_alert_lock);
            int totalRemaining = CountRemainingFiles(
                &g_alerts[g_current_alert_index]);
            char triggerCopy[MAX_PATH];
            strncpy(triggerCopy,
                g_alerts[g_current_alert_index].trigger_file.filepath,
                MAX_PATH - 1);
            triggerCopy[MAX_PATH - 1] = '\0';
            LeaveCriticalSection(&g_alert_lock);

            BOOL fullGroup = (checkedCount >= totalRemaining);

            char confirm[600];
            if (fullGroup) {
                snprintf(confirm, sizeof(confirm),
                    "Delete entire group?\n\n"
                    "All %d files with identical content will be moved to\n"
                    "the Recycle Bin and this duplicate group will be removed.",
                    checkedCount);
            } else {
                snprintf(confirm, sizeof(confirm),
                    "Delete %d selected file(s)?\n\n"
                    "They will be moved to the Recycle Bin.",
                    checkedCount);
            }

            if (MessageBox(hwnd, confirm, "Confirm Delete",
                    MB_YESNO | MB_ICONWARNING) != IDYES)
                break;

            // Delete each checked file
            int  failCount      = 0;
            BOOL triggerDeleted = FALSE;

            for (int i = 0; i < checkedCount; i++) {
                if (!FileExists(checkedPaths[i])) continue;
                if (strcmp(checkedPaths[i], triggerCopy) == 0)
                    triggerDeleted = TRUE;

                char from[MAX_PATH + 2];
                memset(from, 0, sizeof(from));
                strncpy(from, checkedPaths[i], MAX_PATH);

                SHFILEOPSTRUCT fo = {0};
                fo.hwnd   = hwnd;
                fo.wFunc  = FO_DELETE;
                fo.pFrom  = from;
                fo.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;

                int res  = SHFileOperation(&fo);
                BOOL ok  = (res == 0 && !fo.fAnyOperationsAborted);
                if (!ok) { Sleep(100); ok = !FileExists(checkedPaths[i]); }
                if (!ok) failCount++;
            }

            // Update group state
            EnterCriticalSection(&g_alert_lock);
            int cur = g_current_alert_index;
            if (fullGroup) {
                RemoveAlertAt(cur);
            } else {
                if (triggerDeleted) PromoteDuplicate(&g_alerts[cur]);
                int remaining = CountRemainingFiles(&g_alerts[cur]);
                if (remaining <= 1) RemoveAlertAt(cur);
                else                g_alerts[cur].files_remaining = remaining;
            }
            LeaveCriticalSection(&g_alert_lock);

            // Result notification
            if (failCount > 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                    "%d file(s) moved to Recycle Bin.\n"
                    "%d file(s) could not be deleted (may be in use).",
                    checkedCount - failCount, failCount);
                MessageBox(hwnd, msg, "Partial Delete", MB_OK | MB_ICONWARNING);
            } else {
                char msg[128];
                snprintf(msg, sizeof(msg),
                    "%d file(s) moved to Recycle Bin.", checkedCount);
                MessageBox(hwnd, msg, "Success", MB_OK | MB_ICONINFORMATION);
            }
            UpdateReportWindow();
            break;
        }

        case 2004:  // Close
            DestroyWindow(hwnd);
            g_hReportWnd = NULL;
            break;
        }
        break;
    }

    // ------------------------------------------------------------
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

// ----------------------------------------------------------------
// Show (or bring to front) the report window
// ----------------------------------------------------------------

void ShowReportWindow(void) {
    if (g_hReportWnd) {
        SetForegroundWindow(g_hReportWnd);
        UpdateReportWindow();
        return;
    }

    EnsureThemeResources();

    WNDCLASSEX wc = {0};
    wc.cbSize       = sizeof(WNDCLASSEX);
    wc.lpfnWndProc  = ReportWndProc;
    wc.hInstance    = GetModuleHandle(NULL);
    wc.hCursor      = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = "DDASReportWindow";

    if (!GetClassInfoEx(GetModuleHandle(NULL), "DDASReportWindow", &wc))
        RegisterClassEx(&wc);

    g_hReportWnd = CreateWindowEx(0,
        "DDASReportWindow", "DDAS - Duplicate File Groups",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 620,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    ApplyDarkTitleBar(g_hReportWnd);
    ShowWindow(g_hReportWnd, SW_SHOW);
    UpdateWindow(g_hReportWnd);
    SetForegroundWindow(g_hReportWnd);
}
