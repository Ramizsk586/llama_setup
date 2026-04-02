/*
 * OpenWebUI Setup Center
 * Modern dark Win32 UI - Redesigned
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IDI_ICON1 101

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

/* ──────────────────────────────────────────────
   Modern Dark Theme Colors (OpenWebUI Style)
   ────────────────────────────────────────────── */
#define C_BG        RGB(20, 20, 20)      // App background - darker
#define C_PANEL     RGB(32, 32, 32)      // Card background
#define C_PANEL2    RGB(45, 45, 45)      // Button hover/secondary
#define C_BORDER    RGB(55, 55, 55)      // Subtle borders
#define C_ACCENT    RGB(0, 180, 180)     // Teal/Cyan Primary (OpenWebUI style)
#define C_ACCENT2   RGB(0, 200, 200)     // Teal Hover
#define C_TEXT      RGB(240, 240, 240)   // Main text
#define C_MUTED     RGB(140, 140, 140)   // Labels and subtext
#define C_INPUT     RGB(40, 40, 40)      // Input boxes - darker
#define C_INPUT_BORDER RGB(70, 70, 70)   // Input border
#define C_SUCCESS   RGB(34, 197, 94)     // Green for status

/* ──────────────────────────────────────────────
   Layout Constants (Compact Style)
   ────────────────────────────────────────────── */
#define APP_W       680
#define APP_H       520
#define CARD_MAX_W  540
#define HEADER_H    70
#define FOOTER_H    20
#define PAD         12

#define MAX_MODELS  256
#define MAX_CMD     4096

/* ──────────────────────────────────────────────
   Control IDs
   ────────────────────────────────────────────── */
enum {
    ID_BTN_SERVER = 1001,
    ID_BTN_FOLDER,
    ID_BTN_PREV,
    ID_BTN_NEXT,
    ID_BTN_GENERATE,
    ID_BTN_COPY
};

/* ──────────────────────────────────────────────
   App state
   ────────────────────────────────────────────── */
static char sServer[MAX_PATH] = "";
static char sFolder[MAX_PATH] = "";
static char sModels[MAX_MODELS][MAX_PATH];
static int  nModels = 0;
static char sCommand[MAX_CMD] = "";

/* ──────────────────────────────────────────────
   Handles
   ────────────────────────────────────────────── */
static HWND hServerEdit, hFolderEdit, hModelCombo;
static HWND hCtxEdit, hGpuEdit, hPortEdit, hThreadsEdit;
static HWND hServerTypeCombo;
static HWND hOutputEdit;
static HWND hBtnServer, hBtnFolder, hBtnPrev, hBtnNext, hBtnGenerate, hBtnCopy;
// NEW: Static controls for labels so they align perfectly
static HWND hLblServer, hLblFolder, hLblModel, hLblCtx, hLblGpu, hLblPort, hLblThreads, hLblServerType;

/* ──────────────────────────────────────────────
   GDI resources
   ────────────────────────────────────────────── */
static HBRUSH brBg, brPanel, brPanel2, brInput;
static HFONT hFontTitle, hFontBody, hFontSmall, hFontBold, hFontLabel;

/* ──────────────────────────────────────────────
   Utility
   ────────────────────────────────────────────── */
static void SetCtlFont(HWND hWnd, HFONT hFont)
{
    if (hWnd) SendMessageA(hWnd, WM_SETFONT, (WPARAM)hFont, FALSE);
}

