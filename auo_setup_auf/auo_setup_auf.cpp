#include <Windows.h>
#include <string>
#include <thread>
#include <vector>
#include <signal.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include "filter.h"
#include "auo_pipe.h"
#include "auo_setup_util.h"
#include "auo_setup_common.h"

static const bool  DEBUG_MESSAGE_BOX       = false;

static const char *WindowClass = "AUO_SETUP";
static HINSTANCE  hInstanceDLL = NULL;
static HWND       hWndDialog   = NULL;
static HWND       hWndEdit     = NULL;
static const int  WindowWidth  = 640;
static const int  WindowHeight = 480;

static const int  IDC_EDIT     = 100;

static int        abortCount = 0;
static std::unique_ptr<HANDLE, HandleDeleter> eventAbort;
static HANDLE     auo_setup_process_handle = NULL;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        RECT rw, rc;
        GetWindowRect(hWnd, &rw); // ウィンドウ全体のサイズ
        GetClientRect(hWnd, &rc); // クライアント領域のサイズ
        //エディットボックスの定義
        hWndEdit = CreateWindow(
            "EDIT",             //ウィンドウクラス名
            NULL,                   //キャプション
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY |
            WS_HSCROLL | WS_VSCROLL | ES_AUTOHSCROLL | ES_AUTOVSCROLL |
            ES_LEFT | ES_MULTILINE,         //スタイル指定
            0, 0,                   //位置 ｘ、ｙ
            rc.right, rc.bottom,    //幅、高さ
            hWnd,                   //親ウィンドウ
            (HMENU)IDC_EDIT,        // メニューハンドルまたは子ウィンドウID
            hInstanceDLL,            //インスタンスハンドル
            NULL);                  //その他の作成データ

        //テキストエディットのフォント作成
        HFONT hFnt = CreateFont(18, 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, 0,
            SHIFTJIS_CHARSET,
            OUT_TT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            PROOF_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Meiryo UI");
        //テキストエディットのフォント変更のメッセージを送信
        SendMessage(hWndEdit, WM_SETFONT, (WPARAM)hFnt, MAKELPARAM(FALSE, 0));
        return 0;
        }
    case WM_CLOSE: {
            if (eventAbort) {
                if (abortCount > 0) {
                    //2回目の閉じるボタンでは、強制終了の有無を聞く
                    auto button = MessageBox(hWnd, "インストールを強制終了しますか?", "auo_setup", MB_YESNO | MB_ICONWARNING);
                    if (button == IDYES) {
                        TerminateProcess(auo_setup_process_handle, SIGINT);
                    }
                    //3回目は聞かないように
                    abortCount = -1;
                    eventAbort.reset();
                } else {
                    //1回目
                    AddTextBoxLine(hWndEdit, "インストールを中断します...\r\n");
                    //auo_setupに終了のためのイベントを送信する
                    SetEvent(eventAbort.get());
                    abortCount++;
                }
            } else {
                DestroyWindow(hWnd);
            }
        }
        break;
    case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

ATOM register_window(HINSTANCE hInstance) {
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    //wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TESTWINDOWS));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = WindowClass;
    wcex.lpszClassName = WindowClass;
    //wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
    wcex.hIcon = 0;
    wcex.hIconSm = 0;

    return RegisterClassEx(&wcex);
}

void run_auo_setup_thread(const char *auo_setup_exe_path, const char *exe_args) {
    //コマンドライン
    char buf[256];
    sprintf_s(buf, " -ppid 0x%08x -phwnd 0x%08x", (size_t)GetCurrentProcessId(), (size_t)hWndEdit);
    std::string cmd = std::string(exe_args) + buf;

    eventAbort = create_abort_event();
    auo_setup_process_handle = NULL;
    abortCount = 0;

    bool error = false;
    //昇格して実行
    HANDLE processHandle = NULL;
    if (start_installer_elevated(auo_setup_exe_path, cmd.c_str(), processHandle, true) != 0) {
        AddTextBoxLine(hWndEdit, "管理者権限を取得できませんでした。\r\nインストールに失敗しました。\r\n");
        error = true;
    } else {
        auo_setup_process_handle = processHandle;
        WaitForSingleObject(processHandle, INFINITE);
        DWORD return_code = 0;
        GetExitCodeProcess(processHandle, &return_code);
        error = return_code != 0;
        if (error) {
            AddTextBoxLine(hWndEdit, "インストールが完了できませんでした。\r\n");
        }
    }

    eventAbort.reset();

    //エラー終了していなければ、自動でウィンドウを閉じる
    //if (!error) {
    //    SendMessage(hWndDialog, WM_CLOSE, 0, 0);
    //}
}

