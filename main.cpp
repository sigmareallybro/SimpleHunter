#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <sstream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

// ID elements for controlling
#define ID_EDIT_INPUT       101
#define ID_BTN_SEARCH       102
#define ID_LIST_RESULTS     103
#define ID_COMBO_EXT        104
#define ID_BTN_FILTERS      105
#define ID_BTN_STARTUP      106
#define ID_CHECK_GREP       107
#define ID_BTN_THEME        108
#define ID_STATUS_BAR       109

#define ID_MENU_OPEN        201
#define ID_MENU_COPY        202
#define ID_MENU_DELETE      203

#define WM_SEARCH_RESULT    (WM_APP + 1)
#define WM_SEARCH_FINISHED  (WM_APP + 2)
#define WM_UPDATE_STATUS    (WM_APP + 3)

// Global variables Interface
HWND g_hwnd = nullptr;
HWND g_edit = nullptr;
HWND g_list = nullptr;
HWND g_combo = nullptr;
HWND g_filterBtn = nullptr;
HWND g_startBtn = nullptr;
HWND g_searchBtn = nullptr;
HWND g_checkGrep = nullptr;
HWND g_themeBtn = nullptr;
HWND g_statusBar = nullptr;
HFONT g_font = nullptr;

// Setting search & state
std::atomic<bool> g_cancelSearch{ false };
std::atomic<bool> g_isSearching{ false };
std::atomic<long> g_foundCount{ 0 };
std::thread g_workerThread;
std::vector<std::wstring> g_resultsCache;
bool g_searchRegistry = false; 
bool g_isStartupView = false;
bool g_isFilterView = false;
bool g_isDarkTheme = false;

// Brushes for custom theme
HBRUSH g_hbrBkgDark = CreateSolidBrush(RGB(30, 30, 30));
HBRUSH g_hbrBkgLight = CreateSolidBrush(RGB(240, 240, 240));
HBRUSH g_hbrEditDark = CreateSolidBrush(RGB(45, 45, 45));
HBRUSH g_hbrEditLight = CreateSolidBrush(RGB(255, 255, 255));

// Forward declarations
void UpdateStatusText(const std::wstring& text);

// Checking Extension
bool CheckExtension(const std::wstring& fileName, int filterIndex) {
    if (filterIndex == 0) return true; // All

    size_t dot = fileName.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;

    std::wstring ext = fileName.substr(dot);
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));

    switch (filterIndex) {
    case 1: // Archives
        return (ext == L".zip" || ext == L".rar" || ext == L".7z");
    case 2: // Documents
        return (ext == L".txt" || ext == L".json");
    case 3: // Execute
        return (ext == L".exe" || ext == L".bat" || ext == L".ps");
    case 4: // Programming
        return (ext == L".cpp" || ext == L".python" || ext == L".c" || ext == L".asm" || 
                ext == L".nasm" || ext == L".cs" || ext == L".js" || ext == L".html" || ext == L".css");
    default: return true;
    }
}

// Checking protected paths
bool IsProtectedPath(const std::wstring& path) {
    std::wstring lowPath = path;
    for (auto& c : lowPath) c = static_cast<wchar_t>(towlower(c));

    if (lowPath.find(L"\\windows") != std::wstring::npos ||
        lowPath.find(L"\\program files") != std::wstring::npos ||
        lowPath.find(L"\\program files (x86)") != std::wstring::npos ||
        lowPath.find(L"\\programdata") != std::wstring::npos ||
        lowPath.find(L"\\system volume information") != std::wstring::npos) {
        return true;
    }
    return false;
}

