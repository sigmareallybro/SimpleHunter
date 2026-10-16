// Old Version
#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <commctrl.h>
#include <shellapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

// ID
#define ID_EDIT_INPUT 101
#define ID_BTN_SEARCH 102
#define ID_LIST_RESULTS 103
#define ID_COMBO_EXT 104
#define ID_BTN_FIX_SYSTEM 105
#define ID_BTN_STARTUP 106
#define ID_MENU_OPEN 201
#define ID_MENU_DELETE 202
#define ID_MENU_COPY 203

HWND hEdit, hList, hCombo, hFixBtn, hStartBtn;
HFONT hGuiFont = NULL;

// Checking
bool IsRunAsAdmin() {
    BOOL fRet = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
            fRet = elevation.TokenIsElevated;
        }
    }
    if (hToken) CloseHandle(hToken);
    return fRet;
}

// idk
bool CheckExtension(std::wstring fileName, int filterIndex) {
    if (filterIndex == 0) return true; // All
    
    size_t dot = fileName.find_last_of(L".");
    if (dot == std::wstring::npos) return false;
    
    std::wstring ext = fileName.substr(dot);
    for(auto &c : ext) c = towlower(c);

    if (filterIndex == 1) return ext == L".txt";
    if (filterIndex == 2) return ext == L".bat";
    if (filterIndex == 3) { // Archives
        return (ext == L".zip" || ext == L".rar" || ext == L".7z");
    }
    return true;
}

// Scan files
void SearchFiles(std::wstring directory, std::wstring target, HWND listbox, int filterIndex) {
    std::wstring query = directory + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(query.c_str(), &fd);

    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::wstring fn = fd.cFileName;
        if (fn == L".." || fn == L".") continue;

        std::wstring fp = directory + L"\\" + fn;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            SearchFiles(fp, target, listbox, filterIndex);
        } else {
            std::wstring lowFn = fn;
            for(auto &c : lowFn) c = towlower(c);

            if (lowFn.find(target) != std::wstring::npos) {
                if (CheckExtension(fn, filterIndex)) {
                    SendMessageW(listbox, LB_ADDSTRING, 0, (LPARAM)fp.c_str());
                    
                    // Window
                    MSG msg;
                    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }
                }
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

// Autorun
void ShowStartupItems(HWND listbox) {
    SendMessageW(listbox, LB_RESETCONTENT, 0, 0);
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
                    SendMessageW(listbox, LB_ADDSTRING, 0, (LPARAM)entry.c_str());
                }
                RegCloseKey(hKey);
            }
        }
    }
}

