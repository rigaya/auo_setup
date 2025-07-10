#include <Windows.h>
#include <VersionHelpers.h>
#include <tchar.h>
#include <signal.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <vector>
#include "auo_pipe.h"
#include "auo_setup_common.h"

static const TCHAR *AUO_SETUP_MUTEX_NAME = _T("auo_setup_avoid_multirun_mutex");
static const TCHAR *AUOSETUP_EVENT_ABORT = _T("AUOSETUP_EVENT_ABORT");

//PathRemoveFileSpecFixedがVistaでは5C問題を発生させるため、その回避策
BOOL PathRemoveFileSpecFixed(TCHAR *path) {
    TCHAR *ptr = PathFindFileName(path);
    if (path == ptr)
        return FALSE;
    *(ptr - 1) = _T('\0');
    return TRUE;
}

void get_aviutl_dir(TCHAR *aviutl_dir, size_t nSize) {
    GetModuleFileName(NULL, aviutl_dir, (DWORD)nSize);
    PathRemoveFileSpec(aviutl_dir);
}


bool check_dll_can_be_loaded(const TCHAR *dllpath) {
    const DWORD dwOldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS);
    HMODULE hmodule = LoadLibrary(dllpath);
    if (hmodule) {
        FreeLibrary(hmodule);
        return true;
    }
    SetErrorMode(dwOldErrorMode);
    return false;
}

bool check_admin_required() {
    if (!IsWindowsVistaOrGreater()) {
        return false;
    }
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }
    DWORD cb = 0;
    TOKEN_ELEVATION elevation;
    if (!GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cb)) {
        return false;
    }
    TOKEN_ELEVATION_TYPE elevtype;
    if (!GetTokenInformation(hToken, TokenElevationType, &elevtype, sizeof(elevtype), &cb)) {
        return false;
    }
    //昇格ができないor不要である
    if (elevtype == TokenElevationTypeDefault) {
        return false;
    }
    if (elevation.TokenIsElevated) {
        return false;
    }
    return true;
}

static bool func_abort_always_false() {
    return false;
}

//PeekNamedPipeが失敗→プロセスが終了していたら-1
static int read_from_pipe(PIPE_SET *pipes, BOOL fromStdErr) {
    DWORD pipe_read = 0;
    HANDLE h_read = (fromStdErr) ? pipes->stdErr.h_read : pipes->stdOut.h_read;
    if (!PeekNamedPipe(h_read, NULL, 0, NULL, &pipe_read, NULL))
        return -1;
    if (pipe_read) {
        ReadFile(h_read, pipes->read_buf + pipes->buf_len, sizeof(pipes->read_buf) - pipes->buf_len - 1, &pipe_read, NULL);
        pipes->buf_len += pipe_read;
        pipes->read_buf[pipes->buf_len] = _T('\0');
    }
    return pipe_read;
}

static void wait_for_process(const PROCESS_INFORMATION *pi, const TCHAR *mes, PIPE_SET *pipes, BOOL get_exe_message, func_abort abort) {
    for (int i = 0; WAIT_TIMEOUT == WaitForSingleObject(pi->hProcess, TIME_OUT) && !abort(); i++) {
        if (get_exe_message) {
            if (read_from_pipe(pipes, pipes->stdOut.mode == AUO_PIPE_DISABLE) > 0) {
                _ftprintf(stderr, _T("%s"), pipes->read_buf);
                pipes->buf_len = 0;
            }
        } else {
            static const TCHAR *STR[] = { _T(".         "), _T("..        "), _T("...       "), _T("....      "), _T(".....     "), _T("......    "), _T("......   "), _T(".......  "), _T("........ "), _T(".........") };
            _ftprintf(stderr, _T("\r%s%s"), mes, STR[i % _countof(STR)]);
        }
    }
    if (get_exe_message && !abort()) {
        while (read_from_pipe(pipes, pipes->stdOut.mode == AUO_PIPE_DISABLE) > 0) {
            _ftprintf(stderr, _T("%s"), pipes->read_buf);
            pipes->buf_len = 0;
        }
    }
}

static inline const TCHAR *GetFullPath(const TCHAR *path, TCHAR *buffer, size_t nSize) {
    if (PathIsRelative(path) == FALSE)
        return path;

    _tfullpath(buffer, path, nSize);
    return buffer;
}