void show_dialog_window_and_run(const char *title, const char *auo_setup_exe_path, const char *exe_args) {
    const auto aviutl_hwnd = get_aviutl_hwnd();

    register_window(hInstanceDLL);

    hWndDialog = CreateWindow(WindowClass, title,
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, WindowWidth, WindowHeight, nullptr, nullptr, hInstanceDLL, nullptr);

    EnableWindow(aviutl_hwnd, FALSE);

    ShowWindow(hWndDialog, SW_SHOW);
    UpdateWindow(hWndDialog);

    std::thread threadAuoSetup(run_auo_setup_thread, auo_setup_exe_path, exe_args);

    // メイン メッセージ ループ
    { MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    EnableWindow(aviutl_hwnd, TRUE);
    BringWindowToTop(aviutl_hwnd);
    if (threadAuoSetup.joinable()) {
        threadAuoSetup.join();
    }
}

int check_dot_net_installed() {
    char aviutl_dir[MAX_PATH_LEN];
    get_aviutl_dir(aviutl_dir, _countof(aviutl_dir));

    char default_exe_dir[MAX_PATH_LEN];
    PathCombine(default_exe_dir, aviutl_dir, DEFAULT_EXE_DIR);

    char buf[MAX_PATH_LEN];
    PathCombine(buf, default_exe_dir, DOT_NET_RUNTIME_CHECKER);

    if (!PathFileExists(buf)) {
        //モジュールがない場合はチェックしない(スキップしたい時のため)
        return true;
    }
    return check_dll_can_be_loaded(buf);
}

int check_vc_runtime_installed() {
    char aviutl_dir[MAX_PATH_LEN];
    get_aviutl_dir(aviutl_dir, _countof(aviutl_dir));

    char default_exe_dir[MAX_PATH_LEN];
    PathCombine(default_exe_dir, aviutl_dir, DEFAULT_EXE_DIR);

    char buf[MAX_PATH_LEN];
    PathCombine(buf, default_exe_dir, VC_RUNTIME_CHECKER);

    if (!PathFileExists(buf)) {
        //モジュールがない場合はチェックしない(スキップしたい時のため)
        return true;
    }
    return check_dll_can_be_loaded(buf);
}

void run_auo_setup(const char *exe_args) {
    char aviutl_dir[MAX_PATH_LEN] = { 0 };
    get_aviutl_dir(aviutl_dir, _countof(aviutl_dir));

    char default_exe_dir[MAX_PATH_LEN] = { 0 };
    PathCombine(default_exe_dir, aviutl_dir, DEFAULT_EXE_DIR);

    char auo_setup_exe_path[MAX_PATH_LEN] = { 0 };
    PathCombine(auo_setup_exe_path, default_exe_dir, INSTALLER_NAME);

    char installer_ini[MAX_PATH_LEN] = { 0 };
    PathCombine(installer_ini, default_exe_dir, INSTALLER_INI);

    char install_name[1024] = { 0 };
    GetPrivateProfileString(INSTALLER_INI_SECTION, "name", "", install_name, sizeof(install_name), installer_ini);

    //多重起動を防止
    auto mutexHandle = avoid_multiple_run_of_auo_setup_exe();
    if (!mutexHandle) {
        return;
    }

    char mes[1024] = { 0 };
    if (!PathFileExists(auo_setup_exe_path)) {
        sprintf_s(mes, "%s フォルダに %s が存在しません。%s を使用する準備を続行できません。\n"
            "ダウンロードしたzipファイルから \"%s\", \"%s\" フォルダを\n"
            "Aviutlフォルダ内にコピーできているか、再度確認してください。\n",
            DEFAULT_EXE_DIR, INSTALLER_NAME, install_name,
            DEFAULT_EXE_DIR, DEFAULT_PLUGINS_DIR);
        MessageBox(NULL, mes, "auo_setup", MB_OK | MB_ICONERROR);
        return;
    }

    sprintf_s(mes,
        "%s を使用できるようにする準備を行います。\n", install_name);
    if (check_admin_required()) {
        strcat_s(mes,
            "\n"
            "このあと「このアプリがデバイスに変更を加えることを許可しますか?」と\n"
            "表示されたら「はい」をクリックしてください。\n");
    }
    MessageBox(NULL, mes, "auo_setup", MB_OK | MB_ICONWARNING);

    //起動前に解放
    mutexHandle.reset();

    char title[1024];
    sprintf_s(title, "auo_setup: %s 使用の準備を行います...", install_name);
    show_dialog_window_and_run(title, auo_setup_exe_path, exe_args);
    return;
}

void Init() {
    const bool vc_runtime_installed = check_vc_runtime_installed();
    const bool dot_net_installed    = check_dot_net_installed();
    if (DEBUG_MESSAGE_BOX) {
        if (vc_runtime_installed) {
            MessageBox(NULL, "VC runtimeのインストールは不要です。", "auo_setup.auf", MB_OK);
        }
        else {
            MessageBox(NULL, "VC runtimeのインストールが必要です。", "auo_setup.auf", MB_OK);
        }
        if (dot_net_installed) {
            MessageBox(NULL, ".NET Frameworkのインストールは不要です。", "auo_setup.auf", MB_OK);
        }
        else {
            MessageBox(NULL, ".NET Frameworkのインストールが必要です。", "auo_setup.auf", MB_OK);
        }
    }
    if (!vc_runtime_installed || !dot_net_installed) {
        run_auo_setup(vc_runtime_installed ? "-force-vc" : "");
    }
}

FILTER_DLL* list[] = { nullptr };
EXTERN_C FILTER_DLL __declspec(dllexport) **__stdcall GetFilterTableList() {
    Init();
    return list;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    hInstanceDLL = hinstDLL;
    return TRUE;
}
