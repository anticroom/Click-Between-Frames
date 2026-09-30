#include <Geode/Geode.hpp>

HMODULE g_module = nullptr;

void modLoaded();

$on_mod(Loaded) {
	if (CreateMutexA(nullptr, TRUE, "ClickBetweenFrames2.1") && GetLastError() == ERROR_ALREADY_EXISTS) return;

	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&modLoaded), &g_module);
	modLoaded();
}
