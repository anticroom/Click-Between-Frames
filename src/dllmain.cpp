#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

HMODULE g_module = nullptr;

void modLoaded();

DWORD WINAPI thread_func(void*) {
	// quickldr can get us in before cocos is around and we hook one of its exports
	for (int i = 0; i < 200 && !GetModuleHandleA("libcocos2d.dll"); i++) Sleep(50);

	modLoaded();
	return 0;
}

BOOL APIENTRY DllMain(HMODULE handle, DWORD reason, LPVOID reserved) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_module = handle;
		DisableThreadLibraryCalls(handle);

		auto h = CreateThread(0, 0, thread_func, handle, 0, 0);
		if (h)
			CloseHandle(h);
		else
			return FALSE;
	}
	return TRUE;
}
