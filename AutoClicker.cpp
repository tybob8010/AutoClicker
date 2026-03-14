#include <windows.h>
#include <atomic>
#include <mmsystem.h>
#include <string>
#include <ctime>
#include <fstream>
#include <iostream>
#include "resource.h" 

#pragma comment(lib, "winmm.lib")

// --- 定義 ---
#define ID_HOTKEY_START 1
#define ID_HOTKEY_STOP  2
#define ID_HOTKEY_EXIT  3
#define ID_EDIT_INTERVAL 100
#define ID_EDIT_HOUR    101
#define ID_EDIT_MIN     103 
#define ID_RADIO_ON     104
#define ID_RADIO_OFF    105

// --- グローバル変数 ---
HINSTANCE hInst;
HWND g_hEditInterval, g_hEditHour, g_hEditMin, g_hRadioOn, g_hRadioOff;
WNDPROC g_OldEditProc;
std::atomic<bool> g_clicking(false), g_running(true);
std::atomic<double> g_interval(10.0);
int g_lastExitDay = -1, g_lastExitHour = -1, g_lastExitMin = -1;
int g_winX = CW_USEDEFAULT, g_winY = CW_USEDEFAULT;

// --- 関数: 設定の保存 ---
void SaveSettings(HWND hWnd) {
    // 【修正点】最小化されている場合は座標を更新せずに終了
    if (IsIconic(hWnd)) return;

    SetFileAttributesA("settings.dat", FILE_ATTRIBUTE_NORMAL);

    std::ofstream ofs("settings.dat", std::ios::out | std::ios::trunc);
    if (ofs.is_open()) {
        RECT rect;
        GetWindowRect(hWnd, &rect);

        WCHAR intervalW[32], hW[32], mW[32];
        char intervalA[32], hA[32], mA[32];

        GetWindowTextW(g_hEditInterval, intervalW, 32);
        GetWindowTextW(g_hEditHour, hW, 32);
        GetWindowTextW(g_hEditMin, mW, 32);

        WideCharToMultiByte(CP_ACP, 0, intervalW, -1, intervalA, 32, NULL, NULL);
        WideCharToMultiByte(CP_ACP, 0, hW, -1, hA, 32, NULL, NULL);
        WideCharToMultiByte(CP_ACP, 0, mW, -1, mA, 32, NULL, NULL);

        int autoStatus = (SendMessage(g_hRadioOn, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;

        ofs << rect.left << "\n";
        ofs << rect.top << "\n";
        ofs << intervalA << "\n";
        ofs << hA << "\n";
        ofs << mA << "\n";
        ofs << autoStatus << "\n";
        ofs << g_lastExitDay << "\n" << g_lastExitHour << "\n" << g_lastExitMin << "\n";

        ofs.flush();
        ofs.close();
        SetFileAttributesA("settings.dat", FILE_ATTRIBUTE_HIDDEN);
    }
}

// --- 関数: 設定の読み込み (安全装置付き) ---
void LoadSettings() {
    std::ifstream ifs("settings.dat");

    if (!ifs.is_open() || ifs.peek() == std::ifstream::traits_type::eof()) {
        SetWindowTextA(g_hEditInterval, "10.0");
        SetWindowTextA(g_hEditHour, "17");
        SetWindowTextA(g_hEditMin, "00");
        SendMessage(g_hRadioOff, BM_SETCHECK, BST_CHECKED, 1);
        return;
    }

    std::string sX, sY, interval, h, m, autoStatus, lDay, lHour, lMin;

    try {
        if (std::getline(ifs, sX)) g_winX = std::stoi(sX);
        if (std::getline(ifs, sY)) g_winY = std::stoi(sY);

        if (std::getline(ifs, interval)) SetWindowTextA(g_hEditInterval, interval.c_str());
        else SetWindowTextA(g_hEditInterval, "10.0");

        if (std::getline(ifs, h)) SetWindowTextA(g_hEditHour, h.c_str());
        else SetWindowTextA(g_hEditHour, "17");

        if (std::getline(ifs, m)) SetWindowTextA(g_hEditMin, m.c_str());
        else SetWindowTextA(g_hEditMin, "00");

        if (std::getline(ifs, autoStatus)) {
            int status = (autoStatus == "1") ? 1 : 0;
            SendMessage(g_hRadioOn, BM_SETCHECK, (status == 1 ? BST_CHECKED : BST_UNCHECKED), 1);
            SendMessage(g_hRadioOff, BM_SETCHECK, (status == 0 ? BST_CHECKED : BST_UNCHECKED), 1);
        }

        if (std::getline(ifs, lDay)) g_lastExitDay = std::stoi(lDay);
        if (std::getline(ifs, lHour)) g_lastExitHour = std::stoi(lHour);
        if (std::getline(ifs, lMin)) g_lastExitMin = std::stoi(lMin);
    }
    catch (...) {
        SetWindowTextA(g_hEditInterval, "10.0");
        SetWindowTextA(g_hEditHour, "17");
        SetWindowTextA(g_hEditMin, "00");
    }
    ifs.close();
}

// --- 補助関数・サブクラス化 ---
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
        case 1:  SendMessage(hWnd, EM_SETSEL, 0, -1); return 0;
        case 3:  SendMessage(hWnd, WM_COPY, 0, 0);    return 0;
        case 22: SendMessage(hWnd, WM_PASTE, 0, 0);   return 0;
        case 24: SendMessage(hWnd, WM_CUT, 0, 0);     return 0;
        }
    }
    return CallWindowProc(g_OldEditProc, hWnd, message, wParam, lParam);
}

// --- スレッド処理 ---
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
                    SaveSettings(hWnd);
                    PostMessage(hWnd, WM_CLOSE, 0, 0);
                    break;
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
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            double inv = g_interval.load();
            Sleep((DWORD)(inv < 1 ? 1 : inv));
        }
        else Sleep(50);
    }
    return 0;
}

