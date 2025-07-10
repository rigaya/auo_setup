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
#include <tchar.h>

static const int   MAX_PATH_LEN               = 1024;
static const int   TIME_OUT                   = 500;
static const TCHAR *INSTALLER_NAME             = _T("auo_setup.exe");
static const TCHAR *INSTALLER_INI              = _T("auo_setup.ini");
static const TCHAR *INSTALLER_INI_SECTION      = _T("AUO_INSTALLER");
static const TCHAR *DEFAULT_EXE_DIR            = _T("exe_files");
static const TCHAR *DEFAULT_PLUGINS_DIR        = _T("plugins");
static const TCHAR *VC_RUNTIME_CHECKER         = _T("check_vc.dll");
static const TCHAR *DOT_NET_RUNTIME_CHECKER    = _T("check_dotnet.dll");

static const TCHAR *VC_RUNTIME_INSTALLER       = _T("VC_redist.x86.exe");
static const TCHAR *DOT_NET_RUNTIME_INSTALLER  = _T("ndp48-web.exe");
static const TCHAR *DOT_NET_LANGPACK_INSTALLER = _T("ndp48-x86-x64-allos-jpn.exe");

BOOL PathRemoveFileSpecFixed(TCHAR *path);
void get_aviutl_dir(TCHAR *aviutl_dir, size_t nSize);
bool check_dll_can_be_loaded(const TCHAR *dllpath);
bool check_admin_required();
typedef bool (*func_abort)();
int RunInstaller(const TCHAR *exe_path, const TCHAR *args, const TCHAR *dir, const TCHAR *mes, BOOL wait, BOOL hide, BOOL get_exe_message, BOOL quiet, DWORD *return_code, func_abort abort = nullptr);
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
