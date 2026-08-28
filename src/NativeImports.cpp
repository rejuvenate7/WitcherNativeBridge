#include "NativeImports.h"
#include "NativeBridge.h"

#include <string>

namespace witcher_native_bridge::imports
{
	namespace
	{
		struct NativeRegistration
		{
			const char* name;
			WNB_NativeImplementation implementation;
		};

		// WitcherScript declaration:
		// import function WNB_GetApiVersion() : int;
		void GetApiVersionNative(void*, void* frame, void* result)
		{
			AdvanceFrame(frame);
			WriteIntResult(result, static_cast<int>(WNB_GetApiVersion()));
		}

		// WitcherScript declaration:
		// import function WNB_StringToName(value : string) : name;
		void StringToNameNative(void*, void* frame, void* result)
		{
			ScriptString input{};
			const int readSize = ReadStringParameter(frame, input);
			AdvanceFrame(frame);

			if (!result)
				return;

			if (readSize <= 0)
			{
				WriteNameResult(result, L"");
				return;
			}

			const std::wstring value = ScriptStringToWide(input);
			WriteNameResult(result, value);
		}
	} // namespace

	bool RegisterAll()
	{
		const NativeRegistration registrations[] = {
		    {"WNB_GetApiVersion", &GetApiVersionNative},
		    {"WNB_StringToName", &StringToNameNative},
		};

		bool success = true;
		int registeredCount = 0;

		for (const NativeRegistration& registration : registrations)
		{
			if (WNB_RegisterNative(registration.name, registration.implementation))
			{
				++registeredCount;
				DebugLog(std::string("built-in import queued: ") + registration.name + "=OK");
				continue;
			}

			DebugLog(std::string("built-in import queued: ") + registration.name + "=FAILED");
			success = false;
		}

		DebugLog("built-in imports queued=" + std::to_string(registeredCount) + "/" + std::to_string(sizeof(registrations) / sizeof(registrations[0])));
		return success;
	}
} // namespace witcher_native_bridge::imports
