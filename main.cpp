// Source Code
#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// ID elements for controlling
#define ID_EDIT_INPUT       101
#define ID_BTN_SEARCH       102
#define ID_LIST_RESULTS     103
#define ID_COMBO_EXT        104
#define ID_BTN_FILTERS      105
#define ID_BTN_STARTUP      106

#define ID_MENU_OPEN        201
#define ID_MENU_COPY        202
#define ID_MENU_DELETE      203

#define WM_SEARCH_RESULT    (WM_APP + 1)
#define WM_SEARCH_FINISHED  (WM_APP + 2)

// Global variables Interface
HWND g_hwnd = nullptr;
HWND g_edit = nullptr;
HWND g_list = nullptr;
HWND g_combo = nullptr;
HWND g_filterBtn = nullptr;
HWND g_startBtn = nullptr;
HWND g_searchBtn = nullptr;
HFONT g_font = nullptr;

// Setting search
std::atomic<bool> g_cancelSearch{ false };
std::thread g_workerThread;
std::vector<std::wstring> g_resultsCache;
bool g_searchRegistry = false; // false = Disk, true = Registry
bool g_isStartupView = false;
bool g_isFilterView = false;

// Checking Extension
bool CheckExtension(const std::wstring& fileName, int filterIndex) {
    if (filterIndex == 0) return true; // All

    size_t dot = fileName.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;

    std::wstring ext = fileName.substr(dot);
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));

    switch (filterIndex) {
    case 1: return ext == L".txt";
    case 2: return ext == L".bat";
    case 3: return (ext == L".zip" || ext == L".rar" || ext == L".7z");
    case 4: return ext == L".exe";
    default: return true;
    }
}

// Checking if folder system
bool IsProtectedPath(const std::wstring& path) {
    std::wstring lowPath = path;
    for (auto& c : lowPath) c = static_cast<wchar_t>(towlower(c));

    // Exclude System Folders
    if (lowPath.find(L"\\windows") != std::wstring::npos ||
        lowPath.find(L"\\program files") != std::wstring::npos ||
        lowPath.find(L"\\program files (x86)") != std::wstring::npos ||
        lowPath.find(L"\\programdata") != std::wstring::npos ||
        lowPath.find(L"\\system volume information") != std::wstring::npos) {
        return true;
    }
    return false;
}

// Recursive file search in directories
void SearchFilesRecursive(HWND hwnd, const std::wstring& directory, const std::wstring& target, int filterIndex) {
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
                SearchFilesRecursive(hwnd, fp, target, filterIndex);
            }
        }
        else {
            std::wstring lowFn = fn;
            for (auto& c : lowFn) c = static_cast<wchar_t>(towlower(c));

            if (lowFn.find(target) != std::wstring::npos) {
                if (CheckExtension(fn, filterIndex)) {
                    std::wstring* resultPath = new std::wstring(fp);
                    PostMessageW(hwnd, WM_SEARCH_RESULT, 0, reinterpret_cast<LPARAM>(resultPath));
                }
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// Search all files on Disks
void SearchAllDisks(HWND hwnd, const std::wstring& target, int filterIndex) {
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
            // Search everything without system folders
            SearchFilesRecursive(hwnd, root, target, filterIndex);
        }
        pDrive += wcslen(pDrive) + 1;
    }
}

// Search Registry files
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
                }
                RegCloseKey(hKey);
            }
        }
    }
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

        AppendMenuW(hMenu, MF_STRING, ID_MENU_COPY, L"Copy Path");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, ID_MENU_DELETE, L"Delete");

        TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);
    }
}

