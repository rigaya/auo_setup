#include <Windows.h>
#include <VersionHelpers.h>
#include <stdio.h>
#include <signal.h>
#include <tchar.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <vector>
#include "NETVersionChecker.h"
#include "auo_pipe.h"
#include "auo_setup_common.h"

#define ENABLE_CURL_DOWNLOAD 0

#if ENABLE_CURL_DOWNLOAD
#include "DownloadLibcurl.h"
#endif

static const bool CHECK_DOT_NET_BY_DLL = true;

static std::unique_ptr<HANDLE, HandleDeleter> eventAbort;
static HWND parent_hwndEdit;

enum InstallerResult {
    INSTALLER_RESULT_REQUIRE_REBOOT = -2,
    INSTALLER_RESULT_REQUIRE_ADMIN = -1,
    INSTALLER_RESULT_SUCCESS = 0,
    INSTALLER_RESULT_ERROR = 1,
    INSTALLER_RESULT_ABORT = 2,
};

typedef struct setup_options_t {
    char install_dir[1024];
    BOOL force_vc_install;
    BOOL force_dotnet_install;
    BOOL quiet;
    BOOL no_debug;
    DWORD parent_pid;
    HWND parent_hwndEdit;
} setup_options_t;

//Ctrl + C ハンドラ
static bool g_signal_abort = false;
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

