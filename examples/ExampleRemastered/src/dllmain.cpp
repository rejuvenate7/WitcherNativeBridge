#include "NativeImports.h"

#include <windows.h>

namespace
{
	DWORD WINAPI Initialize(LPVOID)
	{
		if (!wnb_example::RegisterAll())
			OutputDebugStringA("WNBExample: one or more native registrations failed\n");

		return 0;
	}
} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(module);

		HANDLE thread = CreateThread(nullptr, 0, Initialize, nullptr, 0, nullptr);
		if (thread)
			CloseHandle(thread);
	}

	return TRUE;
}