void LayoutControls(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    MoveWindow(g_edit, 10, 30, w - 280, 25, TRUE);
    MoveWindow(g_combo, w - 260, 30, 110, 300, TRUE);
    MoveWindow(g_searchBtn, w - 140, 30, 130, 25, TRUE);
    MoveWindow(g_list, 10, 70, w - 20, h - 150, TRUE);
    MoveWindow(g_filterBtn, 10, h - 70, w - 20, 25, TRUE);
    MoveWindow(g_startBtn, 10, h - 40, w - 20, 25, TRUE);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        SetProcessDPIAware();
        g_font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        CreateWindowExW(0, L"STATIC", L"Find name:", WS_VISIBLE | WS_CHILD, 10, 10, 80, 20, hwnd, NULL, NULL, NULL);
        g_edit = CreateWindowExW(0, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 10, 30, 200, 25, hwnd, (HMENU)ID_EDIT_INPUT, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Format:", WS_VISIBLE | WS_CHILD, 220, 10, 80, 20, hwnd, NULL, NULL, NULL);
        g_combo = CreateWindowExW(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE, 220, 30, 110, 300, hwnd, (HMENU)ID_COMBO_EXT, NULL, NULL);

        const wchar_t* filters[] = { L"All", L".txt", L".bat", L"Archives", L".exe" };
        for (int i = 0; i < 5; i++) SendMessageW(g_combo, CB_ADDSTRING, 0, (LPARAM)filters[i]);
        SendMessageW(g_combo, CB_SETCURSEL, 0, 0);

        g_searchBtn = CreateWindowExW(0, L"BUTTON", L"Search Now", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 330, 30, 140, 25, hwnd, (HMENU)ID_BTN_SEARCH, NULL, NULL);
        g_list = CreateWindowExW(0, L"LISTBOX", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 10, 70, 460, 230, hwnd, (HMENU)ID_LIST_RESULTS, NULL, NULL);

        g_filterBtn = CreateWindowExW(0, L"BUTTON", L"Filters", WS_VISIBLE | WS_CHILD, 10, 310, 460, 30, hwnd, (HMENU)ID_BTN_FILTERS, NULL, NULL);
        g_startBtn = CreateWindowExW(0, L"BUTTON", L"Show Startup Items", WS_VISIBLE | WS_CHILD, 10, 345, 460, 30, hwnd, (HMENU)ID_BTN_STARTUP, NULL, NULL);

        EnumChildWindows(hwnd, [](HWND child, LPARAM font) -> BOOL {
            SendMessage(child, WM_SETFONT, font, TRUE);
            return TRUE;
            }, (LPARAM)g_font);
        break;
    }
    case WM_SIZE:
        LayoutControls(hwnd);
        break;

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
        }
        break;
    }
    case WM_SEARCH_FINISHED:
        if (g_workerThread.joinable()) g_workerThread.join();
        MessageBoxW(hwnd, L"Search Complete!", L"Done", MB_OK | MB_ICONINFORMATION);
        break;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int notification = HIWORD(wParam);

        if (id == ID_LIST_RESULTS && notification == LBN_SELCHANGE) {
            if (g_isFilterView) {
                int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
                if (sel == 0) g_searchRegistry = false;
                else if (sel == 1) g_searchRegistry = true;
            }
        }

        if (id == ID_BTN_SEARCH) {
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

            std::wstring query = b;
            for (auto& c : query) c = static_cast<wchar_t>(towlower(c));
            int filterIdx = static_cast<int>(SendMessageW(g_combo, CB_GETCURSEL, 0, 0));
            bool isReg = g_searchRegistry;

            if (g_workerThread.joinable()) g_workerThread.join();
            g_cancelSearch = false;

            HWND hMainWnd = hwnd;
            g_workerThread = std::thread([hMainWnd, query, filterIdx, isReg]() {
                if (isReg) {
                    SearchRegistry(hMainWnd, query);
                }
                else {
                    SearchAllDisks(hMainWnd, query, filterIdx);
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
            }
        }
        else if (id == ID_MENU_DELETE) {
            int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
            if (sel != LB_ERR && sel < static_cast<int>(g_resultsCache.size())) {
                if (g_isStartupView) {
                    MessageBoxW(hwnd, L"Startup deletion not implemented yet.", L"Info", MB_OK);
                }
                else {
                    std::wstring targetPath = g_resultsCache[sel];
                    std::wstring lowTarget = targetPath;
                    for (auto& c : lowTarget) c = static_cast<wchar_t>(towlower(c));

                    // Security of System folders
                    if (lowTarget.find(L"\\windows") != std::wstring::npos ||
                        lowTarget.find(L"\\system32") != std::wstring::npos ||
                        lowTarget.find(L"\\boot") != std::wstring::npos ||
                        lowTarget.length() <= 4) { // Block system files
                        MessageBoxW(hwnd, L"Access denied.", L"Security Alert", MB_OK | MB_ICONERROR);
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
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    g_hwnd = NULL;
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"FileHunterClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, L"FileHunterClass", L"SimpleHunter",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 440,
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
