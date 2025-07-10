#include <Windows.h>
#include <VersionHelpers.h>
#include <stdio.h>
#include <signal.h>
#include <tchar.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <vector>
#include <string>
#include <thread>
#include "NETVersionChecker.h"
#include "auo_pipe.h"
#include "auo_setup_common.h"

static const bool CHECK_DOT_NET_BY_DLL = true;

//Ctrl + C ハンドラ
static bool g_signal_abort = false;

static std::unique_ptr<HANDLE, HandleDeleter> eventAbort;
static HWND parent_hwndEdit;

// --- auo_setup_aufから持ってきたウィンドウ関連のコード ---
static const TCHAR *WindowClass = _T("AUO_SETUP");
static HINSTANCE  hInstance_setup = NULL;
static HWND       hWndDialog = NULL;

static const int  WindowWidth = 640;
static const int  WindowHeight = 320;
static const int  IDC_EDIT = 100;

#pragma warning(push)
#pragma warning(disable:4100)
static void sigintcatch(int sig) {
    if (g_signal_abort) {
        exit(1); // 2回目は直ちに終了する
    } else {
        g_signal_abort = true;
    }
}
#pragma warning(pop)
static int set_signal_handler() {
    int ret = 0;
    if (SIG_ERR == signal(SIGINT, sigintcatch)) {
        _ftprintf(stderr, _T("failed to set signal handler.\n"));
    }
    return ret;
}

