/*
 * RandomChar IME - A Windows input method that replaces every typed
 * character with a random character of the same category.
 *
 *   Uppercase letter -> random uppercase letter
 *   Lowercase letter -> random lowercase letter
 *   Digit            -> random digit
 *   Symbol           -> random symbol
 *
 * Caps Lock and Num Lock are respected.
 * Ctrl / Alt / Win modifier combinations pass through unchanged.
 *
 * Build: CMake + MSVC (or MinGW-w64).
 */

#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>

#define APP_NAME L"RandomChar IME"
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_ENABLE    1001
#define ID_TRAY_AUTOSTART 1002
#define ID_TRAY_ABOUT     1003
#define ID_TRAY_EXIT      1004

static HHOOK  g_hHook    = NULL;
static bool   g_bEnabled = true;
static HWND   g_hWnd     = NULL;
static NOTIFYICONDATAW g_nid;

/* ------------------------------------------------------------------ */
/*  Random character generators                                       */
/* ------------------------------------------------------------------ */

static wchar_t RandUpper(void)
{
    return (wchar_t)(L'A' + rand() % 26);
}

static wchar_t RandLower(void)
{
    return (wchar_t)(L'a' + rand() % 26);
}

static wchar_t RandDigit(void)
{
    return (wchar_t)(L'0' + rand() % 10);
}

static wchar_t RandSymbol(void)
{
    /* 32 printable ASCII symbols (excluding space and alphanumerics) */
    static const wchar_t s[] = L"!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    return s[rand() % (sizeof(s) / sizeof(wchar_t) - 1)];
}

/* ------------------------------------------------------------------ */
/*  Send a Unicode character as synthesized keyboard input           */
/* ------------------------------------------------------------------ */

static void SendUnicode(wchar_t ch)
{
    INPUT in[2];
    ZeroMemory(in, sizeof(in));

    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wScan   = (WORD)ch;
    in[0].ki.dwFlags = KEYEVENTF_UNICODE;

    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wScan   = (WORD)ch;
    in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

    SendInput(2, in, sizeof(INPUT));
}

/* ------------------------------------------------------------------ */
/*  Key-state helpers                                                 */
/* ------------------------------------------------------------------ */

static bool IsDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static bool IsToggleOn(int vk)
{
    return (GetKeyState(vk) & 0x0001) != 0;
}

/* ------------------------------------------------------------------ */
/*  Is this a printable-character key we should randomize?           */
/* ------------------------------------------------------------------ */

static bool IsCharKey(int vk)
{
    if (vk >= 'A' && vk <= 'Z')           return true;
    if (vk >= '0' && vk <= '9')           return true;
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return true;

    switch (vk) {
    case VK_OEM_3:      /* ` ~  */
    case VK_OEM_MINUS:  /* - _  */
    case VK_OEM_PLUS:   /* = +  */
    case VK_OEM_4:      /* [ {  */
    case VK_OEM_5:      /* \ |  */
    case VK_OEM_6:      /* ] }  */
    case VK_OEM_1:      /* ; :  */
    case VK_OEM_7:      /* ' "  */
    case VK_OEM_COMMA:  /* , <  */
    case VK_OEM_PERIOD: /* . >  */
    case VK_OEM_2:      /* / ?  */
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/*  Low-level keyboard hook procedure                                 */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK KbdHook(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && g_bEnabled) {
        KBDLLHOOKSTRUCT *p = (KBDLLHOOKSTRUCT *)lParam;

        /* Never re-process our own injected characters (prevents
         * an infinite loop when SendInput re-enters the hook). */
        if (p->flags & LLKHF_INJECTED)
            return CallNextHookEx(g_hHook, nCode, wParam, lParam);

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            int vk = (int)p->vkCode;

            /* Pass through when Ctrl / Alt / Win is held so that
             * all system and application shortcuts work normally. */
            if (IsDown(VK_CONTROL) || IsDown(VK_MENU) ||
                IsDown(VK_LWIN)  || IsDown(VK_RWIN))
                return CallNextHookEx(g_hHook, nCode, wParam, lParam);

            if (!IsCharKey(vk))
                return CallNextHookEx(g_hHook, nCode, wParam, lParam);

            wchar_t ch = 0;

            if (vk >= 'A' && vk <= 'Z') {
                /* Effective case = Shift XOR CapsLock */
                bool upper = IsDown(VK_SHIFT) ^ IsToggleOn(VK_CAPITAL);
                ch = upper ? RandUpper() : RandLower();
            }
            else if (vk >= '0' && vk <= '9') {
                /* Top-row digits: Shift turns them into symbols */
                ch = IsDown(VK_SHIFT) ? RandSymbol() : RandDigit();
            }
            else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
                /* Numpad: only produces digits when NumLock is on;
                 * otherwise it is a navigation cluster (pass through). */
                if (!IsToggleOn(VK_NUMLOCK))
                    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
                ch = RandDigit();
            }
            else {
                /* OEM symbol keys (shifted or unshifted = still a symbol) */
                ch = RandSymbol();
            }

            if (ch != 0) {
                SendUnicode(ch);
                return 1;  /* swallow the original keystroke */
            }
        }
    }
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/*  Auto-start (registry "Run" key) helpers                          */
/* ------------------------------------------------------------------ */

