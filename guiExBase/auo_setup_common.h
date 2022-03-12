//  -----------------------------------------------------------------------------------------
//    拡張 x264/x265 出力(GUI) Ex  v1.xx/2.xx/3.xx by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  -----------------------------------------------------------------------------------------

#ifndef _CHECK_RUNTIME_H_
#define _CHECK_RUNTIME_H_

#include <Windows.h>
#include <memory>
#include <functional>

static const int   MAX_PATH_LEN               = 1024;
static const int   TIME_OUT                   = 500;
static const char *INSTALLER_NAME             = "auo_setup.exe";
static const char *INSTALLER_INI              = "auo_setup.ini";
static const char *INSTALLER_INI_SECTION      = "AUO_INSTALLER";
static const char *DEFAULT_EXE_DIR            = "exe_files";
static const char *DEFAULT_PLUGINS_DIR        = "plugins";
static const char *VC_RUNTIME_CHECKER         = "check_vc.dll";
static const char *DOT_NET_RUNTIME_CHECKER    = "check_dotnet.dll";

static const char *VC_RUNTIME_INSTALLER       = "VC_redist.x86.exe";
static const char *DOT_NET_RUNTIME_INSTALLER  = "ndp48-web.exe";
static const char *DOT_NET_LANGPACK_INSTALLER = "ndp48-x86-x64-allos-jpn.exe";

BOOL PathRemoveFileSpecFixed(char *path);
void get_aviutl_dir(char *aviutl_dir, size_t nSize);
bool check_dll_can_be_loaded(const char *dllpath);
bool check_admin_required();
typedef bool (*func_abort)();
int RunInstaller(const char *exe_path, const char *args, const char *dir, const char *mes, BOOL wait, BOOL hide, BOOL get_exe_message, BOOL quiet, DWORD *return_code, func_abort abort = nullptr);
int start_installer_elevated(const TCHAR *exe_path, const TCHAR *cmd, HANDLE& processHandle, bool hide);

struct HandleDeleter {
    typedef HANDLE pointer;
    void operator()(HANDLE handle) {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};
std::unique_ptr<HANDLE, HandleDeleter> avoid_multiple_run_of_auo_setup_exe();
std::unique_ptr<HANDLE, HandleDeleter> create_abort_event();
std::unique_ptr<HANDLE, HandleDeleter> open_abort_event(const DWORD pid);

void AddTextBoxLine(HWND hwnd, const TCHAR *format, ...);

#endif //_CHECK_RUNTIME_H_