// Grep: Search text inside file
bool SearchTextInFile(const std::wstring& filePath, const std::wstring& target) {
    std::wifstream file(filePath.c_str());
    if (!file.is_open()) return false;

    std::wstring line;
    while (std::getline(file, line)) {
        std::wstring lowLine = line;
        for (auto& c : lowLine) c = towlower(c);
        if (lowLine.find(target) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

// Recursive file search
void SearchFilesRecursive(HWND hwnd, const std::wstring& directory, const std::wstring& target, int filterIndex, bool grepMode) {
    if (g_cancelSearch.load()) return;
    if (IsProtectedPath(directory)) return;

    std::wstring query = directory + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(query.c_str(), &fd);

    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (g_cancelSearch.load()) break;

        std::wstring fn = fd.cFileName;
        if (fn == L".." || fn == L".") continue;

        std::wstring fp = directory + L"\\" + fn;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                SearchFilesRecursive(hwnd, fp, target, filterIndex, grepMode);
            }
        }
        else {
            std::wstring lowFn = fn;
            for (auto& c : lowFn) c = static_cast<wchar_t>(towlower(c));

            bool matched = false;
            if (grepMode) {
                if (CheckExtension(fn, filterIndex) && SearchTextInFile(fp, target)) {
                    matched = true;
                }
            } else {
                if (lowFn.find(target) != std::wstring::npos && CheckExtension(fn, filterIndex)) {
                    matched = true;
                }
            }

            if (matched) {
                std::wstring* resultPath = new std::wstring(fp);
                PostMessageW(hwnd, WM_SEARCH_RESULT, 0, reinterpret_cast<LPARAM>(resultPath));
                g_foundCount++;
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// Search all disks
void SearchAllDisks(HWND hwnd, const std::wstring& target, int filterIndex, bool grepMode) {
    wchar_t drives[256];
    DWORD len = GetLogicalDriveStringsW(256, drives);
    if (len == 0) return;

    wchar_t* pDrive = drives;
    while (*pDrive && !g_cancelSearch.load()) {
        UINT type = GetDriveTypeW(pDrive);
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
            std::wstring root = pDrive;
            if (!root.empty() && root.back() == L'\\') {
                root.pop_back();
            }
            UpdateStatusText(L"Scanning drive: " + root);
            SearchFilesRecursive(hwnd, root, target, filterIndex, grepMode);
        }
        pDrive += wcslen(pDrive) + 1;
    }
}

// Search Registry files/values
void SearchRegistryRecursive(HWND hwnd, HKEY hKey, const std::wstring& keyPath, const std::wstring& target) {
    if (g_cancelSearch.load()) return;

    DWORD subKeyCount = 0, maxSubKeyLen = 0, valueCount = 0, maxValueNameLen = 0, maxValueDataLen = 0;
    if (RegQueryInfoKeyW(hKey, NULL, NULL, NULL, &subKeyCount, &maxSubKeyLen, NULL, &valueCount, &maxValueNameLen, &maxValueDataLen, NULL, NULL) == ERROR_SUCCESS) {

        std::vector<wchar_t> valName(maxValueNameLen + 1);
        std::vector<BYTE> valData(maxValueDataLen + 1);

        for (DWORD i = 0; i < valueCount; ++i) {
            if (g_cancelSearch.load()) return;
            DWORD nameLen = (DWORD)valName.size();
            DWORD dataLen = (DWORD)valData.size();
            DWORD type = 0;

            if (RegEnumValueW(hKey, i, valName.data(), &nameLen, NULL, &type, valData.data(), &dataLen) == ERROR_SUCCESS) {
                std::wstring name(valName.data(), nameLen);
                std::wstring dataStr = L"";
                if (type == REG_SZ || type == REG_EXPAND_SZ) {
                    dataStr = reinterpret_cast<wchar_t*>(valData.data());
                }

                std::wstring lowName = name;
                std::wstring lowData = dataStr;
                for (auto& c : lowName) c = towlower(c);
                for (auto& c : lowData) c = towlower(c);

                if (lowName.find(target) != std::wstring::npos || lowData.find(target) != std::wstring::npos) {
                    std::wstring result = keyPath + L"\\" + name + L" = " + dataStr;
                    std::wstring* pRes = new std::wstring(result);
                    PostMessageW(hwnd, WM_SEARCH_RESULT, 0, reinterpret_cast<LPARAM>(pRes));
                    g_foundCount++;
                }
            }
        }

        std::vector<wchar_t> subKeyName(maxSubKeyLen + 1);
        for (DWORD i = 0; i < subKeyCount; ++i) {
            if (g_cancelSearch.load()) return;
            DWORD nameLen = (DWORD)subKeyName.size();
            if (RegEnumKeyExW(hKey, i, subKeyName.data(), &nameLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                std::wstring subName(subKeyName.data(), nameLen);
                HKEY hSubKey = NULL;
                if (RegOpenKeyExW(hKey, subName.c_str(), 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                    SearchRegistryRecursive(hwnd, hSubKey, keyPath + L"\\" + subName, target);
                    RegCloseKey(hSubKey);
                }
            }
        }
    }
}

void SearchRegistry(HWND hwnd, const std::wstring& target) {
    HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    const wchar_t* rootNames[] = { L"HKEY_CURRENT_USER", L"HKEY_LOCAL_MACHINE" };

    for (int i = 0; i < 2; ++i) {
        if (g_cancelSearch.load()) return;
        HKEY hKey = NULL;
        if (RegOpenKeyExW(roots[i], L"", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            UpdateStatusText(L"Scanning Registry: " + std::wstring(rootNames[i]));
            SearchRegistryRecursive(hwnd, hKey, rootNames[i], target);
            RegCloseKey(hKey);
        }
    }
}

void ShowFilterOptions(HWND listbox) {
    g_isFilterView = true;
    g_isStartupView = false;
    SendMessageW(listbox, LB_RESETCONTENT, 0, 0);
    g_resultsCache.clear();

    SendMessageW(listbox, LB_ADDSTRING, 0, (LPARAM)L"Disk (Scan across all local drives)");
    SendMessageW(listbox, LB_ADDSTRING, 0, (LPARAM)L"Registry (Search in Windows Registry)");
    SendMessageW(listbox, LB_SETCURSEL, g_searchRegistry ? 1 : 0, 0);
    UpdateStatusText(L"Viewing Search Source Settings.");
}

void ShowStartupItems(HWND listbox) {
    g_isStartupView = true;
    g_isFilterView = false;
    SendMessageW(listbox, LB_RESETCONTENT, 0, 0);
    g_resultsCache.clear();

    HKEY hKey;
    LPCWSTR runs[] = {
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"
    };
    HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };

    int count = 0;
    for (int r = 0; r < 2; r++) {
        for (int i = 0; i < 2; i++) {
            if (RegOpenKeyExW(roots[r], runs[i], 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                wchar_t name[256], data[MAX_PATH];
                DWORD nSize, dSize, type;
                for (int j = 0; ; j++) {
                    nSize = 256; dSize = MAX_PATH;
                    if (RegEnumValueW(hKey, j, name, &nSize, NULL, &type, (LPBYTE)data, &dSize) != ERROR_SUCCESS) break;

                    std::wstring entry = (roots[r] == HKEY_LOCAL_MACHINE ? L"[System] " : L"[User] ")
                        + std::wstring(name) + L" -> " + data;

                    SendMessageW(listbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.c_str()));
                    g_resultsCache.push_back(entry);
                    count++;
                }
                RegCloseKey(hKey);
            }
        }
    }
    UpdateStatusText(L"Loaded startup items: " + std::to_wstring(count));
}

bool DeleteStartupItem(const std::wstring& entryStr) {
    bool isSystem = (entryStr.find(L"[System]") == 0);
    size_t nameStart = entryStr.find(L"] ") + 2;
    size_t arrowPos = entryStr.find(L" -> ");
    if (nameStart == std::wstring::npos || arrowPos == std::wstring::npos) return false;

    std::wstring valName = entryStr.substr(nameStart, arrowPos - nameStart);
    HKEY hRoot = isSystem ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    
    LPCWSTR runPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY hKey;
    if (RegOpenKeyExW(hRoot, runPath, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        LONG res = RegDeleteValueW(hKey, valName.c_str());
        RegCloseKey(hKey);
        if (res == ERROR_SUCCESS) return true;
    }

    runPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
    if (RegOpenKeyExW(hRoot, runPath, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        LONG res = RegDeleteValueW(hKey, valName.c_str());
        RegCloseKey(hKey);
        if (res == ERROR_SUCCESS) return true;
    }

    return false;
}

void CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (hMem) {
        memcpy(GlobalLock(hMem), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
}

void ShowContextMenu(HWND hwnd, LPARAM lParam) {
    if (g_isFilterView) return;

    POINT pt = { LOWORD(lParam), HIWORD(lParam) };
    POINT ptClient = pt;
    ScreenToClient(g_list, &ptClient);

    LRESULT itemIndex = SendMessageW(g_list, LB_ITEMFROMPOINT, 0, MAKELPARAM(ptClient.x, ptClient.y));
    if (HIWORD(itemIndex) == 0) {
        int index = LOWORD(itemIndex);
        SendMessageW(g_list, LB_SETCURSEL, index, 0);

        HMENU hMenu = CreatePopupMenu();
        if (!g_isStartupView) {
            AppendMenuW(hMenu, MF_STRING, ID_MENU_OPEN, L"Locate in Explorer");
        }
        AppendMenuW(hMenu, MF_STRING, ID_MENU_COPY, L"Copy Path / Data");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, ID_MENU_DELETE, g_isStartupView ? L"Remove from Startup" : L"Delete");

        TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);
    }
}

void UpdateStatusText(const std::wstring& text) {
    if (g_statusBar) {
        SendMessageW(g_statusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
}

void UpdateCountText() {
    std::wstring countStr = L"Found: " + std::to_wstring(g_resultsCache.size());
    if (g_statusBar) {
        SendMessageW(g_statusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(countStr.c_str()));
    }
}

void LayoutControls(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    MoveWindow(g_edit, 10, 30, w - 390, 25, TRUE);
    MoveWindow(g_checkGrep, w - 370, 32, 100, 23, TRUE);
    MoveWindow(g_combo, w - 265, 30, 110, 300, TRUE);
    MoveWindow(g_searchBtn, w - 150, 30, 140, 25, TRUE);
    
    MoveWindow(g_list, 10, 70, w - 20, h - 170, TRUE);
    MoveWindow(g_filterBtn, 10, h - 90, (w - 30) / 3, 25, TRUE);
    MoveWindow(g_startBtn, 20 + (w - 30) / 3, h - 90, (w - 30) / 3, 25, TRUE);
    MoveWindow(g_themeBtn, 30 + 2 * ((w - 30) / 3), h - 90, (w - 30) / 3, 25, TRUE);

    SendMessage(g_statusBar, WM_SIZE, 0, 0);
}

// Custom owner-draw button for hover effects
LRESULT CALLBACK ButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    static bool hovered = false;
    switch (uMsg) {
    case WM_MOUSEMOVE:
        if (!hovered) {
            hovered = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        break;
    case WM_MOUSELEAVE:
        hovered = false;
        InvalidateRect(hwnd, NULL, TRUE);
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        COLORREF bgCol = g_isDarkTheme ? (hovered ? RGB(70, 70, 70) : RGB(50, 50, 50)) 
                                      : (hovered ? RGB(220, 220, 220) : RGB(240, 240, 240));
        COLORREF txtCol = g_isDarkTheme ? RGB(240, 240, 240) : RGB(10, 10, 10);

        HBRUSH hBr = CreateSolidBrush(bgCol);
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, txtCol);
        SelectObject(hdc, g_font);

        wchar_t text[256];
        GetWindowTextW(hwnd, text, 256);
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// Custom dark-mode compatible checkbox to avoid white artifacts
LRESULT CALLBACK CheckboxProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        bool checked = (SendMessage(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED);

        COLORREF bgCol = g_isDarkTheme ? RGB(30, 30, 30) : RGB(240, 240, 240);
        COLORREF txtCol = g_isDarkTheme ? RGB(240, 240, 240) : RGB(10, 10, 10);

        HBRUSH hBr = CreateSolidBrush(bgCol);
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);

        RECT rcBox = { 2, (rc.bottom - rc.top - 16) / 2, 18, (rc.bottom - rc.top - 16) / 2 + 16 };
        HBRUSH boxBr = CreateSolidBrush(g_isDarkTheme ? RGB(50, 50, 50) : RGB(255, 255, 255));
        HPEN boxPen = CreatePen(PS_SOLID, 1, g_isDarkTheme ? RGB(120, 120, 120) : RGB(150, 150, 150));
        SelectObject(hdc, boxBr);
        SelectObject(hdc, boxPen);
        Rectangle(hdc, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom);
        DeleteObject(boxBr);
        DeleteObject(boxPen);

        if (checked) {
            HPEN checkPen = CreatePen(PS_SOLID, 2, g_isDarkTheme ? RGB(46, 204, 113) : RGB(0, 120, 215));
            SelectObject(hdc, checkPen);
            MoveToEx(hdc, rcBox.left + 4, rcBox.top + 8, NULL);
            LineTo(hdc, rcBox.left + 7, rcBox.top + 12);
            LineTo(hdc, rcBox.right - 3, rcBox.top + 4);
            DeleteObject(checkPen);
        }

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, txtCol);
        SelectObject(hdc, g_font);

        RECT rcText = { 24, 0, rc.right, rc.bottom };
        wchar_t text[256];
        GetWindowTextW(hwnd, text, 256);
        DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void ApplyTheme(HWND hwnd) {
    SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)(g_isDarkTheme ? g_hbrBkgDark : g_hbrBkgLight));

    BOOL dark = g_isDarkTheme ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        SetProcessDPIAware();
        g_font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        CreateWindowExW(0, L"STATIC", L"Search:", WS_VISIBLE | WS_CHILD | SS_NOTIFY, 10, 10, 60, 20, hwnd, NULL, NULL, NULL);
        g_edit = CreateWindowExW(0, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 10, 30, 180, 25, hwnd, (HMENU)ID_EDIT_INPUT, NULL, NULL);
        
        g_checkGrep = CreateWindowExW(0, L"BUTTON", L"Grep Text", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 200, 32, 90, 23, hwnd, (HMENU)ID_CHECK_GREP, NULL, NULL);

        g_combo = CreateWindowExW(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE, 300, 30, 130, 300, hwnd, (HMENU)ID_COMBO_EXT, NULL, NULL);
        const wchar_t* filters[] = { L"All", L"Archives", L"Documents", L"Execute", L"Programming" };
        for (int i = 0; i < 5; i++) SendMessageW(g_combo, CB_ADDSTRING, 0, (LPARAM)filters[i]);
        SendMessageW(g_combo, CB_SETCURSEL, 0, 0);

        g_searchBtn = CreateWindowExW(0, L"BUTTON", L"Search Now", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 440, 30, 130, 25, hwnd, (HMENU)ID_BTN_SEARCH, NULL, NULL);
        g_list = CreateWindowExW(0, L"LISTBOX", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 10, 70, 560, 260, hwnd, (HMENU)ID_LIST_RESULTS, NULL, NULL);

        g_filterBtn = CreateWindowExW(0, L"BUTTON", L"Sources", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 10, 340, 180, 30, hwnd, (HMENU)ID_BTN_FILTERS, NULL, NULL);
        g_startBtn = CreateWindowExW(0, L"BUTTON", L"Startup Items", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 200, 340, 180, 30, hwnd, (HMENU)ID_BTN_STARTUP, NULL, NULL);
        g_themeBtn = CreateWindowExW(0, L"BUTTON", L"Toggle Theme", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 390, 340, 180, 30, hwnd, (HMENU)ID_BTN_THEME, NULL, NULL);

        g_statusBar = CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L" Ready", hwnd, ID_STATUS_BAR);
        int parts[] = { 380, -1 };
        SendMessageW(g_statusBar, SB_SETPARTS, 2, (LPARAM)parts);

        // Subclass controls for custom dark theme rendering & hover
        SetWindowSubclass(g_searchBtn, ButtonProc, 1, 0);
        SetWindowSubclass(g_filterBtn, ButtonProc, 2, 0);
        SetWindowSubclass(g_startBtn, ButtonProc, 3, 0);
        SetWindowSubclass(g_themeBtn, ButtonProc, 4, 0);
        SetWindowSubclass(g_checkGrep, CheckboxProc, 5, 0);

        EnumChildWindows(hwnd, [](HWND child, LPARAM font) -> BOOL {
            SendMessage(child, WM_SETFONT, font, TRUE);
            return TRUE;
            }, (LPARAM)g_font);
        break;
    }
    case WM_SIZE:
        LayoutControls(hwnd);
        break;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        if (g_isDarkTheme) {
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(35, 35, 35));
            return (INT_PTR)(uMsg == WM_CTLCOLOREDIT ? g_hbrEditDark : g_hbrBkgDark);
        } else {
            SetTextColor(hdc, RGB(10, 10, 10));
            SetBkColor(hdc, RGB(255, 255, 255));
            return (INT_PTR)(uMsg == WM_CTLCOLOREDIT ? g_hbrEditLight : g_hbrBkgLight);
        }
    }

    case WM_NOTIFY: {
        LPNMHDR hdr = (LPNMHDR)lParam;
        if (hdr->hwndFrom == g_statusBar && hdr->code == NM_CUSTOMDRAW) {
            LPNMCUSTOMDRAW cd = (LPNMCUSTOMDRAW)lParam;
            if (cd->dwDrawStage == CDDS_PREPAINT) {
                return CDRF_NOTIFYITEMDRAW;
            }
            else if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                // Turn text green during search or when search results are ready
                if (g_isSearching.load()) {
                    SetTextColor(cd->hdc, g_isDarkTheme ? RGB(46, 204, 113) : RGB(0, 150, 0)); // Green scan color
                } else {
                    SetTextColor(cd->hdc, g_isDarkTheme ? RGB(220, 220, 220) : RGB(10, 10, 10)); // Default theme text color
                }
                return CDRF_DODEFAULT;
            }
        }
        break;
    }

    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(wParam) == g_list) {
            ShowContextMenu(hwnd, lParam);
        }
        break;

    case WM_SEARCH_RESULT: {
        std::wstring* path = reinterpret_cast<std::wstring*>(lParam);
        if (path) {
            SendMessageW(g_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(path->c_str()));
            g_resultsCache.push_back(*path);
            delete path;
            UpdateCountText();
        }
        break;
    }
    case WM_SEARCH_FINISHED:
        g_isSearching = false;
        if (g_workerThread.joinable()) g_workerThread.join();
        SetWindowTextW(g_searchBtn, L"Search Now");
        UpdateStatusText(L"Search Complete.");
        InvalidateRect(g_statusBar, NULL, TRUE);
        break;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int notification = HIWORD(wParam);

        if (id == ID_LIST_RESULTS && notification == LBN_SELCHANGE) {
            if (g_isFilterView) {
                int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
                g_searchRegistry = (sel == 1);
            }
        }

        if (id == ID_BTN_SEARCH) {
            if (g_workerThread.joinable() && g_isSearching.load()) {
                // Cancel search request
                g_cancelSearch = true;
                g_isSearching = false;
                SetWindowTextW(g_searchBtn, L"Search Now");
                UpdateStatusText(L"Search Cancelled.");
                InvalidateRect(g_statusBar, NULL, TRUE);
                break;
            }

            wchar_t b[256];
            GetWindowTextW(g_edit, b, 256);
            if (wcslen(b) < 2) {
                MessageBoxW(hwnd, L"Enter at least 2 characters!", L"Error", MB_ICONWARNING);
                break;
            }

            g_isStartupView = false;
            g_isFilterView = false;
            SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
            g_resultsCache.clear();
            g_foundCount = 0;

            std::wstring query = b;
            for (auto& c : query) c = static_cast<wchar_t>(towlower(c));
            int filterIdx = static_cast<int>(SendMessageW(g_combo, CB_GETCURSEL, 0, 0));
            bool isReg = g_searchRegistry;
            bool grepMode = (SendMessageW(g_checkGrep, BM_GETCHECK, 0, 0) == BST_CHECKED);

            g_cancelSearch = false;
            g_isSearching = true;
            SetWindowTextW(g_searchBtn, L"Cancel");
            InvalidateRect(g_statusBar, NULL, TRUE);

            HWND hMainWnd = hwnd;
            g_workerThread = std::thread([hMainWnd, query, filterIdx, isReg, grepMode]() {
                if (isReg) {
                    SearchRegistry(hMainWnd, query);
                }
                else {
                    SearchAllDisks(hMainWnd, query, filterIdx, grepMode);
                }
                PostMessageW(hMainWnd, WM_SEARCH_FINISHED, 0, 0);
            });
        }
        else if (id == ID_BTN_STARTUP) {
            ShowStartupItems(g_list);
        }
        else if (id == ID_BTN_FILTERS) {
            ShowFilterOptions(g_list);
        }
        else if (id == ID_BTN_THEME) {
            g_isDarkTheme = !g_isDarkTheme;
            ApplyTheme(hwnd);
        }
        else if (id == ID_MENU_OPEN) {
            int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
            if (sel != LB_ERR && sel < static_cast<int>(g_resultsCache.size())) {
                std::wstring cmd = L"/select,\"" + g_resultsCache[sel] + L"\"";
                ShellExecuteW(NULL, L"open", L"explorer.exe", cmd.c_str(), NULL, SW_SHOWNORMAL);
            }
        }
        else if (id == ID_MENU_COPY) {
            int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
            if (sel != LB_ERR && sel < static_cast<int>(g_resultsCache.size())) {
                CopyToClipboard(g_resultsCache[sel]);
                UpdateStatusText(L"Copied to clipboard.");
            }
        }
        else if (id == ID_MENU_DELETE) {
            int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
            if (sel != LB_ERR && sel < static_cast<int>(g_resultsCache.size())) {
                if (g_isStartupView) {
                    if (DeleteStartupItem(g_resultsCache[sel])) {
                        SendMessageW(g_list, LB_DELETESTRING, sel, 0);
                        g_resultsCache.erase(g_resultsCache.begin() + sel);
                        UpdateCountText();
                        MessageBoxW(hwnd, L"Startup entry removed successfully.", L"Success", MB_OK);
                    } else {
                        MessageBoxW(hwnd, L"Failed to remove startup entry.", L"Error", MB_ICONERROR);
                    }
                }
                else {
                    std::wstring targetPath = g_resultsCache[sel];
                    size_t eqPos = targetPath.find(L" = ");
                    if (g_searchRegistry && eqPos != std::wstring::npos) {
                        MessageBoxW(hwnd, L"Registry deletion from list not implemented yet.", L"Info", MB_OK);
                        break;
                    }

                    targetPath.push_back(L'\0');
                    SHFILEOPSTRUCTW fileOp = { 0 };
                    fileOp.hwnd = hwnd;
                    fileOp.wFunc = FO_DELETE;
                    fileOp.pFrom = targetPath.c_str();
                    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI;

                    int res = SHFileOperationW(&fileOp);
                    if (res == 0 && !fileOp.fAnyOperationsAborted) {
                        SendMessageW(g_list, LB_DELETESTRING, sel, 0);
                        g_resultsCache.erase(g_resultsCache.begin() + sel);
                        UpdateCountText();
                        MessageBoxW(hwnd, L"File moved to Recycle Bin.", L"Success", MB_OK);
                    }
                    else {
                        MessageBoxW(hwnd, L"Failed to move file to Recycle Bin.", L"Error", MB_ICONERROR);
                    }
                }
            }
        }
        break;
    }
    case WM_DESTROY:
        g_cancelSearch = true;
        if (g_workerThread.joinable()) g_workerThread.join();
        if (g_font) DeleteObject(g_font);
        DeleteObject(g_hbrBkgDark);
        DeleteObject(g_hbrBkgLight);
        DeleteObject(g_hbrEditDark);
        DeleteObject(g_hbrEditLight);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"FileHunterAdvancedClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, L"FileHunterAdvancedClass", L"Simple Hunter v1.2",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 430,
        NULL, NULL, hInst, NULL
    );

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return static_cast<int>(m.wParam);
}
