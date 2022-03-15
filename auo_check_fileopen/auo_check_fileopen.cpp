#include <Windows.h>
#include <tchar.h>
#include <memory>
#include <functional>

using unique_handle = std::unique_ptr<std::remove_pointer<HANDLE>::type, std::function<void(HANDLE)>>;

int _tmain(int argc, TCHAR **argv) {
    if (argc == 2) {
        const TCHAR *filename = argv[1];
        auto handle = unique_handle(CreateFile(filename, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL),
            [](HANDLE h) { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); });
        if (handle.get() == INVALID_HANDLE_VALUE) {
            auto err = GetLastError();
            _ftprintf(stderr, _T("%s: INVALID_HANDLE_VALUE: %d\n"), filename, err);
            return err;
        }
        _ftprintf(stderr, _T("%s: ERROR_SUCCESS\n"), filename);
        handle.reset();
        DeleteFile(filename);
        return ERROR_SUCCESS;
    }
    return ERROR_INVALID_FUNCTION;
}