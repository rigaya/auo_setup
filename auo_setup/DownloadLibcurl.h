#pragma once

#include <curl/curl.h>
#include <Windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <regex>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "Winhttp.lib")
#pragma comment(lib, "libcurl_a.lib")

std::wstring get_proxy(const WCHAR *targeturl) {
    std::wstring proxy = L"";

    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config = { 0 };
    WinHttpGetIEProxyConfigForCurrentUser(&config);

    if (config.fAutoDetect || (config.lpszAutoConfigUrl && wcslen(config.lpszAutoConfigUrl))) {
        auto hInternet = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        WINHTTP_PROXY_INFO info = { 0 };
        WINHTTP_AUTOPROXY_OPTIONS options = { 0 };
        if (config.fAutoDetect) {
            options.dwFlags = WINHTTP_AUTOPROXY_AUTO_DETECT;
            options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
        } else {
            options.dwFlags = WINHTTP_AUTOPROXY_CONFIG_URL;
            options.lpszAutoConfigUrl = config.lpszAutoConfigUrl;
        }
        WinHttpGetProxyForUrl(hInternet, (targeturl) ? targeturl : L"http://www.google.co.jp/", &options, &info);
        if (info.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY) {
            proxy = info.lpszProxy;
        }
    } else if (config.lpszProxy && wcslen(config.lpszProxy)) {
        proxy = config.lpszProxy;
    }
    return proxy;
}

unsigned int wstring_to_string(const wchar_t *wstr, std::string& str, uint32_t codepage = CP_THREAD_ACP) {
    if (wstr == nullptr) {
        str = "";
        return 0;
    }
    uint32_t flags = (codepage == CP_UTF8) ? 0 : WC_NO_BEST_FIT_CHARS;
    int multibyte_length = WideCharToMultiByte(codepage, flags, wstr, -1, nullptr, 0, nullptr, nullptr);
    str.resize(multibyte_length, 0);
    if (0 == WideCharToMultiByte(codepage, flags, wstr, -1, &str[0], multibyte_length, nullptr, nullptr)) {
        str.clear();
        return 0;
    }
    return multibyte_length;
}
std::string wstring_to_string(const wchar_t *wstr, uint32_t codepage = CP_THREAD_ACP) {
    if (wstr == nullptr) {
        return "";
    }
    std::string str;
    wstring_to_string(wstr, str, codepage);
    return str;
}
unsigned int char_to_wstring(std::wstring& wstr, const char *str, uint32_t codepage = CP_THREAD_ACP) {
    if (str == nullptr) {
        wstr = L"";
        return 0;
    }
    int widechar_length = MultiByteToWideChar(codepage, 0, str, -1, nullptr, 0);
    wstr.resize(widechar_length, 0);
    if (0 == MultiByteToWideChar(codepage, 0, str, -1, &wstr[0], (int)wstr.size())) {
        wstr.clear();
        return 0;
    }
    return widechar_length;
}
std::wstring char_to_wstring(const char *str, uint32_t codepage = CP_THREAD_ACP) {
    if (str == nullptr) {
        return L"";
    }
    std::wstring wstr;
    char_to_wstring(wstr, str, codepage);
    return wstr;
}

static int progress_func_do_nothing(void* ptr, double TotalToDownload, double NowDownloaded, double TotalToUpload, double NowUploaded) {
    return 0;
}
static int progress_func(void* ptr, double TotalToDownload, double NowDownloaded, double TotalToUpload, double NowUploaded) {
    DWORD tm = timeGetTime();
    static DWORD last_update = timeGetTime();
    if (tm - last_update >= 250 && TotalToDownload > 0.0) {
        fprintf(stderr, "\rダウンロード中... %.2f%%", NowDownloaded * 100.0 / TotalToDownload);
        last_update = tm;
    }
    return 0;
}

class LibcurlSimpleDownloader {
private:
    bool quiet;
    CURL *curl;
    FILE *fp_download_data;
    DWORD last_update;
public:
    LibcurlSimpleDownloader() {
        quiet = false;
        curl = NULL;
        fp_download_data = NULL;
        last_update = timeGetTime();
    };
    ~LibcurlSimpleDownloader() {
        if (fp_download_data) {
            fclose(fp_download_data);
            fp_download_data = NULL;
        }
        if (curl) {
            curl_easy_cleanup(curl); //ハンドラのクリーンアップ
            curl = NULL;
        }
    };
    void set_quiet(bool _quiet) {
        quiet = _quiet;
    }
    int download_to_file(const char *url, const char *filename) {
        if (fopen_s(&fp_download_data, filename, "wb") || NULL == fp_download_data) {
            fprintf(stderr, "ファイルのオープンに失敗しました。\n");
            return 1;
        }

        auto proxy = get_proxy(char_to_wstring(url).c_str());

        CURLcode res = CURLE_OK;
        long http_code = 0;
        if (   NULL     == (curl = curl_easy_init()) //ハンドラの初期化
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_URL, url)) //URLの登録
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_FAILONERROR, TRUE)) //HTTPで400以上のコードが返ってきた際に処理失敗と判断
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, TRUE)) //スレッドセーフに
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE))
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, FALSE))
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite)) //BODYを書き込む関数ポインタを登録    if (progress == NULL)))
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_NOPROGRESS, FALSE))
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, TRUE))
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, TRUE))
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_AUTOREFERER, TRUE))
            ||!(proxy.length() == 0 ||
               CURLE_OK == (res = curl_easy_setopt(curl, CURLOPT_PROXY, wstring_to_string(proxy.c_str()).c_str())))
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, TRUE)) //リダイレクト有効
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 100)) //最大リダイレクト数
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_POSTREDIR, 0x07)) //HTTP:301, 302, 303いずれでもリダイレクト
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, TRUE)) //リダイレクト時、ユーザー名とパスワードを送信し続けます
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64)")) //some servers don't like requests that are made without a user-agent field, so we provide one
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, (quiet) ? progress_func_do_nothing : progress_func))
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, NULL))
            || CURLE_OK != (res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp_download_data)) //↑で登録した関数が読み取るファイルポインタ
            || CURLE_OK != (res = curl_easy_perform(curl))                                     //いままで登録した情報で色々実行
            || CURLE_OK != (res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code))) { //ステータスコードの取得
            fprintf(stderr, "%s\n", curl_easy_strerror(res));
            return 1;
        }

        fprintf(stderr, "\rダウンロード 完了                       \n");

        fclose(fp_download_data);
        return 0;
    }
};