static bool func_check_abort() {
    if (g_signal_abort) return true;
    if (eventAbort) {
        return WaitForSingleObject(eventAbort.get(), 0) == WAIT_OBJECT_0;
    }
    return false;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        RECT rw, rc;
        GetWindowRect(hWnd, &rw); // ウィンドウ全体のサイズ
        GetClientRect(hWnd, &rc); // クライアント領域のサイズ
        //エディットボックスの定義
        parent_hwndEdit = CreateWindow(
            _T("EDIT"),             //ウィンドウクラス名
            NULL,                   //キャプション
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY |
            WS_HSCROLL | WS_VSCROLL | ES_AUTOHSCROLL | ES_AUTOVSCROLL |
            ES_LEFT | ES_MULTILINE,         //スタイル指定
            0, 0,                   //位置 ｘ、ｙ
            rc.right, rc.bottom,    //幅、高さ
            hWnd,                   //親ウィンドウ
            (HMENU)IDC_EDIT,        // メニューハンドルまたは子ウィンドウID
            hInstance_setup,            //インスタンスハンドル
            NULL);                  //その他の作成データ

        //テキストエディットのフォント作成
        HFONT hFnt = CreateFont(18, 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, 0,
            SHIFTJIS_CHARSET,
            OUT_TT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            PROOF_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Meiryo UI"));
        //テキストエディットのフォント変更のメッセージを送信
        SendMessage(parent_hwndEdit, WM_SETFONT, (WPARAM)hFnt, MAKELPARAM(FALSE, 0));
        return 0;
    }
    case WM_CLOSE: {
        if (eventAbort) {
            //確認して強制終了
            auto button = MessageBox(hWnd, _T("インストールを強制終了しますか?"), _T("auo_setup"), MB_YESNO | MB_ICONWARNING);
            if (button == IDYES) {
                SetEvent(eventAbort.get());
                g_signal_abort = true;
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
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = WindowClass;
    wcex.lpszClassName = WindowClass;
    wcex.hIcon = 0;
    wcex.hIconSm = 0;

    return RegisterClassEx(&wcex);
}
// --- ここまで ---

enum InstallerResult {
    INSTALLER_RESULT_REQUIRE_REBOOT = -2,
    INSTALLER_RESULT_REQUIRE_ADMIN = -1,
    INSTALLER_RESULT_SUCCESS = 0,
    INSTALLER_RESULT_ERROR = 1,
    INSTALLER_RESULT_ABORT = 2,
};

typedef struct setup_options_t {
    TCHAR install_dir[1024];
    BOOL force_vc_install;
    BOOL force_dotnet_install;
    BOOL quiet;
    BOOL no_debug;
    DWORD parent_pid;
    HWND parent_hwndEdit;
    BOOL force_run;
} setup_options_t;

static void print_message(const TCHAR *format, ...) {
    va_list args;
    va_start(args, format);
    int len = _vsctprintf(format, args) // _vscprintf doesn't count
        + 1; // terminating '\0'
    std::vector<TCHAR> buf(len, 0);
    _vstprintf_s(buf.data(), buf.size(), format, args);

    if (parent_hwndEdit) {
        AddTextBoxLine(parent_hwndEdit, _T("%s"), buf.data());
    }
    _ftprintf(stderr, _T("%s"), buf.data());
}

int check_exe_dir() {
    wchar_t wbuffer[1024] = { 0 };
    GetModuleFileNameW(NULL, wbuffer, _countof(wbuffer));

    std::vector<char> str;
    int multibyte_length = WideCharToMultiByte(CP_THREAD_ACP, WC_NO_BEST_FIT_CHARS, wbuffer, -1, nullptr, 0, nullptr, nullptr);
    str.resize(multibyte_length + 1, 0);
    if (0 == WideCharToMultiByte(CP_THREAD_ACP, WC_NO_BEST_FIT_CHARS, wbuffer, -1, &str[0], multibyte_length, nullptr, nullptr)) {
        str.clear();
        return 1;
    }

    std::vector<wchar_t> wstr;
    int widechar_length = MultiByteToWideChar(CP_THREAD_ACP, 0, str.data(), -1, nullptr, 0);
    wstr.resize(widechar_length, 0);
    if (0 == MultiByteToWideChar(CP_THREAD_ACP, 0, str.data(), -1, &wstr[0], (int)wstr.size())) {
        wstr.clear();
        return 0;
    }
    return (wcscmp(wbuffer, wstr.data()) != 0) ? 1 : 0;
}

bool check_if_dll_exists(const TCHAR *dllname) {
    HMODULE hmodule = LoadLibrary(dllname);
    if (hmodule) {
        FreeLibrary(hmodule);
        return true;
    }
    return false;
}

InstallerResult check_vc_runtime(const TCHAR *exe_dir, BOOL quiet, BOOL force_install) {
    TCHAR buf[2048] = { 0 };
    TCHAR installer_ini[1024] = { 0 };
    PathCombine(installer_ini, exe_dir, INSTALLER_INI);

    if (!force_install) {
        bool dll_check = false;
        PathCombine(buf, exe_dir, VC_RUNTIME_CHECKER);
        if (check_dll_can_be_loaded(buf)) {
            dll_check = true;
        }
        if (dll_check) {
            GetPrivateProfileString(INSTALLER_INI_SECTION, _T("vc_runtime_dll"), _T(""), buf, _countof(buf), installer_ini);
            TCHAR *ctx = nullptr;
            for (TCHAR *next = _tcstok_s(buf, _T(","), &ctx); next != nullptr; next = _tcstok_s(nullptr, _T(","), &ctx)) {
                if (!check_if_dll_exists(next)) {
                    dll_check = false;
                    break;
                }
            }
        }
        if (dll_check) {
            return INSTALLER_RESULT_SUCCESS;
        }
    }
    if (check_admin_required()) {
        return INSTALLER_RESULT_REQUIRE_ADMIN;
    }

#define fprintf_check if (!quiet) print_message

    TCHAR path_installer[1024] = { 0 };
    PathCombine(path_installer, exe_dir, VC_RUNTIME_INSTALLER);
    if (!PathFileExists(path_installer)) {
        if (!PathFileExists(path_installer)) {
            fprintf_check(_T("VC runtime のインストーラが\n  %s\nに存在しません。\n"), path_installer);
            fprintf_check(_T("ダウンロードしたzipファイルの中身をすべてAviutlフォルダ内にコピーできているか、再度確認してください。\n"));
            return INSTALLER_RESULT_ERROR;
        }
    }

    fprintf_check(_T("\n"));
    fprintf_check(_T("VC runtime をインストールします。\n"));
    DWORD return_code = 0;
    RunInstaller(path_installer, _T("/quiet /norestart"), NULL, _T("VC runtime インストール"), TRUE, TRUE, FALSE, quiet, &return_code, func_check_abort);
    _tremove(path_installer);
    if (   return_code == ERROR_SUCCESS_REBOOT_INITIATED
        || return_code == ERROR_SUCCESS_REBOOT_REQUIRED) {
        return INSTALLER_RESULT_REQUIRE_REBOOT;
    } else if (return_code == ERROR_PRODUCT_VERSION) {
        fprintf_check(_T("VC runtime はすでにインストールされています。\n"));
    } else if (return_code == ERROR_OPERATION_ABORTED) {
        fprintf_check(_T("VC runtime のインストールを中断しました。\n"));
        return INSTALLER_RESULT_ABORT;
    } else if (return_code != ERROR_SUCCESS) {
        fprintf_check(_T("VC runtime のインストールでエラーが発生しました。\n"));

        TCHAR buffer[1024];
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, return_code, 0, buffer, _countof(buffer), NULL);
        fprintf_check(_T("%s\n"), buffer);
        return INSTALLER_RESULT_ERROR;
    }
    fprintf_check(_T("VC runtime のインストールが完了しました。\n\n"));
    return INSTALLER_RESULT_SUCCESS;
}

static void write_net_framework_install_error(DWORD return_code, BOOL quiet) {
    switch (return_code) {
    case ERROR_INSTALL_USEREXIT:
        fprintf_check(_T("ユーザーによりキャンセルされました。\n"));
        break;
    case ERROR_INSTALL_FAILURE:
        fprintf_check(_T("ユーザーによりキャンセルされました。\n"));
        break;
    case 5100:
        fprintf_check(_T("空きディスク容量が不足しているか、システムがインストールの必要要件を満たしていません。\n"));
        break;
    default: {
        TCHAR buffer[1024];
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, return_code, 0, buffer, _countof(buffer), NULL);
        fprintf_check(_T("%s\n"), buffer);
    } break;
    }
}

InstallerResult check_net_framework(const TCHAR *exe_dir, BOOL quiet, BOOL force_install) {
    TCHAR buf[2048] = { 0 };
    TCHAR installer_ini[1024] = { 0 };
    PathCombine(installer_ini, exe_dir, INSTALLER_INI);
    GetPrivateProfileString(INSTALLER_INI_SECTION, _T("net_ver"), _T(""), buf, _countof(buf), installer_ini);
    TCHAR net_ver[256] = { 0 };
    _stprintf_s(net_ver, _countof(net_ver), _T(".NET Framework %s"), buf);
    int ret = -1, language_pack = -1;
    if (CHECK_DOT_NET_BY_DLL) {
        if (!force_install) {
            PathCombine(buf, exe_dir, DOT_NET_RUNTIME_CHECKER);
            if (check_dll_can_be_loaded(buf)) {
                language_pack = 0;
                ret = 0;
            }
        }
    } else {
        if      (_tcsstr(buf, _T("1.1")))         ret = check_net_1_1(&language_pack);
        else if (_tcsstr(buf, _T("2.0")))         ret = check_net_2_0(&language_pack);
        else if (_tcsstr(buf, _T("3.0")))         ret = check_net_3_0(&language_pack);
        else if (_tcsstr(buf, _T("3.5")))         ret = check_net_3_5(&language_pack);
        else if (_tcsstr(buf, _T("4.0 Client")))  ret = check_net_4_0_Client(&language_pack);
        else if (_tcsstr(buf, _T("4.0 Full")))    ret = check_net_4_0_Full(&language_pack);
        else if (_tcsstr(buf, _T("4.0")))         ret = check_net_4_0_Client(&language_pack);
        else if (_tcsstr(buf, _T("4.5")))         ret = check_net_4_5(&language_pack);
        else if (_tcsstr(buf, _T("4.5.1")))       ret = check_net_4_5_1(&language_pack);
        else if (_tcsstr(buf, _T("4.5.2")))       ret = check_net_4_5_2(&language_pack);
        else if (_tcsstr(buf, _T("4.6")))         ret = check_net_4_6(&language_pack);
        else if (_tcsstr(buf, _T("4.6.1")))       ret = check_net_4_6_1(&language_pack);
        else if (_tcsstr(buf, _T("4.6.2")))       ret = check_net_4_6_2(&language_pack);
        else if (_tcsstr(buf, _T("4.7")))         ret = check_net_4_7(&language_pack);
        else if (_tcsstr(buf, _T("4.7.1")))       ret = check_net_4_7_1(&language_pack);
        else if (_tcsstr(buf, _T("4.7.2")))       ret = check_net_4_7_2(&language_pack);
        else if (_tcsstr(buf, _T("4.8")))         ret = check_net_4_8(&language_pack);
        else return INSTALLER_RESULT_ERROR;
    }

    if (!force_install && ret >= 0) {
        fprintf_check(_T("%s はすでにインストールされています。\n"), net_ver);
    } else {
        if (check_admin_required()) {
            return INSTALLER_RESULT_REQUIRE_ADMIN;
        }

        TCHAR path_installer[1024] = { 0 };
        PathCombine(path_installer, exe_dir, DOT_NET_RUNTIME_INSTALLER);
        if (!PathFileExists(path_installer)) {
            if (!PathFileExists(path_installer)) {
                fprintf_check(_T("%s のインストーラが\n  %s\nに存在しません。\n"), net_ver, path_installer);
                fprintf_check(_T("ダウンロードしたzipファイルの中身をすべてAviutlフォルダ内にコピーできているか、再度確認してください。\n"));
                return INSTALLER_RESULT_ERROR;
            }
        }

        TCHAR run_process_mes[1024] = { 0 };
        _stprintf_s(run_process_mes, _countof(run_process_mes), _T("%s インストール"), net_ver);
        fprintf_check(_T("%s をインストールします。最大で10分ほどかかる場合があります。\n"), net_ver);
        DWORD return_code = 0;
        RunInstaller(path_installer, _T("/q /LANG:ENG /norestart"), NULL, run_process_mes, TRUE, TRUE, FALSE, quiet, &return_code, func_check_abort);
        fprintf_check(_T("\n\n"));
        _tremove(path_installer);
        if (   return_code == ERROR_SUCCESS_REBOOT_INITIATED
            || return_code == ERROR_SUCCESS_REBOOT_REQUIRED) {
            return INSTALLER_RESULT_REQUIRE_REBOOT;
        } else if (return_code == ERROR_OPERATION_ABORTED) {
            fprintf_check(_T("%s のインストールを中断しました。\n"), net_ver);
            return INSTALLER_RESULT_ABORT;
        } else if (return_code == ERROR_PRODUCT_VERSION) {
            fprintf_check(_T("%s はすでにインストールされています。\n"), net_ver);
        } else if (return_code != ERROR_SUCCESS) {
            fprintf_check(_T("%s のインストールでエラーが発生しました。\n"), net_ver);
            write_net_framework_install_error(return_code, quiet);
            return INSTALLER_RESULT_ERROR;
        }
        fprintf_check(_T("%s のインストールが完了しました。\n"), net_ver);
    }

    if (!force_install && language_pack >= 0) {
        if (ret < 0) {
            fprintf_check(_T("%s 言語パック はすでにインストールされています。\n"), net_ver);
        }
    } else {
        if (check_admin_required()) {
            return INSTALLER_RESULT_REQUIRE_ADMIN;
        }

        TCHAR path_installer[1024] = { 0 };
        PathCombine(path_installer, exe_dir, DOT_NET_LANGPACK_INSTALLER);
        if (!PathFileExists(path_installer)) {
            if (!PathFileExists(path_installer)) {
                fprintf_check(_T("%s 言語パックのインストーラが\n  %s\nに存在しません。\n"), net_ver, path_installer);
                fprintf_check(_T("ダウンロードしたzipファイルの中身をすべてAviutlフォルダ内にコピーできているか、再度確認してください。\n"));
                return INSTALLER_RESULT_ERROR;
            }
        }

        TCHAR run_process_mes[1024] = { 0 };
        _stprintf_s(run_process_mes, _countof(run_process_mes), _T("%s 言語パック インストール"), net_ver);
        fprintf_check(_T("%s をインストールします。5分ほどかかる場合があります。\n"), net_ver);
        DWORD return_code = 0;
        RunInstaller(path_installer, _T("/q /norestart"), NULL, run_process_mes, TRUE, TRUE, FALSE, quiet, &return_code, func_check_abort);
        fprintf_check(_T("\n\n"));
        _tremove(path_installer);
        if (   return_code == ERROR_SUCCESS_REBOOT_INITIATED
            || return_code == ERROR_SUCCESS_REBOOT_REQUIRED) {
            return INSTALLER_RESULT_REQUIRE_REBOOT;
        } else if (return_code == ERROR_OPERATION_ABORTED) {
            return INSTALLER_RESULT_ABORT;
        } else if (return_code == ERROR_PRODUCT_VERSION) {
            fprintf_check(_T("%s はすでにインストールされています。\n"), net_ver);
        } else if (return_code != ERROR_SUCCESS) {
            fprintf_check(_T("%s 言語パックのインストールでエラーが発生しました。\n"), net_ver);
            write_net_framework_install_error(return_code, quiet);
            //言語パックのインストールエラーは無視
            //return INSTALLER_RESULT_ERROR;
        }
        fprintf_check(_T("%s 言語パックのインストールが完了しました。\n"), net_ver);
    }
    return INSTALLER_RESULT_SUCCESS;
}

void create_cmd_for_installer(TCHAR *cmd, size_t nSize, setup_options_t *option) {
    cmd[0] = _T('\0');
    if (option->no_debug)             _tcscat_s(cmd, nSize, _T("-no-debug "));
    if (option->force_vc_install)     _tcscat_s(cmd, nSize, _T("-force-vc "));
    if (option->force_dotnet_install) _tcscat_s(cmd, nSize, _T("-force-dotnet "));
    if (option->force_run)            _tcscat_s(cmd, nSize, _T("-force-run "));
    if (option->parent_pid)           _stprintf_s(cmd + _tcslen(cmd), nSize - _tcslen(cmd), _T("-ppid 0x%08x "), option->parent_pid);
    if (option->parent_hwndEdit)      _stprintf_s(cmd + _tcslen(cmd), nSize - _tcslen(cmd), _T("-phwnd 0x%08x"), (size_t)option->parent_hwndEdit);
}

int restart_installer_elevated(const TCHAR *exe_path, setup_options_t *option,
    decltype(avoid_multiple_run_of_auo_setup_exe())& mutexHandle) {
    TCHAR cmd[4096] = { 0 };
    create_cmd_for_installer(cmd, _countof(cmd), option);

    //実行前にmutexを解放
    mutexHandle.reset();

    HANDLE processHandle = NULL;
    if (start_installer_elevated(exe_path, cmd, processHandle, false) != 0) {
        _ftprintf(stderr, _T("管理者権限の取得に失敗しました。\n"));
        return 1;
    }
    WaitForSingleObject(processHandle, INFINITE);
    DWORD return_code = 0;
    GetExitCodeProcess(processHandle, &return_code);
    return return_code;
}

static void print_help(const TCHAR *exe_path) {
    _ftprintf(stdout, _T("%s\n")
        _T("Aviutlの出力プラグインの簡易インストーラです。\n")
        _T("\n")
        _T("オプション\n")
        _T("-help                        このヘルプの表示\n")
        _T("\n")
        _T("-force-vc                    VC runtimeのインストールを強制します。\n")
        _T("-force-dotnet                .NET Frameworkのインストールを強制します。\n")
        _T("\n")
        _T("-no-debug                    インストール時のデバッグ出力を行いません。\n")
        _T("\n")
        _T("-quiet            コマンドプロンプトにメッセージを表示しない。\n"),
        PathFindFileName(exe_path)
        );
}

int run_installer_main(setup_options_t *option, const TCHAR *exe_path) {
    auto mutexHandle = avoid_multiple_run_of_auo_setup_exe();
    if (!mutexHandle) {
        print_message(_T("すでに簡易インストーラが実行されているため、終了します。\n"));
        return 0;
    }

    if (option->parent_pid != 0) {
        eventAbort = open_abort_event(option->parent_pid);
    } else {
        eventAbort = create_abort_event();
    }

    set_signal_handler();

    TCHAR exe_dir[1024] = { 0 };
    _tcscpy_s(exe_dir, _countof(exe_dir), exe_path);
    PathRemoveFileSpec(exe_dir);

    TCHAR installer_ini[1024] = { 0 };
    PathCombine(installer_ini, exe_dir, INSTALLER_INI);
    TCHAR install_name[1024] = { 0 };
    GetPrivateProfileString(INSTALLER_INI_SECTION, _T("name"), _T(""), install_name, _countof(install_name), installer_ini);
    PathRemoveExtension(install_name);

    print_message(_T("%s を使用できるようにするための準備を行います。\n"), install_name);

    bool require_reboot = false;

    //VC runtimeのインストール
    int ret = check_vc_runtime(exe_dir, option->quiet, option->force_vc_install);
    if (ret == INSTALLER_RESULT_REQUIRE_ADMIN) {
        if (restart_installer_elevated(exe_path, option, mutexHandle)) {
            return 1;
        }
        return 0;
    } else if (ret == INSTALLER_RESULT_ABORT) {
        return 1;
    } else if (ret == INSTALLER_RESULT_REQUIRE_REBOOT) {
        require_reboot = true;
    } else if (ret > 0) {
        return 1;
    }

    //.NET Frameworkのインストール
    ret = check_net_framework(exe_dir, option->quiet, option->force_dotnet_install);
    if (ret == INSTALLER_RESULT_REQUIRE_ADMIN) {
        if (restart_installer_elevated(exe_path, option, mutexHandle)) {
            return 1;
        }
        return 0;
    } else if (ret == INSTALLER_RESULT_ABORT) {
        return 1;
    } else if (ret == INSTALLER_RESULT_REQUIRE_REBOOT) {
        require_reboot = true;
    } else if (ret > 0) {
        print_message(_T("%sの記述が正しくありません。.NET Frameworkのインストールに失敗しました。\n\n"), INSTALLER_INI);
        return 1;
    }

    print_message(_T("\n"));
    print_message(_T("%s を使用する準備が完了しました。\n"), install_name);
    if (require_reboot) {
        print_message(_T("PCの再起動必要です。\n"));
        print_message(_T("PCの再起動後、%s が使用できるようになります。\n"), install_name);
        return -1;
    }
    print_message(_T("\n"));
    print_message(_T("このウィンドウを閉じ、\n"));
    print_message(_T("Aviutlの出力プラグインに %s が追加されているか、確認してください。\n"), install_name);

    eventAbort.reset();
    SendMessage(hWndDialog, WM_CLOSE, 0, 0);
    return 0;
}

int _tmain(int argc, TCHAR **argv) {
    setup_options_t option = { 0 };
    parent_hwndEdit = NULL;

    for (int i_arg = 1; i_arg < argc; i_arg++) {
        if (0 == _tcsicmp(argv[i_arg], _T("-force-vc"))) {
            option.force_vc_install = TRUE;
        } else if (0 == _tcsicmp(argv[i_arg], _T("-force-dotnet"))) {
            option.force_dotnet_install = TRUE;
        } else if (0 == _tcsicmp(argv[i_arg], _T("-quiet"))) {
            option.quiet = TRUE;
        } else if (0 == _tcsicmp(argv[i_arg], _T("-no-debug"))) {
            option.no_debug = TRUE;
        } else if (0 == _tcsicmp(argv[i_arg], _T("-force-run"))) {
            option.force_run = TRUE;
        } else if (0 == _tcsicmp(argv[i_arg], _T("-ppid"))) {
            i_arg++;
            DWORD value = 0;
            if (_stscanf_s(argv[i_arg], _T("0x%x"), &value) == 1) {
                option.parent_pid = value;
            }
        } else if (0 == _tcsicmp(argv[i_arg], _T("-phwnd"))) {
            i_arg++;
            DWORD value = 0;
            if (_stscanf_s(argv[i_arg], _T("0x%x"), &value) == 1) {
                parent_hwndEdit = (HWND)value;
            }
        } else if (0 == _tcsicmp(argv[i_arg], _T("-help"))
            || 0 == _tcsicmp(argv[i_arg], _T("-h"))) {
            print_help(argv[0]);
            return 1;
        }
    }

    TCHAR exe_dir[1024] = { 0 };
    _tcscpy_s(exe_dir, _countof(exe_dir), argv[0]);
    PathRemoveFileSpec(exe_dir);

    if (check_exe_dir()) {
        print_message(_T("簡易インストーラが環境依存文字を含むパスから実行されており、\nインストールを続行できません。\n"));
        return 1;
    }

    TCHAR installer_ini[1024] = { 0 };
    PathCombine(installer_ini, exe_dir, INSTALLER_INI);
    if (!PathFileExists(installer_ini)) {
        print_message(_T("%sが存在しません。インストールを続行できません。\n"), INSTALLER_INI);
        print_message(_T("ダウンロードしたzipファイルの中身をすべてAviutlフォルダ内にコピーできているか、再度確認してください。\n"));
        return 1;
    }

    if (!option.force_run) {
        // ppidが渡されていない場合、ユーザーが直接実行しているものと思われる。
        // メッセージを出して終了する
        if (option.parent_pid == 0) {
            TCHAR install_name[1024] = { 0 };
            GetPrivateProfileString(INSTALLER_INI_SECTION, _T("name"), _T(""), install_name, _countof(install_name), installer_ini);
            PathRemoveExtension(install_name);
            TCHAR install_desc_url[1024] = { 0 };
            GetPrivateProfileString(INSTALLER_INI_SECTION, _T("install_desc_url"), _T(""), install_desc_url, _countof(install_desc_url), installer_ini);
            print_message(_T("%sの導入方法が変更されており、\n簡易インストーラ(auo_setup)の実行は不要となっています。\n\n"), install_name);
            print_message(_T("新しい導入方法については、\n同梱の%s_readme.txtの【導入方法】の欄をご確認いただくか、\n下記urlを確認してください。\n\n"), install_name);
            print_message(_T("%s\n\n"), install_desc_url);
            print_message(_T("どれかのキーを押すと終了します。\n"));
            getchar();
            return 1;
        }
    }

    //-phwndが指定されていない場合、ウィンドウを作成してそこにログを出力する
    if (parent_hwndEdit == NULL) {
        hInstance_setup = GetModuleHandle(NULL);
        register_window(hInstance_setup);

        TCHAR install_name[1024] = { 0 };
        GetPrivateProfileString(INSTALLER_INI_SECTION, _T("name"), _T(""), install_name, _countof(install_name), installer_ini);
        TCHAR title[1024];
        _stprintf_s(title, _T("auo_setup: %s 使用の準備を行います..."), install_name);

        hWndDialog = CreateWindow(WindowClass, title,
            WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, WindowWidth, WindowHeight, nullptr, nullptr, hInstance_setup, nullptr);

        ShowWindow(hWndDialog, SW_SHOW);
        UpdateWindow(hWndDialog);

        std::thread thread_run_installer(run_installer_main, &option, argv[0]);

        // メイン メッセージ ループ
        {
            MSG msg;
            while (GetMessage(&msg, nullptr, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        if (thread_run_installer.joinable()) {
            thread_run_installer.join();
        }
        return 0;
    }

    return run_installer_main(&option, argv[0]);
}