static void CopyToClipboardA(HWND hwnd, const char *text)
{
    if (!text || !*text) return;
    if (!OpenClipboard(hwnd)) return;

    EmptyClipboard();

    size_t len = strlen(text) + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (hMem) {
        void *p = GlobalLock(hMem);
        if (p) {
            memcpy(p, text, len);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }

    CloseClipboard();
}

static void ShowOnlyOnPage(HWND hwnd)
{
    ShowWindow(hServerEdit,    SW_SHOW);
    ShowWindow(hFolderEdit,    SW_SHOW);
    ShowWindow(hModelCombo,    SW_SHOW);
    ShowWindow(hBtnServer,     SW_SHOW);
    ShowWindow(hBtnFolder,     SW_SHOW);
    ShowWindow(hCtxEdit,       SW_SHOW);
    ShowWindow(hGpuEdit,       SW_SHOW);
    ShowWindow(hPortEdit,      SW_SHOW);
    ShowWindow(hThreadsEdit,   SW_SHOW);
    ShowWindow(hServerTypeCombo, SW_SHOW);
    ShowWindow(hLblServer,     SW_SHOW);
    ShowWindow(hLblFolder,     SW_SHOW);
    ShowWindow(hLblModel,      SW_SHOW);
    ShowWindow(hLblCtx,        SW_SHOW);
    ShowWindow(hLblGpu,        SW_SHOW);
    ShowWindow(hLblPort,       SW_SHOW);
    ShowWindow(hLblThreads,    SW_SHOW);
    ShowWindow(hLblServerType, SW_SHOW);
    
    ShowWindow(hOutputEdit,    SW_HIDE);
    ShowWindow(hBtnGenerate,   SW_HIDE);
    ShowWindow(hBtnCopy,       SW_HIDE);
    ShowWindow(hBtnPrev,       SW_HIDE);
    SetWindowTextA(hBtnNext, "Generate Command");
    InvalidateRect(hwnd, NULL, TRUE);
}

/* ──────────────────────────────────────────────
   Model scanning
   ────────────────────────────────────────────── */
static void ScanModels(void)
{
    nModels = 0;
    SendMessageA(hModelCombo, CB_RESETCONTENT, 0, 0);

    if (!sFolder[0]) return;

    char pattern[MAX_PATH * 2];
    snprintf(pattern, sizeof(pattern), "%s\\*.gguf", sFolder);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (nModels < MAX_MODELS) {
                lstrcpynA(sModels[nModels], fd.cFileName, MAX_PATH);
                SendMessageA(hModelCombo, CB_ADDSTRING, 0, (LPARAM)sModels[nModels]);
                nModels++;
            }
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);

    if (nModels > 0)
        SendMessageA(hModelCombo, CB_SETCURSEL, 0, 0);
}

static void SelectServerFile(HWND hwnd)
{
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ZeroMemory(sServer, sizeof(sServer));

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = sServer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Executable (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = "Select Server Executable";

    if (GetOpenFileNameA(&ofn))
        SetWindowTextA(hServerEdit, sServer);
}

static void SelectFolder(HWND hwnd)
{
    BROWSEINFOA bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hwnd;
    bi.lpszTitle = "Select Model Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return;

    char path[MAX_PATH] = {0};
    if (SHGetPathFromIDListA(pidl, path)) {
        lstrcpynA(sFolder, path, MAX_PATH);
        SetWindowTextA(hFolderEdit, sFolder);
        ScanModels();
    }

    CoTaskMemFree(pidl);
}

static void GenerateCommand(HWND hwnd)
{
    int idx = (int)SendMessageA(hModelCombo, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR) {
        MessageBoxA(hwnd, "No model selected. Browse a model folder first.", "Error", MB_ICONERROR);
        return;
    }

    int serverTypeIdx = (int)SendMessageA(hServerTypeCombo, CB_GETCURSEL, 0, 0);
    char serverType[64] = {0};
    SendMessageA(hServerTypeCombo, CB_GETLBTEXT, (WPARAM)serverTypeIdx, (LPARAM)serverType);

    char model[MAX_PATH] = {0};
    char ctx[32] = {0};
    char gpu[32] = {0};
    char port[32] = {0};
    char threads[32] = {0};

    SendMessageA(hModelCombo, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)model);
    GetWindowTextA(hCtxEdit, ctx, sizeof(ctx));
    GetWindowTextA(hGpuEdit, gpu, sizeof(gpu));
    GetWindowTextA(hPortEdit, port, sizeof(port));
    GetWindowTextA(hThreadsEdit, threads, sizeof(threads));

    if (!sServer[0]) {
        MessageBoxA(hwnd, "Select the server executable first.", "Error", MB_ICONERROR);
        return;
    }

    if (!sFolder[0]) {
        MessageBoxA(hwnd, "Select the model folder first.", "Error", MB_ICONERROR);
        return;
    }

    // Determine host based on server type
    const char *host = (serverTypeIdx == 1) ? "0.0.0.0" : "127.0.0.1";

    snprintf(
        sCommand, sizeof(sCommand),
        "& \"%s\" -m \"%s\\%s\" -c %s -ngl %s --port %s -t %s --host %s",
        sServer,
        sFolder,
        model,
        ctx[0] ? ctx : "2048",
        gpu[0] ? gpu : "-1",
        port[0] ? port : "8000",
        threads[0] ? threads : "4",
        host
    );

    CopyToClipboardA(hwnd, sCommand);
    MessageBoxA(hwnd, "Command copied to clipboard successfully!", "Success", MB_OK | MB_ICONINFORMATION);
}

