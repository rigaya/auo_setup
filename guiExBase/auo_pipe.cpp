//  -----------------------------------------------------------------------------------------
//    拡張 x264/x265 出力(GUI) Ex  v1.xx/2.xx/3.xx by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  -----------------------------------------------------------------------------------------

#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <tchar.h>

#include "auo_pipe.h"
#include "auo_setup_common.h"

//参考 : http://support.microsoft.com/kb/190351/ja
//参考 : http://www.autch.net/page/tips/win32_anonymous_pipe.html
//参考 : http://www.monzen.org/blogn/index.php?e=43&PHPSESSID=o1hmtphk82cd428g8p09tf84e6

static const int PIPE_MAX_PATH_LEN = 1024;

void InitPipes(PIPE_SET *pipes) {
    ZeroMemory(pipes, sizeof(PIPE_SET));
}

static int StartPipes(PIPE_SET *pipes) {
    int ret = RP_USE_NO_PIPE;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (pipes->stdOut.mode) {
        if (!CreatePipe(&pipes->stdOut.h_read, &pipes->stdOut.h_write, &sa, pipes->stdOut.bufferSize) || 
            !SetHandleInformation(pipes->stdOut.h_read, HANDLE_FLAG_INHERIT, 0))
            return RP_ERROR_OPEN_PIPE;
        ret = RP_SUCCESS;
    }
    if (pipes->stdErr.mode) {
        if (!CreatePipe(&pipes->stdErr.h_read, &pipes->stdErr.h_write, &sa, pipes->stdErr.bufferSize) ||
            !SetHandleInformation(pipes->stdErr.h_read, HANDLE_FLAG_INHERIT, 0))
            return RP_ERROR_OPEN_PIPE;
        ret = RP_SUCCESS;
    }
    if (pipes->stdIn.mode) {
        if (!CreatePipe(&pipes->stdIn.h_read, &pipes->stdIn.h_write, &sa, pipes->stdIn.bufferSize) ||
            !SetHandleInformation(pipes->stdIn.h_write, HANDLE_FLAG_INHERIT, 0))
            return RP_ERROR_OPEN_PIPE;
        if ((pipes->f_stdin = _fdopen(_open_osfhandle((intptr_t)pipes->stdIn.h_write, _O_BINARY), "wb")) == NULL) {
            return RP_ERROR_GET_STDIN_FILE_HANDLE;
        }
        ret = RP_SUCCESS;
    }
    return ret;
}

int RunProcess(TCHAR *args, const TCHAR *exe_dir, PROCESS_INFORMATION *pi, PIPE_SET *pipes, DWORD priority, BOOL hidden, BOOL minimized) {
    BOOL Inherit = FALSE;
    DWORD flag = priority;
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(STARTUPINFO));
    ZeroMemory(pi, sizeof(PROCESS_INFORMATION));
    si.cb = sizeof(STARTUPINFO);

    int ret = (pipes) ? StartPipes(pipes) : RP_USE_NO_PIPE;
    if (ret > RP_SUCCESS)
        return ret;

    if (ret == RP_SUCCESS) {
        if (pipes->stdOut.mode)
            si.hStdOutput = pipes->stdOut.h_write;
        if (pipes->stdErr.mode)
            si.hStdError = (pipes->stdErr.mode == AUO_PIPE_MUXED) ? pipes->stdOut.h_write : pipes->stdErr.h_write;
        if (pipes->stdIn.mode)
            si.hStdInput = pipes->stdIn.h_read;
        si.dwFlags |= STARTF_USESTDHANDLES;
        Inherit = TRUE;
        //flag |= DETACHED_PROCESS; //このフラグによるコンソール抑制よりCREATE_NO_WINDOWの抑制を使用する
    }
    if (minimized) {
        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow |= SW_SHOWMINNOACTIVE;
    }
    if (hidden)
        flag |= CREATE_NO_WINDOW;

    if (!PathIsDirectory(exe_dir))
        exe_dir = NULL; //とりあえずカレントディレクトリで起動しとく

    ret = (CreateProcess(NULL, args, NULL, NULL, Inherit, flag, NULL, exe_dir, &si, pi)) ? RP_SUCCESS : RP_ERROR_CREATE_PROCESS;

    if (pipes) {
        if (pipes->stdOut.mode) {
            CloseHandle(pipes->stdOut.h_write);
            if (ret != RP_SUCCESS) {
                CloseHandle(pipes->stdOut.h_read);
                pipes->stdOut.mode = AUO_PIPE_DISABLE;
            }
        }
        if (pipes->stdErr.mode) {
            if (pipes->stdErr.mode)
                CloseHandle(pipes->stdErr.h_write);
            if (ret != RP_SUCCESS) {
                CloseHandle(pipes->stdErr.h_read);
                pipes->stdErr.mode = AUO_PIPE_DISABLE;
            }
        }
        if (pipes->stdIn.mode) {
            CloseHandle(pipes->stdIn.h_read);
            if (ret != RP_SUCCESS) {
                CloseHandle(pipes->stdIn.h_write);
                pipes->stdIn.mode = AUO_PIPE_DISABLE;
            }
        }
    }

    return ret;
}

