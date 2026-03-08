#include <windows.h>
#include <atomic>
#include <mmsystem.h>
#include <string>
#include <ctime>
#include <fstream>

#pragma comment(lib, "winmm.lib")

#define ID_HOTKEY_START 1
#define ID_HOTKEY_STOP  2
#define ID_HOTKEY_EXIT  3
#define ID_EDIT_INTERVAL 100
#define ID_EDIT_HOUR    101
#define ID_EDIT_MIN     103 
#define ID_RADIO_ON     104
#define ID_RADIO_OFF    105
#define IDM_FILE_EXIT   1001
#define IDM_HELP_INFO   1002

HINSTANCE hInst;
HWND g_hEditInterval, g_hEditHour, g_hEditMin, g_hRadioOn, g_hRadioOff;
WNDPROC g_OldEditProc;
std::atomic<bool> g_clicking(false), g_running(true);
std::atomic<double> g_interval(10.0);

int g_lastExitDay = -1, g_lastExitHour = -1, g_lastExitMin = -1;

void SaveSettings() {
    std::wofstream ofs("settings.dat");
    if (ofs) {
        WCHAR interval[32], h[8], m[8];
        GetWindowText(g_hEditInterval, interval, 32);
        GetWindowText(g_hEditHour, h, 8);
        GetWindowText(g_hEditMin, m, 8);
        int autoStatus = (SendMessage(g_hRadioOn, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
        ofs << interval << "\n" << h << "\n" << m << "\n";
        ofs << g_lastExitDay << "\n" << g_lastExitHour << "\n" << g_lastExitMin << "\n";
        ofs << autoStatus << "\n";
    }
}

void LoadSettings() {
    std::wifstream ifs("settings.dat");
    int status = 0; // デフォルト値を「0（無効）」に設定

    if (ifs.is_open()) {
        std::wstring interval, h, m, lDay, lHour, lMin, autoStatus;
        if (std::getline(ifs, interval)) SetWindowText(g_hEditInterval, interval.c_str());
        if (std::getline(ifs, h)) SetWindowText(g_hEditHour, h.c_str());
        if (std::getline(ifs, m)) SetWindowText(g_hEditMin, m.c_str());
        if (std::getline(ifs, lDay)) try { g_lastExitDay = std::stoi(lDay); }
        catch (...) {}
        if (std::getline(ifs, lHour)) try { g_lastExitHour = std::stoi(lHour); }
        catch (...) {}
        if (std::getline(ifs, lMin)) try { g_lastExitMin = std::stoi(lMin); }
        catch (...) {}
        if (std::getline(ifs, autoStatus)) {
            try { status = std::stoi(autoStatus); }
            catch (...) { status = 0; }
        }
    }

    // 読み込み成否に関わらず、statusの値に基づいてボタンをセット
    if (status == 1) {
        SendMessage(g_hRadioOn, BM_SETCHECK, BST_CHECKED, 1);
        SendMessage(g_hRadioOff, BM_SETCHECK, BST_UNCHECKED, 0);
    }
    else {
        SendMessage(g_hRadioOn, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessage(g_hRadioOff, BM_SETCHECK, BST_CHECKED, 1);
    }
}

void AdjustValue(HWND hWnd, int delta, int maxVal) {
    int val = GetDlgItemInt(GetParent(hWnd), (int)GetWindowLongPtr(hWnd, GWLP_ID), NULL, FALSE);
    val += (delta > 0) ? 1 : -1;
    if (val > maxVal) val = 0; else if (val < 0) val = maxVal;
    SetDlgItemInt(GetParent(hWnd), (int)GetWindowLongPtr(hWnd, GWLP_ID), val, FALSE);
}

LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_MOUSEWHEEL) {
        int id = (int)GetWindowLongPtr(hWnd, GWLP_ID);
        if (id == ID_EDIT_HOUR) AdjustValue(hWnd, GET_WHEEL_DELTA_WPARAM(wParam), 23);
        if (id == ID_EDIT_MIN)  AdjustValue(hWnd, GET_WHEEL_DELTA_WPARAM(wParam), 59);
        return 0;
    }
    if (message == WM_CHAR) {
        switch (wParam) {
        case 1:  SendMessage(hWnd, EM_SETSEL, 0, -1); return 0; // Ctrl+A
        case 3:  SendMessage(hWnd, WM_COPY, 0, 0);    return 0; // Ctrl+C
        case 22: SendMessage(hWnd, WM_PASTE, 0, 0);   return 0; // Ctrl+V
        case 24: SendMessage(hWnd, WM_CUT, 0, 0);     return 0; // Ctrl+X
        }
    }
    return CallWindowProc(g_OldEditProc, hWnd, message, wParam, lParam);
}

DWORD WINAPI ExitMonitorThread(LPVOID lpParam) {
    HWND hWnd = (HWND)lpParam;
    while (g_running) {
        if (SendMessage(g_hRadioOn, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            int th = GetDlgItemInt(hWnd, ID_EDIT_HOUR, NULL, FALSE);
            int tm_val = GetDlgItemInt(hWnd, ID_EDIT_MIN, NULL, FALSE);
            struct tm ltm; time_t now = time(nullptr); localtime_s(&ltm, &now);
            if (ltm.tm_hour == th && ltm.tm_min == tm_val) {
                if (!(g_lastExitDay == ltm.tm_mday && g_lastExitHour == th && g_lastExitMin == tm_val)) {
                    g_lastExitDay = ltm.tm_mday; g_lastExitHour = th; g_lastExitMin = tm_val;
                    SaveSettings(); PostMessage(hWnd, WM_CLOSE, 0, 0); break;
                }
            }
        }
        Sleep(2000);
    }
    return 0;
}

DWORD WINAPI ClickThread(LPVOID lpParam) {
    while (g_running) {
        if (g_clicking) {
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0); mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            double inv = g_interval.load(); Sleep((DWORD)(inv < 1 ? 1 : inv));
        }
        else Sleep(50);
    }
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        HMENU hMenuBar = CreateMenu(), hFile = CreateMenu();
        AppendMenu(hFile, MF_STRING, IDM_FILE_EXIT, L"終了 (&X)");
        AppendMenu(hMenuBar, MF_POPUP, (UINT_PTR)hFile, L"ファイル (&F)");
        AppendMenu(hMenuBar, MF_STRING, IDM_HELP_INFO, L"バージョン (&H)");

        SetMenu(hWnd, hMenuBar);

        // 「連打間隔」ラベルと入力欄
        CreateWindow(L"STATIC", L"連打間隔(ms):", WS_VISIBLE | WS_CHILD, 20, 20, 110, 20, hWnd, NULL, hInst, NULL);
        g_hEditInterval = CreateWindow(L"EDIT", L"10.0", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 140, 18, 100, 25, hWnd, (HMENU)ID_EDIT_INTERVAL, hInst, NULL);

        // 「自動終了時刻」ラベルの幅を 110 に広げ、入力欄の開始位置を 140 にずらす
        CreateWindow(L"STATIC", L"自動終了時刻:", WS_VISIBLE | WS_CHILD, 20, 55, 110, 20, hWnd, NULL, hInst, NULL);

        g_hEditHour = CreateWindow(L"EDIT", L"17", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_CENTER, 140, 53, 40, 25, hWnd, (HMENU)ID_EDIT_HOUR, hInst, NULL);
        CreateWindow(L"STATIC", L":", WS_VISIBLE | WS_CHILD | ES_CENTER, 182, 55, 10, 20, hWnd, NULL, hInst, NULL);
        g_hEditMin = CreateWindow(L"EDIT", L"00", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_CENTER, 195, 53, 40, 25, hWnd, (HMENU)ID_EDIT_MIN, hInst, NULL);

        // ラジオボタンの位置も少し調整（ラベルと合わせる）
        g_hRadioOn = CreateWindow(L"BUTTON", L"有効", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 140, 85, 50, 20, hWnd, (HMENU)ID_RADIO_ON, hInst, NULL);
        g_hRadioOff = CreateWindow(L"BUTTON", L"無効", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 195, 85, 50, 20, hWnd, (HMENU)ID_RADIO_OFF, hInst, NULL);

        // サブクラス化（マウスホイールとCtrl+A/C/V用）
        g_OldEditProc = (WNDPROC)SetWindowLongPtr(g_hEditHour, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
        SetWindowLongPtr(g_hEditMin, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
        SetWindowLongPtr(g_hEditInterval, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

        CreateWindow(L"STATIC", L"F9:開始 / F10:停止 / ESC:終了", WS_VISIBLE | WS_CHILD, 20, 115, 250, 20, hWnd, NULL, hInst, NULL);

        LoadSettings();
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_FILE_EXIT) { SaveSettings(); DestroyWindow(hWnd); }
        if (LOWORD(wParam) == IDM_HELP_INFO) MessageBox(hWnd, L"Auto Clicker v.1.0\n\n@tybob8010\nhttps://tybob8010.github.io", L"バージョン", MB_OK);
        break;
    case WM_HOTKEY:
        if (wParam == ID_HOTKEY_START) {
            WCHAR buf[32]; GetWindowText(g_hEditInterval, buf, 32);
            try { g_interval = std::stod(buf); }
            catch (...) { g_interval = 10.0; }
            g_clicking = true;
        }
        else if (wParam == ID_HOTKEY_STOP) g_clicking = false;
        else if (wParam == ID_HOTKEY_EXIT) { SaveSettings(); DestroyWindow(hWnd); }
        break;
    case WM_CLOSE: SaveSettings(); DestroyWindow(hWnd); break;
    case WM_DESTROY: g_running = false; PostQuitMessage(0); break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(_In_ HINSTANCE h, _In_opt_ HINSTANCE p, _In_ LPWSTR c, _In_ int n) {
    timeBeginPeriod(1);
    hInst = h;

    // --- アイコンの読み込み ---
    // Resource.h で IDI_ICON3 は 134 と定義されているので、それに合わせます
    HICON hMainIcon = LoadIcon(h, MAKEINTRESOURCE(128));

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX) };
    wcex.hIcon = hMainIcon;   // これでタスクバーとエクスプローラー
    wcex.hIconSm = hMainIcon; // これでウィンドウ左上
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = h;
    wcex.hIcon = hMainIcon;   // exeファイルとタスクバーのアイコン
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = L"AutoClicker";
    wcex.hIconSm = hMainIcon;   // ウィンドウ左上の小さいアイコン

    if (!RegisterClassExW(&wcex)) return 0;

    // ウィンドウの作成（タイトルバーのバージョンも 3.1 に合わせておきます）
    HWND hWnd = CreateWindowExW(WS_EX_TOPMOST, L"AutoClicker", L"Auto Clicker v1.0",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, 0, 300, 210, NULL, NULL, h, NULL);

    if (!hWnd) return 0;

    // ホットキーの登録
    RegisterHotKey(hWnd, ID_HOTKEY_START, 0, VK_F9);
    RegisterHotKey(hWnd, ID_HOTKEY_STOP, 0, VK_F10);
    RegisterHotKey(hWnd, ID_HOTKEY_EXIT, 0, VK_ESCAPE);

    // 各種スレッドの開始
    CreateThread(NULL, 0, ClickThread, NULL, 0, NULL);
    CreateThread(NULL, 0, ExitMonitorThread, (LPVOID)hWnd, 0, NULL);

    ShowWindow(hWnd, n);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    timeEndPeriod(1);
    return (int)msg.wParam;
}