/* ──────────────────────────────────────────────
   Drawing helpers
   ────────────────────────────────────────────── */
static void DrawLabel(HDC hdc, const char *text, int x, int y, int w, int h, HFONT font, COLORREF color, UINT fmt)
{
    RECT r = {x, y, x + w, y + h};
    HFONT old = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextA(hdc, text, -1, &r, fmt);
    SelectObject(hdc, old);
}

static void DrawHeader(HDC hdc, RECT rc)
{
    SetBkMode(hdc, TRANSPARENT);
    DrawLabel(hdc, "Llama Server Setup", 0, 16, rc.right, 38, hFontTitle, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawLabel(hdc, "Configure paths and copy the launch command to your clipboard.", 0, 50, rc.right, 20, hFontSmall, C_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void DrawCardFrame(HDC hdc, RECT rc)
{
    int cardW = (CARD_MAX_W < (rc.right - 2 * PAD)) ? CARD_MAX_W : (rc.right - 2 * PAD);
    if (cardW < 440) cardW = rc.right - 2 * PAD;
    int cardH = rc.bottom - HEADER_H - FOOTER_H - 12;
    if (cardH < 300) cardH = 300;

    int cardX = (rc.right - cardW) / 2;
    int cardY = HEADER_H + 12;

    HBRUSH b = CreateSolidBrush(C_PANEL);
    HPEN p = CreatePen(PS_SOLID, 1, C_BORDER);
    
    HBRUSH oldB = (HBRUSH)SelectObject(hdc, b);
    HPEN oldP = (HPEN)SelectObject(hdc, p);

    RoundRect(hdc, cardX, cardY, cardX + cardW, cardY + cardH, 10, 10);

    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);

    DeleteObject(p);
    DeleteObject(b);
}

/* ──────────────────────────────────────────────
   Layout
   ────────────────────────────────────────────── */
static void LayoutControls(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);

    int W = rc.right;
    int H = rc.bottom;

    int cardW = (CARD_MAX_W < (W - 2 * PAD)) ? CARD_MAX_W : (W - 2 * PAD);
    if (cardW < 440) cardW = W - 2 * PAD;
    int cardH = H - HEADER_H - FOOTER_H - 12;

    int cardX = (W - cardW) / 2;
    int cardY = HEADER_H + 12;

    int innerX = cardX + 28;
    int innerW = cardW - 56;
    int rowH = 24;      
    int labelH = 16;    
    int gapY = 8;
    int colW = (innerW - 8) / 2;  // Two columns with small gap
    int y = cardY + 18; 

    // Server Type (new option)
    MoveWindow(hLblServerType, innerX, y, innerW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hServerTypeCombo, innerX, y, innerW, 100, TRUE);
    y += rowH + gapY + 4;

    // Server Executable
    MoveWindow(hLblServer, innerX, y, innerW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hServerEdit, innerX, y, innerW - 85, rowH, TRUE);
    MoveWindow(hBtnServer,  innerX + innerW - 78, y - 1, 78, rowH + 2, TRUE);
    y += rowH + gapY;

    // Model Folder
    MoveWindow(hLblFolder, innerX, y, innerW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hFolderEdit, innerX, y, innerW - 85, rowH, TRUE);
    MoveWindow(hBtnFolder,  innerX + innerW - 78, y - 1, 78, rowH + 2, TRUE);
    y += rowH + gapY;

    // Model Combo (Height is dropdown height)
    MoveWindow(hLblModel, innerX, y, innerW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hModelCombo, innerX, y, innerW, 100, TRUE);
    y += rowH + gapY + 4;

    // Parameters in 2-column grid
    // Left column: Context Size
    MoveWindow(hLblCtx, innerX, y, colW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hCtxEdit, innerX, y, colW, rowH, TRUE);
    
    // Right column: GPU Layers (at same y level)
    int rightColX = innerX + colW + 8;
    MoveWindow(hLblGpu, rightColX, y - labelH - 2, colW, labelH, TRUE);
    MoveWindow(hGpuEdit, rightColX, y, colW, rowH, TRUE);
    y += rowH + gapY;

    // Left column: Port
    MoveWindow(hLblPort, innerX, y, colW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hPortEdit, innerX, y, colW, rowH, TRUE);
    
    // Right column: Threads (at same y level)
    MoveWindow(hLblThreads, rightColX, y - labelH - 2, colW, labelH, TRUE);
    MoveWindow(hThreadsEdit, rightColX, y, colW, rowH, TRUE);
    y += rowH + 8;

    // Generate button - positioned tightly
    MoveWindow(hBtnNext, innerX, y, innerW, 32, TRUE);
}

/* ──────────────────────────────────────────────
   Painting
   ────────────────────────────────────────────── */
static void OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);

    FillRect(hdc, &rc, brBg);

    DrawHeader(hdc, rc);
    DrawCardFrame(hdc, rc);
    
    // Labels are now drawn via standard STATIC controls to ensure 
    // mathematically perfect alignment with the layout engine.

    EndPaint(hwnd, &ps);
}