static bool IsAutoStart(void)
{
    HKEY hKey;
    bool found = false;

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH];
        DWORD sz = sizeof(buf);
        found = (RegQueryValueExW(hKey, APP_NAME, NULL, NULL,
                                   (LPBYTE)buf, &sz) == ERROR_SUCCESS);
        RegCloseKey(hKey);
    }
    return found;
}

static void ToggleAutoStart(void)
{
    HKEY hKey;

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return;

    if (IsAutoStart()) {
        RegDeleteValueW(hKey, APP_NAME);
    } else {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (LPBYTE)path,
                       (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(hKey);
}

/* ------------------------------------------------------------------ */
/*  Tray icon                                                         */
/* ------------------------------------------------------------------ */

static void UpdateTrayTip(void)
{
    g_nid.szTip[0] = L'\0';
    wsprintfW(g_nid.szTip, L"%s - %s", APP_NAME,
              g_bEnabled ? L"Enabled" : L"Disabled");
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void ShowTrayMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();

    AppendMenuW(hMenu, MF_STRING | (g_bEnabled ? MF_CHECKED : 0),
                ID_TRAY_ENABLE, L"Enable Randomization");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING | (IsAutoStart() ? MF_CHECKED : 0),
                ID_TRAY_AUTOSTART, L"Start with Windows");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_ABOUT, L"About");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT,  L"Exit");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

/* ------------------------------------------------------------------ */
/*  Window procedure (message-only window for tray callbacks)        */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
            ShowTrayMenu(hWnd);
        else if (lParam == WM_LBUTTONDBLCLK) {
            g_bEnabled = !g_bEnabled;
            UpdateTrayTip();
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_ENABLE:
            g_bEnabled = !g_bEnabled;
            UpdateTrayTip();
            break;
        case ID_TRAY_AUTOSTART:
            ToggleAutoStart();
            break;
        case ID_TRAY_ABOUT:
            MessageBoxW(hWnd,
                L"RandomChar IME\r\n\r\n"
                L"Replaces every character you type with a random character "
                L"of the same category.\r\n\r\n"
                L"  - Uppercase letter -> random uppercase letter\r\n"
                L"  - Lowercase letter -> random lowercase letter\r\n"
                L"  - Digit            -> random digit\r\n"
                L"  - Symbol           -> random symbol\r\n\r\n"
                L"Caps Lock and Num Lock are respected.\r\n"
                L"Ctrl / Alt / Win shortcuts pass through normally.\r\n\r\n"
                L"Double-click the tray icon to toggle on/off.",
                L"About RandomChar IME", MB_OK | MB_ICONINFORMATION);
            break;
        case ID_TRAY_EXIT:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_hHook) UnhookWindowsHookEx(g_hHook);
            PostQuitMessage(0);
            break;
        }
        break;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (g_hHook) UnhookWindowsHookEx(g_hHook);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Entry point                                                       */
/* ------------------------------------------------------------------ */

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    (void)hPrev;
    (void)cmd;
    (void)show;

    srand((unsigned)time(NULL));

    /* --- Single-instance mutex --- */
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\RandomCharIME_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"RandomChar IME is already running.",
                    APP_NAME, MB_OK | MB_ICONWARNING);
        CloseHandle(hMutex);
        return 0;
    }

    /* --- Register a message-only window class --- */
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"RandomCharIME_Wnd";
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(0, L"RandomCharIME_Wnd", APP_NAME,
                              0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);

    /* --- Install the low-level keyboard hook --- */
    g_hHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbdHook, hInst, 0);
    if (!g_hHook) {
        MessageBoxW(NULL, L"Failed to install keyboard hook.",
                    APP_NAME, MB_OK | MB_ICONERROR);
        return 1;
    }

    /* --- Add tray icon --- */
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd             = g_hWnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIconW(NULL, IDI_SHIELD);
    wsprintfW(g_nid.szTip, L"%s - Enabled", APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    /* --- Message loop --- */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(hMutex);
    return (int)msg.wParam;
}
