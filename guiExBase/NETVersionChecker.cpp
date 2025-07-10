#include <Windows.h>
#include <tchar.h>
#include <stdio.h>
#pragma comment(lib, "Advapi32.lib")

static const TCHAR *KEY_NET_1_1 = _T("SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v1.1.4322");
static const TCHAR *KEY_NET_2_0 = _T("SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v2.0.50727");
static const TCHAR *KEY_NET_3_0 = _T("SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v3.0");
static const TCHAR *KEY_NET_3_5 = _T("SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v3.5");
static const TCHAR *KEY_NET_4_0_C = _T("SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Client");
static const TCHAR *KEY_NET_4_0_F = _T("SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full");
static const TCHAR *KEY_NET_4_5   = _T("SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full");
static const DWORD RELEASE_VER_NET_4_5          = 378389;
static const DWORD RELEASE_VER_NET_4_5_1        = 378675;
static const DWORD RELEASE_VER_NET_4_5_2        = 379893;
static const DWORD RELEASE_VER_NET_4_6          = 393295;
static const DWORD RELEASE_VER_NET_4_6_1        = 394254;
static const DWORD RELEASE_VER_NET_4_6_2        = 394802;
static const DWORD RELEASE_VER_NET_4_7          = 460798;
static const DWORD RELEASE_VER_NET_4_7_1        = 461308;
static const DWORD RELEASE_VER_NET_4_7_2        = 461808;
static const DWORD RELEASE_VER_NET_4_8          = 528040;

int check_net_framework_version(HKEY mainKey, const TCHAR *subKey, int *language_pack, DWORD releaseVer = 0) {
    HKEY hKey;
    DWORD getValue, Type, Size = sizeof(DWORD);
    int ret = -1;
    if (ERROR_SUCCESS == RegOpenKeyEx(mainKey, subKey, 0, KEY_READ, &hKey)) {
        if (ERROR_SUCCESS == RegQueryValueEx(hKey, _T("Install"), NULL, &Type, (BYTE *)&getValue, &Size) && getValue == 1) {
            ret = 0;
            if (releaseVer) {
                if (ERROR_SUCCESS == RegQueryValueEx(hKey, _T("Release"), NULL, &Type, (BYTE *)&getValue, &Size)) {
                    if ((DWORD)getValue >= releaseVer) {
                        ret = 0;
                    }
                } else {
                    ret = -1;
                }
            } else {
                if (ERROR_SUCCESS == RegQueryValueEx(hKey, _T("SP"), NULL, &Type, (BYTE *)&getValue, &Size))
                    ret = (int)getValue;
            }
        }
        RegCloseKey(hKey);
    }
    if (ret >= 0 && language_pack) {
        static const TCHAR *language_pack_key_appendix[2] = { _T("\\1041"), _T("\\setup\\1041") };
        *language_pack = -1;

        DWORD appendix_max_len = 0;
        for (int i = 0; i < _countof(language_pack_key_appendix); i++)
            appendix_max_len = max(appendix_max_len, _tcslen(language_pack_key_appendix[i]));
        int subKeylen = _tcslen(subKey) + appendix_max_len + 1;
        TCHAR *laguage_pack_key = (TCHAR *)malloc(sizeof(laguage_pack_key[0]) * subKeylen);
        if (laguage_pack_key) {
            for (int i = 0; *language_pack == -1 && i < _countof(language_pack_key_appendix); i++) {
                _stprintf_s(laguage_pack_key, subKeylen, _T("%s%s"), subKey, language_pack_key_appendix[i]);
                *language_pack = check_net_framework_version(mainKey, laguage_pack_key, NULL, releaseVer);
            }
        }
        if (laguage_pack_key)      free(laguage_pack_key);
    }
    return ret;
}

int check_net_1_1(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_1_1, language_pack);
}

int check_net_2_0(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_2_0, language_pack);
}

int check_net_3_0(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_3_0, language_pack);
}

int check_net_3_5(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_3_5, language_pack);
}

int check_net_4_0_Client(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_4_0_C, language_pack);
}

int check_net_4_0_Full(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_4_0_F, language_pack);
}

int check_net_4_5(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_4_5, language_pack, RELEASE_VER_NET_4_5);
}

int check_net_4_5_1(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_4_5, language_pack, RELEASE_VER_NET_4_5_1);
}

int check_net_4_5_2(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_4_5, language_pack, RELEASE_VER_NET_4_5_2);
}

int check_net_4_6(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_4_5, language_pack, RELEASE_VER_NET_4_6);
}

int check_net_4_6_1(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_4_5, language_pack, RELEASE_VER_NET_4_6_1);
}

int check_net_4_6_2(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_4_5, language_pack, RELEASE_VER_NET_4_6_2);
}

int check_net_4_7(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_4_5, language_pack, RELEASE_VER_NET_4_7);
}

int check_net_4_7_1(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_4_5, language_pack, RELEASE_VER_NET_4_7_1);
}

int check_net_4_7_2(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_4_5, language_pack, RELEASE_VER_NET_4_7_2);
}

int check_net_4_8(int *language_pack) {
    return check_net_framework_version(HKEY_LOCAL_MACHINE, KEY_NET_4_5, language_pack, RELEASE_VER_NET_4_8);
}