/* ──────────────────────────────────────────────
   Control creation
   ────────────────────────────────────────────── */
static void CreateControls(HWND hwnd)
{
    HINSTANCE hi = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

    // Labels
    hLblServerType = CreateWindowA("STATIC", "Server Type", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblServer = CreateWindowA("STATIC", "Server Executable File", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblFolder = CreateWindowA("STATIC", "Models Directory", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblModel  = CreateWindowA("STATIC", "Target Model", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblCtx    = CreateWindowA("STATIC", "Context Size", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblGpu    = CreateWindowA("STATIC", "GPU Layers", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblPort   = CreateWindowA("STATIC", "Port", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblThreads = CreateWindowA("STATIC", "Threads", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);

    // Server Type Combo
    hServerTypeCombo = CreateWindowA("COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);
    SendMessageA(hServerTypeCombo, CB_ADDSTRING, 0, (LPARAM)"Local - same device only");
    SendMessageA(hServerTypeCombo, CB_ADDSTRING, 0, (LPARAM)"LAN/IP - same Wi-Fi network");
    SendMessageA(hServerTypeCombo, CB_SETCURSEL, 0, 0);

    // Inputs
    hServerEdit = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hBtnServer = CreateWindowA("BUTTON", "Browse",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_SERVER, hi, NULL);

    hFolderEdit = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hBtnFolder = CreateWindowA("BUTTON", "Browse",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_FOLDER, hi, NULL);

    hModelCombo = CreateWindowA("COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hCtxEdit = CreateWindowA("EDIT", "2048",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hGpuEdit = CreateWindowA("EDIT", "-1",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hPortEdit = CreateWindowA("EDIT", "8000",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hThreadsEdit = CreateWindowA("EDIT", "4",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    /* Output controls */
    hOutputEdit = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hBtnPrev = CreateWindowA("BUTTON", "Previous",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_PREV, hi, NULL);

    hBtnNext = CreateWindowA("BUTTON", "Generate Command",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_NEXT, hi, NULL);

    hBtnGenerate = CreateWindowA("BUTTON", "Generate",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_GENERATE, hi, NULL);

    hBtnCopy = CreateWindowA("BUTTON", "Copy",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_COPY, hi, NULL);

    // Apply Fonts
    HWND ctrls[] = { hServerTypeCombo, hServerEdit, hFolderEdit, hModelCombo, hCtxEdit, hGpuEdit, hPortEdit, hThreadsEdit, hOutputEdit };
    for (int i = 0; i < (int)(sizeof(ctrls) / sizeof(ctrls[0])); i++) SetCtlFont(ctrls[i], hFontBody);

    HWND labels[] = { hLblServerType, hLblServer, hLblFolder, hLblModel, hLblCtx, hLblGpu, hLblPort, hLblThreads };
    for (int i = 0; i < (int)(sizeof(labels) / sizeof(labels[0])); i++) SetCtlFont(labels[i], hFontLabel);

    SetCtlFont(hBtnServer, hFontBody);
    SetCtlFont(hBtnFolder, hFontBody);
    SetCtlFont(hBtnNext, hFontBold);

    ShowOnlyOnPage(hwnd);
    LayoutControls(hwnd);
}

/* ──────────────────────────────────────────────
   Owner-draw button rendering
   ────────────────────────────────────────────── */
static BOOL OnDrawItem(LPARAM lp)
{
    DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
    if (!di || di->CtlType != ODT_BUTTON) return FALSE;

    int id = di->CtlID;
    BOOL pressed = (di->itemState & ODS_SELECTED) ? TRUE : FALSE;
    BOOL primary = (id == ID_BTN_NEXT);

    RECT r = di->rcItem;
    HDC hdc = di->hDC;

    COLORREF bg, border, txt;

    if (primary) {
        // Teal Primary Button
        bg = pressed ? C_ACCENT2 : C_ACCENT;
        border = bg;
        txt = RGB(255, 255, 255);
    } else {
        // Secondary Buttons - more compact
        bg = pressed ? RGB(55, 55, 55) : RGB(50, 50, 50);
        border = C_INPUT_BORDER;
        txt = C_TEXT;
    }

    HBRUSH b = CreateSolidBrush(bg);
    FillRect(hdc, &r, b);
    DeleteObject(b);

    HPEN p = CreatePen(PS_SOLID, 1, border);
    HPEN oldP = (HPEN)SelectObject(hdc, p);
    HBRUSH oldB = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, 5, 5);
    
    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);
    DeleteObject(p);

    char label[64] = {0};
    GetWindowTextA(di->hwndItem, label, sizeof(label));

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, txt);
    HFONT oldF = (HFONT)SelectObject(hdc, primary ? hFontBold : hFontBody);
    DrawTextA(hdc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldF);

    return TRUE;
}

/* ──────────────────────────────────────────────
   Window procedure
   ────────────────────────────────────────────── */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        {
            brBg     = CreateSolidBrush(C_BG);
            brPanel  = CreateSolidBrush(C_PANEL);
            brPanel2 = CreateSolidBrush(C_PANEL2);
            brInput  = CreateSolidBrush(C_INPUT);

            hFontTitle = CreateFontA(24, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontBody  = CreateFontA(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontSmall = CreateFontA(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontBold  = CreateFontA(15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontLabel = CreateFontA(14, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

            CreateControls(hwnd);
        }
        return 0;

    case WM_SIZE:
        LayoutControls(hwnd);
        ShowOnlyOnPage(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        OnPaint(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BTN_SERVER:   SelectServerFile(hwnd); break;
        case ID_BTN_FOLDER:   SelectFolder(hwnd); break;
        case ID_BTN_NEXT:     GenerateCommand(hwnd); break;
        }
        return 0;

    case WM_DRAWITEM:
        return OnDrawItem(lp);

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, C_TEXT);
            SetBkColor(hdc, C_INPUT);
            
            // Draw a subtle accent border effect for inputs
            HWND hCtl = (HWND)lp;
            if (hCtl == hServerEdit || hCtl == hFolderEdit || hCtl == hCtxEdit || hCtl == hModelCombo) {
                // Input field border hint
            }
            return (LRESULT)brInput;
        }

    case WM_CTLCOLORSTATIC:
        {
            HDC hdc = (HDC)wp;
            HWND hCtl = (HWND)lp;

            // Labels inside the card get the Panel background color
            if (hCtl == hLblServerType || hCtl == hLblServer || hCtl == hLblFolder || hCtl == hLblModel || hCtl == hLblCtx || 
                hCtl == hLblGpu || hCtl == hLblPort || hCtl == hLblThreads) {
                SetTextColor(hdc, C_MUTED);
                SetBkColor(hdc, C_PANEL);
                return (LRESULT)brPanel;
            }

            // Other generic statics get the App background
            SetTextColor(hdc, C_MUTED);
            SetBkColor(hdc, C_BG);
            return (LRESULT)brBg;
        }

    case WM_DESTROY:
        if (brBg) DeleteObject(brBg);
        if (brPanel) DeleteObject(brPanel);
        if (brPanel2) DeleteObject(brPanel2);
        if (brInput) DeleteObject(brInput);

        if (hFontTitle) DeleteObject(hFontTitle);
        if (hFontBody) DeleteObject(hFontBody);
        if (hFontSmall) DeleteObject(hFontSmall);
        if (hFontBold) DeleteObject(hFontBold);
        if (hFontLabel) DeleteObject(hFontLabel);

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* ──────────────────────────────────────────────
   Entry point
   ────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hPrev;
    (void)lpCmd;

    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(IDI_ICON1));
    wc.hIconSm = LoadIconA(hInst, MAKEINTRESOURCEA(IDI_ICON1));
    wc.lpszClassName = "LlamaSetupRedesign";
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;

    if (!RegisterClassExA(&wc))
        return 0;

    RECT wr = {0, 0, APP_W, APP_H};
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;

    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowExA(
        0, "LlamaSetupRedesign", "Llama Server Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (sx - winW) / 2, (sy - winH) / 2, winW, winH,
        NULL, NULL, hInst, NULL
    );

    if (!hwnd) return 0;

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}