int RunInstaller(const TCHAR *exe_path, const TCHAR *args, const TCHAR *dir, const TCHAR *mes, BOOL wait, BOOL hide, BOOL get_exe_message, BOOL quiet, DWORD *return_code, func_abort abort) {
    TCHAR dir_full[1024] = { 0 };
    TCHAR cmd[2048] = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    PIPE_SET pipes = { 0 };
    InitPipes(&pipes);
    if (get_exe_message) {
        wait = TRUE;
        pipes.stdOut.mode = AUO_PIPE_ENABLE;
    }
    if (abort == nullptr) {
        abort = func_abort_always_false;
    }

    _stprintf_s(cmd, _countof(cmd), _T("\"%s\" %s"), exe_path, args);
    int ret = RunProcess(cmd, (dir) ? GetFullPath(dir, dir_full, _countof(dir_full)) : nullptr, &pi, (get_exe_message) ? &pipes : NULL, GetPriorityClass(GetCurrentProcess()), hide, hide);
    if (RP_SUCCESS == ret) {
        if (wait) {
            wait_for_process(&pi, mes, &pipes, get_exe_message, abort);
            if (abort()) {
                TerminateProcess(pi.hProcess, SIGINT);
                *return_code = ERROR_OPERATION_ABORTED;
            } else {
                GetExitCodeProcess(pi.hProcess, return_code);
            }
        } else {
            WaitForInputIdle(pi.hProcess, INFINITE);
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (wait && !quiet) {
            if (get_exe_message)
                _ftprintf(stderr, _T("\n"));
            _ftprintf(stderr, _T("\r%s完了...             \n"), mes);
        }
    }
    return ret;
}

int start_installer_elevated(const TCHAR *exe_path, const TCHAR *cmd, HANDLE& processHandle, bool hide) {
    SHELLEXECUTEINFO sei = { 0 };
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.nShow = (hide) ? SW_HIDE : SW_SHOWNORMAL;
    sei.lpVerb = _T("runas");
    sei.lpFile = exe_path;
    sei.lpParameters = cmd;
    if (ShellExecuteEx(&sei) == FALSE) {
        return 1;
    }
    processHandle = sei.hProcess;
    return 0;
}

std::unique_ptr<HANDLE, HandleDeleter> avoid_multiple_run_of_auo_setup_exe() {
    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, 0, FALSE);
    SECURITY_ATTRIBUTES secAttribute;
    secAttribute.nLength = sizeof(secAttribute);
    secAttribute.lpSecurityDescriptor = &sd;
    secAttribute.bInheritHandle = TRUE;

    std::unique_ptr<HANDLE, HandleDeleter> hMutex(CreateMutex(&secAttribute, FALSE, AUO_SETUP_MUTEX_NAME), HandleDeleter());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        hMutex.reset();
    }
    return hMutex;
}

std::unique_ptr<HANDLE, HandleDeleter> create_abort_event() {
    TCHAR event_name[1024];
    _stprintf_s(event_name, _T("%s_0x%8x"), AUOSETUP_EVENT_ABORT, GetCurrentProcessId());
    return std::unique_ptr<HANDLE, HandleDeleter>(CreateEvent(NULL, TRUE, FALSE, event_name));
}

std::unique_ptr<HANDLE, HandleDeleter> open_abort_event(const DWORD pid) {
    TCHAR event_name[1024];
    _stprintf_s(event_name, _T("%s_0x%8x"), AUOSETUP_EVENT_ABORT, pid);
    return std::unique_ptr<HANDLE, HandleDeleter>(OpenEvent(EVENT_ALL_ACCESS, FALSE, event_name));
}

void AddTextBoxLine(HWND hwnd, const TCHAR *format, ...) {
    va_list args;
    va_start(args, format);
    int len = _vsctprintf(format, args) // _vscprintf doesn't count
        + 1; // terminating '\0'
    std::vector<TCHAR> buf(len, 0);
    _vstprintf_s(buf.data(), buf.size(), format, args);

    //エディットすべての文字列の長さを取得
    len = SendMessage(hwnd, WM_GETTEXTLENGTH, 0, 0);
    //一番最後にカーソルを移動
    SendMessage(hwnd, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    //文字列を挿入（置き換え）
    SendMessage(hwnd, EM_REPLACESEL, (WPARAM)false, (LPARAM)buf.data());
    //改行を追加
    //SendDlgItemMessage(hEdit, IDC_EDIT, EM_REPLACESEL, (WPARAM)false, (LPARAM)TEXT("\r\r\n"));
}