void CloseStdIn(PIPE_SET *pipes) {
    if (pipes->stdIn.mode) {
        _fclose_nolock(pipes->f_stdin);
        CloseHandle(pipes->stdIn.h_write);
        pipes->stdIn.mode = AUO_PIPE_DISABLE;
    }
}

//PeekNamedPipeが失敗→プロセスが終了していたら-1
static int read_from_pipe(PIPE_SET *pipes, BOOL fromStdErr) {
    DWORD pipe_read = 0;
    HANDLE h_read = (fromStdErr) ? pipes->stdErr.h_read : pipes->stdOut.h_read;
    if (!PeekNamedPipe(h_read, NULL, 0, NULL, &pipe_read, NULL))
        return -1;
    if (pipe_read) {
        ReadFile(h_read, (char*)pipes->read_buf + pipes->buf_len, (sizeof(pipes->read_buf) / sizeof(TCHAR) - pipes->buf_len - 1) * sizeof(TCHAR), &pipe_read, NULL);
        pipes->buf_len += pipe_read / sizeof(TCHAR);
        pipes->read_buf[pipes->buf_len] = _T('\0');
    }
    return pipe_read;
}

//失敗... TRUE / 成功... FALSE
BOOL get_exe_message(const TCHAR *exe_path, const TCHAR *args, TCHAR *buf, size_t nSize, AUO_PIPE_MODE stderr_mode) {
    BOOL ret = FALSE;
    TCHAR exe_dir[PIPE_MAX_PATH_LEN];
    size_t len = _tcslen(exe_path) + _tcslen(args) + 5;
    TCHAR *const fullargs = (TCHAR*)malloc(len * sizeof(TCHAR));
    PROCESS_INFORMATION pi;
    PIPE_SET pipes;

    InitPipes(&pipes);
    pipes.stdErr.mode = stderr_mode;
    pipes.stdOut.mode = (stderr_mode == AUO_PIPE_ENABLE) ? AUO_PIPE_DISABLE : AUO_PIPE_ENABLE;
    buf[0] = _T('\0');

    _tcscpy_s(exe_dir, _countof(exe_dir), exe_path);
    PathRemoveFileSpec(exe_dir);

    _stprintf_s(fullargs, len, _T("\"%s\" %s"), exe_path, args);
    if ((ret = RunProcess(fullargs, exe_dir, &pi, &pipes, NORMAL_PRIORITY_CLASS, TRUE, FALSE)) == RP_SUCCESS) {
        while (WAIT_TIMEOUT == WaitForSingleObject(pi.hProcess, 10)) {
            if (read_from_pipe(&pipes, pipes.stdOut.mode == AUO_PIPE_DISABLE) > 0) {
                _tcscat_s(buf, nSize, pipes.read_buf);
                pipes.buf_len = 0;
            }
        }

        while (read_from_pipe(&pipes, pipes.stdOut.mode == AUO_PIPE_DISABLE) > 0) {
            _tcscat_s(buf, nSize, pipes.read_buf);
            pipes.buf_len = 0;
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    free(fullargs);
    if (pipes.stdErr.mode) CloseHandle(pipes.stdErr.h_read);
    if (pipes.stdOut.mode) CloseHandle(pipes.stdOut.h_read);

    return ret;
}

//改行コードの'\r\n'への変換
//対応   '\n'   → '\r\n'
//       '\r\n' → '\r\n'
//非対応 '\r'   → '\r\n'
static void write_to_file_in_crlf(FILE *fp, TCHAR *str) {
    if (((size_t)fp & (size_t)str) != NULL) {
        TCHAR *const fin_ptr = str + _tcslen(str);
        for (TCHAR *ptr = str, *qtr = NULL; ptr < fin_ptr; ptr = qtr+1) {
            qtr = _tcschr(ptr, _T('\n'));
            if (qtr == NULL) {
                _fputts(ptr, fp);
                break;
            } else {
                if (qtr != ptr) {
                    int cr_count;
                    for (cr_count = 0 ; qtr - cr_count >= ptr; cr_count++)
                        if (qtr[-1-cr_count] != _T('\r'))
                            break;
                    *qtr = _T('\0');
                    _fputts(ptr, fp);
                    *qtr = _T('\n');
                }
                _fputts(_T("\r\n"), fp);
            }
        }
    }
}

//実行ファイルのメッセージをファイルに追記モードで書き出す
//失敗... TRUE / 成功... FALSE
BOOL get_exe_message_to_file(const TCHAR *exe_path, const TCHAR *args, const TCHAR *filepath, AUO_PIPE_MODE stderr_mode, DWORD loop_ms) {
    BOOL ret = FALSE;
    TCHAR exe_dir[PIPE_MAX_PATH_LEN];
    size_t len = _tcslen(exe_path) + _tcslen(args) + 5;
    TCHAR *const fullargs = (TCHAR*)malloc(len * sizeof(TCHAR));
    PROCESS_INFORMATION pi;
    PIPE_SET pipes;

    InitPipes(&pipes);
    pipes.stdErr.mode = stderr_mode;
    pipes.stdOut.mode = (stderr_mode == AUO_PIPE_ENABLE) ? AUO_PIPE_DISABLE : AUO_PIPE_ENABLE;

    _tcscpy_s(exe_dir, _countof(exe_dir), exe_path);
    PathRemoveFileSpec(exe_dir);

    FILE *fp = NULL;
    if (_tfopen_s(&fp, filepath, _T("ab")) == NULL && fp) {
        _stprintf_s(fullargs, len, _T("\"%s\" %s"), exe_path, args);
        if ((ret = RunProcess(fullargs, exe_dir, &pi, &pipes, NORMAL_PRIORITY_CLASS, TRUE, FALSE)) == RP_SUCCESS) {
            while (WAIT_TIMEOUT == WaitForSingleObject(pi.hProcess, loop_ms)) {
                if (read_from_pipe(&pipes, pipes.stdOut.mode == AUO_PIPE_DISABLE) > 0) {
                    write_to_file_in_crlf(fp, pipes.read_buf);
                    pipes.buf_len = 0;
                }
            }

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            while (read_from_pipe(&pipes, pipes.stdOut.mode == AUO_PIPE_DISABLE) > 0) {
                write_to_file_in_crlf(fp, pipes.read_buf);
                pipes.buf_len = 0;
            }
        }
        fclose(fp);
    }

    free(fullargs);
    if (pipes.stdErr.mode) CloseHandle(pipes.stdErr.h_read);
    if (pipes.stdOut.mode) CloseHandle(pipes.stdOut.h_read);

    return ret;
}