// --- メインウィンドウプロシージャ ---
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        HMENU hMenuBar = CreateMenu(), hFile = CreateMenu();
        AppendMenu(hFile, MF_STRING, IDM_FILE_EXIT, L"終了 (&X)");
        AppendMenu(hMenuBar, MF_POPUP, (UINT_PTR)hFile, L"ファイル (&F)");
        AppendMenu(hMenuBar, MF_STRING, IDM_HELP_INFO, L"バージョン (&H)");
        SetMenu(hWnd, hMenuBar);
        CreateWindow(L"STATIC", L"連打間隔(ms):", WS_VISIBLE | WS_CHILD, 20, 20, 110, 20, hWnd, NULL, hInst, NULL);
        g_hEditInterval = CreateWindow(L"EDIT", L"10.0", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 140, 18, 100, 25, hWnd, (HMENU)ID_EDIT_INTERVAL, hInst, NULL);
        CreateWindow(L"STATIC", L"自動終了時刻:", WS_VISIBLE | WS_CHILD, 20, 55, 110, 20, hWnd, NULL, hInst, NULL);
        g_hEditHour = CreateWindow(L"EDIT", L"17", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_CENTER, 140, 53, 40, 25, hWnd, (HMENU)ID_EDIT_HOUR, hInst, NULL);
        CreateWindow(L"STATIC", L":", WS_VISIBLE | WS_CHILD | ES_CENTER, 182, 55, 10, 20, hWnd, NULL, hInst, NULL);
        g_hEditMin = CreateWindow(L"EDIT", L"00", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_CENTER, 195, 53, 40, 25, hWnd, (HMENU)ID_EDIT_MIN, hInst, NULL);
        g_hRadioOn = CreateWindow(L"BUTTON", L"有効", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 140, 85, 50, 20, hWnd, (HMENU)ID_RADIO_ON, hInst, NULL);
        g_hRadioOff = CreateWindow(L"BUTTON", L"無効", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 195, 85, 50, 20, hWnd, (HMENU)ID_RADIO_OFF, hInst, NULL);

        g_OldEditProc = (WNDPROC)SetWindowLongPtr(g_hEditHour, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
        SetWindowLongPtr(g_hEditMin, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
        SetWindowLongPtr(g_hEditInterval, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

        CreateWindow(L"STATIC", L"F9:開始 / F10:停止 / ESC:終了", WS_VISIBLE | WS_CHILD, 20, 115, 250, 20, hWnd, NULL, hInst, NULL);
        LoadSettings();
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_FILE_EXIT) PostMessage(hWnd, WM_CLOSE, 0, 0);
        if (LOWORD(wParam) == IDM_HELP_INFO) MessageBox(hWnd, L"Auto Clicker v.1.1.1\n\n@tybob8010\nhttps://tybob8010.github.io", L"バージョン", MB_OK);
        break;
    case WM_HOTKEY:
        if (wParam == ID_HOTKEY_START) {
            WCHAR buf[32]; GetWindowTextW(g_hEditInterval, buf, 32);
            try { g_interval = std::stod(buf); }
            catch (...) { g_interval = 10.0; }
            g_clicking = true;
        }
        else if (wParam == ID_HOTKEY_STOP) g_clicking = false;
        else if (wParam == ID_HOTKEY_EXIT) PostMessage(hWnd, WM_CLOSE, 0, 0);
        break;
    case WM_CLOSE:
        SaveSettings(hWnd);
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        SaveSettings(hWnd);
        g_running = false;
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// --- エントリポイント ---
int APIENTRY wWinMain(_In_ HINSTANCE h, _In_opt_ HINSTANCE p, _In_ LPWSTR c, _In_ int n) {
    timeBeginPeriod(1);
    hInst = h;

    HICON hIconLarge = (HICON)LoadImage(h, MAKEINTRESOURCE(128), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    HICON hIconSmall = (HICON)LoadImage(h, MAKEINTRESOURCE(128), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = h;
    wcex.hIcon = hIconLarge;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"AutoClicker";
    wcex.hIconSm = hIconSmall;

    if (!RegisterClassExW(&wcex)) return 0;

    // 起動時の座標読み込み
    {
        std::ifstream ifs("settings.dat");
        if (ifs.is_open()) {
            std::string sX, sY;
            if (std::getline(ifs, sX)) try { g_winX = std::stoi(sX); }
            catch (...) {}
            if (std::getline(ifs, sY)) try { g_winY = std::stoi(sY); }
            catch (...) {}
            ifs.close();
        }
    }

    // もし -32000 (最小化座標) が保存されていた場合の保険
    if (g_winX < -10000) g_winX = CW_USEDEFAULT;
    if (g_winY < -10000) g_winY = CW_USEDEFAULT;

    HWND hWnd = CreateWindowExW(WS_EX_TOPMOST, L"AutoClicker", L"Auto Clicker v.1.1.1",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        g_winX, g_winY, 300, 210, NULL, NULL, h, NULL);

    if (!hWnd) return 0;

    RegisterHotKey(hWnd, ID_HOTKEY_START, 0, VK_F9);
    RegisterHotKey(hWnd, ID_HOTKEY_STOP, 0, VK_F10);
    RegisterHotKey(hWnd, ID_HOTKEY_EXIT, 0, VK_ESCAPE);

    CreateThread(NULL, 0, ClickThread, NULL, 0, NULL);
    CreateThread(NULL, 0, ExitMonitorThread, (LPVOID)hWnd, 0, NULL);

    ShowWindow(hWnd, n);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    timeEndPeriod(1);
    return (int)msg.wParam;
}
