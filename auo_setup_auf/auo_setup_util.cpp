#include <Windows.h>
#include <string>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include "auo_setup_common.h"

static HWND aviutl_hwnd = NULL;

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD ProcessID;
    GetWindowThreadProcessId(hwnd, &ProcessID);
    if (GetCurrentProcessId() == ProcessID) {
        aviutl_hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND get_aviutl_hwnd() {
    aviutl_hwnd = NULL;
    EnumWindows(EnumWindowsProc, NULL);
    return aviutl_hwnd;
}