static void print_message(const TCHAR *format, ...) {
    va_list args;
    va_start(args, format);
    int len = _vscprintf(format, args) // _vscprintf doesn't count
        + 1; // terminating '\0'
    std::vector<char> buf(len, 0);
    vsprintf_s(buf.data(), buf.size(), format, args);

    if (parent_hwndEdit) {
        AddTextBoxLine(parent_hwndEdit, "%s", buf.data());
    }
    fprintf(stderr, "%s", buf.data());
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

bool check_if_dll_exists(const char *dllname) {
    HMODULE hmodule = LoadLibrary(dllname);
    if (hmodule) {
        FreeLibrary(hmodule);
        return true;
    }
    return false;
}

InstallerResult check_vc_runtime(const char *exe_dir, BOOL quiet, BOOL force_install) {
    char buf[2048] = { 0 };
    char installer_ini[1024] = { 0 };
    PathCombine(installer_ini, exe_dir, INSTALLER_INI);

    if (!force_install) {
        bool dll_check = false;
        PathCombine(buf, exe_dir, VC_RUNTIME_CHECKER);
        if (check_dll_can_be_loaded(buf)) {
            dll_check = true;
        }
        if (dll_check) {
            GetPrivateProfileString(INSTALLER_INI_SECTION, "vc_runtime_dll", "", buf, sizeof(buf), installer_ini);
            char *ctx = nullptr;
            for (char *next = strtok_s(buf, ",", &ctx); next != nullptr; next = strtok_s(nullptr, ",", &ctx)) {
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

    char path_installer[1024] = { 0 };
    PathCombine(path_installer, exe_dir, VC_RUNTIME_INSTALLER);
    if (!PathFileExists(path_installer)) {
#if ENABLE_CURL_DOWNLOAD
        GetPrivateProfileString(INSTALLER_INI_SECTION, "vc_runtime", "", buf, sizeof(buf), installer_ini);
        if (strlen(buf) > 0) {
            fprintf_check("VC runtime のインストーラをダウンロードします。\n");
            LibcurlSimpleDownloader downloader;
            downloader.set_quiet(!!quiet);
            if (downloader.download_to_file(buf, path_installer)) {
                print_message("%s の記述が正しくありません。VC runtimeのダウンロードに失敗しました。\n", INSTALLER_INI);
                return INSTALLER_RESULT_ERROR;
            }
        }
#endif
        if (!PathFileExists(path_installer)) {
            fprintf_check("VC runtime のインストーラが\n  %s\nに存在しません。\n", path_installer);
            fprintf_check("ダウンロードしたzipファイルの中身をすべてAviutlフォルダ内にコピーできているか、再度確認してください。\n");
            return INSTALLER_RESULT_ERROR;
        }
    }

    fprintf_check("\n");
    fprintf_check("VC runtime をインストールします。\n");
    DWORD return_code = 0;
    RunInstaller(path_installer, "/quiet /norestart", NULL, "VC runtime インストール", TRUE, TRUE, FALSE, quiet, &return_code, func_check_abort);
    fprintf_check("\n\n");
    remove(path_installer);
    if (   return_code == ERROR_SUCCESS_REBOOT_INITIATED
        || return_code == ERROR_SUCCESS_REBOOT_REQUIRED) {
        return INSTALLER_RESULT_REQUIRE_REBOOT;
    } else if (return_code == ERROR_OPERATION_ABORTED) {
        fprintf_check("VC runtime のインストールを中断しました。\n");
        return INSTALLER_RESULT_ABORT;
    } else if (return_code != ERROR_SUCCESS) {
        fprintf_check("VC runtime のインストールでエラーが発生しました。\n");

        char buffer[1024];
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, return_code, 0, buffer, sizeof(buffer), NULL);
        fprintf_check("%s\n", buffer);
        return INSTALLER_RESULT_ERROR;
    }
    return INSTALLER_RESULT_SUCCESS;
}

static void write_net_framework_install_error(DWORD return_code, BOOL quiet) {
    switch (return_code) {
    case ERROR_INSTALL_USEREXIT:
        fprintf_check("ユーザーによりキャンセルされました。\n");
        break;
    case ERROR_INSTALL_FAILURE:
        fprintf_check("ユーザーによりキャンセルされました。\n");
        break;
    case 5100:
        fprintf_check("空きディスク容量が不足しているか、システムがインストールの必要要件を満たしていません。\n");
        break;
    default: {
        char buffer[1024];
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, return_code, 0, buffer, sizeof(buffer), NULL);
        fprintf_check("%s\n", buffer);
    } break;
    }
}

InstallerResult check_net_framework(const char *exe_dir, BOOL quiet, BOOL force_install) {
    char buf[2048] = { 0 };
    char installer_ini[1024] = { 0 };
    PathCombine(installer_ini, exe_dir, INSTALLER_INI);
    GetPrivateProfileString(INSTALLER_INI_SECTION, "net_ver", "", buf, sizeof(buf), installer_ini);
    char net_ver[256] = { 0 };
    sprintf_s(net_ver, _countof(net_ver), ".NET Framework %s", buf);
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
        if      (strstr(buf, "1.1"))         ret = check_net_1_1(&language_pack);
        else if (strstr(buf, "2.0"))         ret = check_net_2_0(&language_pack);
        else if (strstr(buf, "3.0"))         ret = check_net_3_0(&language_pack);
        else if (strstr(buf, "3.5"))         ret = check_net_3_5(&language_pack);
        else if (strstr(buf, "4.0 Client"))  ret = check_net_4_0_Client(&language_pack);
        else if (strstr(buf, "4.0 Full"))    ret = check_net_4_0_Full(&language_pack);
        else if (strstr(buf, "4.0"))         ret = check_net_4_0_Client(&language_pack);
        else if (strstr(buf, "4.5"))         ret = check_net_4_5(&language_pack);
        else if (strstr(buf, "4.5.1"))       ret = check_net_4_5_1(&language_pack);
        else if (strstr(buf, "4.5.2"))       ret = check_net_4_5_2(&language_pack);
        else if (strstr(buf, "4.6"))         ret = check_net_4_6(&language_pack);
        else if (strstr(buf, "4.6.1"))       ret = check_net_4_6_1(&language_pack);
        else if (strstr(buf, "4.6.2"))       ret = check_net_4_6_2(&language_pack);
        else if (strstr(buf, "4.7"))         ret = check_net_4_7(&language_pack);
        else if (strstr(buf, "4.7.1"))       ret = check_net_4_7_1(&language_pack);
        else if (strstr(buf, "4.7.2"))       ret = check_net_4_7_2(&language_pack);
        else if (strstr(buf, "4.8"))         ret = check_net_4_8(&language_pack);
        else return INSTALLER_RESULT_ERROR;
    }

    if (!force_install && ret >= 0) {
        fprintf_check("%s はすでにインストールされています。\n", net_ver);
    } else {
        if (check_admin_required()) {
            return INSTALLER_RESULT_REQUIRE_ADMIN;
        }

        char path_installer[1024] = { 0 };
        PathCombine(path_installer, exe_dir, DOT_NET_RUNTIME_INSTALLER);
        if (!PathFileExists(path_installer)) {
#if ENABLE_CURL_DOWNLOAD
            GetPrivateProfileString(INSTALLER_INI_SECTION, "net_url", "", buf, sizeof(buf), installer_ini);
            if (strlen(buf) > 0) {
                fprintf_check("%s のインストーラをダウンロードします。\n", net_ver);
                LibcurlSimpleDownloader downloader;
                downloader.set_quiet(!!quiet);
                if (downloader.download_to_file(buf, path_installer)) {
                    print_message("%s の記述が正しくありません。%s インストーラのダウンロードに失敗しました。\n", INSTALLER_INI, net_ver);
                    return INSTALLER_RESULT_ERROR;
                }
            }
#endif
            if (!PathFileExists(path_installer)) {
                fprintf_check("%s のインストーラが\n  %s\nに存在しません。\n", net_ver, path_installer);
                fprintf_check("ダウンロードしたzipファイルの中身をすべてAviutlフォルダ内にコピーできているか、再度確認してください。\n");
                return INSTALLER_RESULT_ERROR;
            }
        }

        char run_process_mes[1024] = { 0 };
        sprintf_s(run_process_mes, _countof(run_process_mes), "%s インストール", net_ver);
        fprintf_check("%s をインストールします。最大で10分ほどかかる場合があります。\n", net_ver);
        DWORD return_code = 0;
        RunInstaller(path_installer, "/q /LANG:ENG /norestart", NULL, run_process_mes, TRUE, TRUE, FALSE, quiet, &return_code, func_check_abort);
        fprintf_check("\n\n");
        remove(path_installer);
        if (   return_code == ERROR_SUCCESS_REBOOT_INITIATED
            || return_code == ERROR_SUCCESS_REBOOT_REQUIRED) {
            return INSTALLER_RESULT_REQUIRE_REBOOT;
        } else if (return_code == ERROR_OPERATION_ABORTED) {
            fprintf_check("%s のインストールを中断しました。\n", net_ver);
            return INSTALLER_RESULT_ABORT;
        } else if (return_code != ERROR_SUCCESS) {
            fprintf_check("%s のインストールでエラーが発生しました。\n", net_ver);
            write_net_framework_install_error(return_code, quiet);
            return INSTALLER_RESULT_ERROR;
        }
    }

    if (!force_install && language_pack >= 0) {
        if (ret < 0) {
            fprintf_check("%s 言語パック はすでにインストールされています。\n", net_ver);
        }
    } else {
        if (check_admin_required()) {
            return INSTALLER_RESULT_REQUIRE_ADMIN;
        }

        char path_installer[1024] = { 0 };
        PathCombine(path_installer, exe_dir, DOT_NET_LANGPACK_INSTALLER);
        if (!PathFileExists(path_installer)) {
#if ENABLE_CURL_DOWNLOAD
            GetPrivateProfileString(INSTALLER_INI_SECTION, "net_lang_url", "", buf, sizeof(buf), installer_ini);
            if (strlen(buf) > 0) {
                fprintf_check("%s 言語パック インストーラをダウンロードします。\n", net_ver);
                LibcurlSimpleDownloader downloader;
                downloader.set_quiet(!!quiet);
                if (downloader.download_to_file(buf, path_installer)) {
                    print_message("%sの記述が正しくありません。%s 言語パック のインストールに失敗しました。\n", INSTALLER_INI, net_ver);
                    return INSTALLER_RESULT_ERROR;
                }
            }
#endif
            if (!PathFileExists(path_installer)) {
                fprintf_check("%s 言語パックのインストーラが\n  %s\nに存在しません。\n", net_ver, path_installer);
                fprintf_check("ダウンロードしたzipファイルの中身をすべてAviutlフォルダ内にコピーできているか、再度確認してください。\n");
                return INSTALLER_RESULT_ERROR;
            }
        }

        char run_process_mes[1024] = { 0 };
        sprintf_s(run_process_mes, _countof(run_process_mes), "%s 言語パック インストール", net_ver);
        fprintf_check("%s をインストールします。5分ほどかかる場合があります。\n", net_ver);
        DWORD return_code = 0;
        RunInstaller(path_installer, "/q /norestart", NULL, run_process_mes, TRUE, TRUE, FALSE, quiet, &return_code, func_check_abort);
        fprintf_check("\n\n");
        remove(path_installer);
        if (   return_code == ERROR_SUCCESS_REBOOT_INITIATED
            || return_code == ERROR_SUCCESS_REBOOT_REQUIRED) {
            return INSTALLER_RESULT_REQUIRE_REBOOT;
        } else if (return_code == ERROR_OPERATION_ABORTED) {
            return INSTALLER_RESULT_ABORT;
        } else if (return_code != ERROR_SUCCESS) {
            fprintf_check("%s 言語パックのインストールでエラーが発生しました。\n", net_ver);
            write_net_framework_install_error(return_code, quiet);
            //言語パックのインストールエラーは無視
            //return INSTALLER_RESULT_ERROR;
        }
    }
    return INSTALLER_RESULT_SUCCESS;
}

void create_cmd_for_installer(TCHAR *cmd, size_t nSize, setup_options_t *option, BOOL for_main_installer = FALSE) {
    cmd[0] = '\0';
    if (option->no_debug)         strcat_s (cmd, nSize, "-no-debug ");
    if (for_main_installer) {
        if (option->force_vc_install)     strcat_s (cmd, nSize, "-force-vc ");
        if (option->force_dotnet_install) strcat_s (cmd, nSize, "-force-dotnet ");
    }
}

int restart_installer_elevated(const TCHAR *exe_path, setup_options_t *option,
    decltype(avoid_multiple_run_of_auo_setup_exe())& mutexHandle) {
    TCHAR cmd[4096] = { 0 };
    create_cmd_for_installer(cmd, _countof(cmd), option, TRUE);

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

static void print_help(const char *exe_path) {
    fprintf(stdout, "%s\n"
        "Aviutlの出力プラグインの簡易インストーラです。\n"
        "\n"
        "オプション\n"
        "-help                        このヘルプの表示\n"
        "\n"
        "-force-vc                    VC runtimeのインストールを強制します。\n"
        "-force-dotnet                .NET Frameworkのインストールを強制します。\n"
        "\n"
        "-no-debug                    インストール時のデバッグ出力を行いません。\n"
        "\n"
        "-quiet            コマンドプロンプトにメッセージを表示しない。\n",
        PathFindFileName(exe_path)
        );
}

int main(int argc, char **argv) {
    setup_options_t option = { 0 };
    parent_hwndEdit = NULL;

    for (int i_arg = 1; i_arg < argc; i_arg++) {
        if (0 == _stricmp(argv[i_arg], "-force-vc")) {
            option.force_vc_install = TRUE;
        } else if (0 == _stricmp(argv[i_arg], "-force-dotnet")) {
            option.force_dotnet_install = TRUE;
        } else if (0 == _stricmp(argv[i_arg], "-quiet")) {
            option.quiet = TRUE;
        } else if (0 == _stricmp(argv[i_arg], "-no-debug")) {
            option.no_debug = TRUE;
        } else if (0 == _stricmp(argv[i_arg], "-ppid")) {
            i_arg++;
            DWORD value = 0;
            if (sscanf_s(argv[i_arg], "0x%x", &value) == 1) {
                option.parent_pid = value;
            }
        } else if (0 == _stricmp(argv[i_arg], "-phwnd")) {
            i_arg++;
            DWORD value = 0;
            if (sscanf_s(argv[i_arg], "0x%x", &value) == 1) {
                parent_hwndEdit = (HWND)value;
            }
        } else if (0 == _stricmp(argv[i_arg], "-help")
            || 0 == _stricmp(argv[i_arg], "-h")) {
            print_help(argv[0]);
            return 1;
        }
    }

    char exe_dir[1024] = { 0 };
    strcpy_s(exe_dir, _countof(exe_dir), argv[0]);
    PathRemoveFileSpecFixed(exe_dir);

    if (check_exe_dir()) {
        print_message("簡易インストーラが環境依存文字を含むパスから実行されており、インストールを続行できません。\n");
        return 1;
    }

    char installer_ini[1024] = { 0 };
    PathCombine(installer_ini, exe_dir, INSTALLER_INI);
    if (!PathFileExists(installer_ini)) {
        print_message("%sが存在しません。インストールを続行できません。\n", INSTALLER_INI);
        print_message("ダウンロードしたzipファイルの中身をすべてAviutlフォルダ内にコピーできているか、再度確認してください。\n");
        return 1;
    }

    auto mutexHandle = avoid_multiple_run_of_auo_setup_exe();
    if (!mutexHandle) {
        print_message("すでに簡易インストーラが実行されているため、終了します。\n");
        return 0;
    }

    if (option.parent_pid != 0) {
        eventAbort = open_abort_event(option.parent_pid);
    }

    set_signal_handler();
#if ENABLE_CURL_DOWNLOAD
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    char install_name[1024] = { 0 };
    GetPrivateProfileString(INSTALLER_INI_SECTION, "name", "", install_name, sizeof(install_name), installer_ini);
    PathRemoveExtension(install_name);
    print_message("%s を使用できるようにするための準備を行います。\n", install_name);

    bool require_reboot = false;

    //VC runtimeのインストール
    int ret = check_vc_runtime(exe_dir, option.quiet, option.force_vc_install);
    if (ret == INSTALLER_RESULT_REQUIRE_ADMIN) {
        if (restart_installer_elevated(argv[0], &option, mutexHandle)) {
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
    ret = check_net_framework(exe_dir, option.quiet, option.force_dotnet_install);
    if (ret == INSTALLER_RESULT_REQUIRE_ADMIN) {
        if (restart_installer_elevated(argv[0], &option, mutexHandle)) {
            return 1;
        }
        return 0;
    } else if (ret == INSTALLER_RESULT_ABORT) {
        return 1;
    } else if (ret == INSTALLER_RESULT_REQUIRE_REBOOT) {
        require_reboot = true;
    } else if (ret > 0) {
        print_message("%sの記述が正しくありません。.NET Frameworkのインストールに失敗しました。\n\n", INSTALLER_INI);
        return 1;
    }
#if ENABLE_CURL_DOWNLOAD
    curl_global_cleanup();
#endif

    print_message("%s を使用する準備が完了しました。\n", install_name);
    if (require_reboot) {
        print_message("PCの再起動必要です。\n");
        print_message("PCの再起動後、%s が使用できるようになります。\n", install_name);
        return -1;
    }
    print_message("Aviutlの出力プラグインに %s が追加されているか、確認してください。\n", install_name);
    return 0;
}
