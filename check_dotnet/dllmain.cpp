#include <Windows.h>

int funcDummy(int x);

BOOL APIENTRY DLLMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	int ret = funcDummy(10);
	return ret > 0;
}