// Context Menu
void ShowContextMenu(HWND hwnd, LPARAM lParam) {
    POINT pt = { LOWORD(lParam), HIWORD(lParam) };
    POINT ptClient = pt;
    ScreenToClient(hList, &ptClient);
    
    LRESULT itemIndex = SendMessage(hList, LB_ITEMFROMPOINT, 0, MAKELPARAM(ptClient.x, ptClient.y));
    if (HIWORD(itemIndex) == 0) {
        SendMessage(hList, LB_SETCURSEL, LOWORD(itemIndex), 0);
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, ID_MENU_OPEN, L"Locate in Explorer");
        AppendMenuW(hMenu, MF_STRING, ID_MENU_COPY, L"Copy Path");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, ID_MENU_DELETE, L"Delete Permanent");
        TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            SetProcessDPIAware();
            hGuiFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, 
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                   DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

            // Interface
            CreateWindowExW(0, L"STATIC", L"Find name:", WS_VISIBLE | WS_CHILD, 10, 10, 80, 20, hwnd, NULL, NULL, NULL);
            hEdit = CreateWindowExW(0, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER, 10, 30, 200, 25, hwnd, (HMENU)ID_EDIT_INPUT, NULL, NULL);
            
            CreateWindowExW(0, L"STATIC", L"Format:", WS_VISIBLE | WS_CHILD, 220, 10, 80, 20, hwnd, NULL, NULL, NULL);
            hCombo = CreateWindowExW(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE, 220, 30, 100, 300, hwnd, (HMENU)ID_COMBO_EXT, NULL, NULL);
            
            const wchar_t* filters[] = { L"All", L".txt", L".bat", L"Archives" };
            for(int i = 0; i < 4; i++) SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)filters[i]);
            SendMessageW(hCombo, CB_SETCURSEL, 0, 0);

            HWND hSearchBtn = CreateWindowExW(0, L"BUTTON", L"Search Now", WS_VISIBLE | WS_CHILD, 330, 30, 140, 25, hwnd, (HMENU)ID_BTN_SEARCH, NULL, NULL);
            hList = CreateWindowExW(0, L"LISTBOX", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 10, 70, 460, 230, hwnd, (HMENU)ID_LIST_RESULTS, NULL, NULL);
            
            hFixBtn = CreateWindowExW(0, L"BUTTON", L"Fix System Restrictions", WS_VISIBLE | WS_CHILD, 10, 310, 460, 30, hwnd, (HMENU)ID_BTN_FIX_SYSTEM, NULL, NULL);
            hStartBtn = CreateWindowExW(0, L"BUTTON", L"Show Startup Items", WS_VISIBLE | WS_CHILD, 10, 345, 460, 30, hwnd, (HMENU)ID_BTN_STARTUP, NULL, NULL);

            // Font
            EnumChildWindows(hwnd, [](HWND child, LPARAM font) -> BOOL {
                SendMessage(child, WM_SETFONT, font, TRUE);
                return TRUE;
            }, (LPARAM)hGuiFont);
            break;
        }
        case WM_CONTEXTMENU: {
            if ((HWND)wParam == hList) ShowContextMenu(hwnd, lParam);
            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == ID_BTN_SEARCH) {
                wchar_t b[256]; GetWindowTextW(hEdit, b, 256);
                if (wcslen(b) < 2) { MessageBoxW(hwnd, L"Too little!!", L"Error", MB_ICONWARNING); break; }
                SendMessageW(hList, LB_RESETCONTENT, 0, 0);
                std::wstring t = b; for(auto &c : t) c = towlower(c);
                SearchFiles(L"C:\\Users", t, hList, SendMessage(hCombo, CB_GETCURSEL, 0, 0));
                MessageBoxW(hwnd, L"Search Complete!", L"Done", MB_OK | MB_ICONINFORMATION);
            }
            else if (LOWORD(wParam) == ID_BTN_STARTUP) {
                ShowStartupItems(hList);
            }
            else if (LOWORD(wParam) == ID_BTN_FIX_SYSTEM) {
                if (!IsRunAsAdmin()) { MessageBoxW(hwnd, L"Need Admin Rights!", L"Error", MB_ICONERROR); break; }
                HKEY k;
                if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
                    DWORD v = 0;
                    RegSetValueExW(k, L"DisableTaskMgr", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
                    RegCloseKey(k);
                    MessageBoxW(hwnd, L"Registry Fixed!", L"Success", MB_OK);
                }
            }
            else if (LOWORD(wParam) == ID_MENU_OPEN) {
                int sel = SendMessage(hList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    wchar_t path[MAX_PATH]; SendMessageW(hList, LB_GETTEXT, sel, (LPARAM)path);
                    std::wstring cmd = L"/select,\"" + std::wstring(path) + L"\"";
                    ShellExecuteW(NULL, L"open", L"explorer.exe", cmd.c_str(), NULL, SW_SHOWNORMAL);
                }
            }
            break;
        }
        case WM_DESTROY: {
            if (hGuiFont) DeleteObject(hGuiFont);
            PostQuitMessage(0);
            break;
        }
        default: return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int n) {
    WNDCLASSW wc = {0}; 
    wc.lpfnWndProc = WindowProc; 
    wc.hInstance = h; 
    wc.lpszClassName = L"FileHunter";
    wc.hIcon = LoadIconW(h, MAKEINTRESOURCE(1)); 
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"FileHunter", L"SimpleHunter v1.3", 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 430, NULL, NULL, h, NULL);

    ShowWindow(hwnd, n); 
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) { 
        TranslateMessage(&m); 
        DispatchMessageW(&m); 
    }
    return 0;
}
