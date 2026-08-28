#include "NativeImports.h"

#include <WitcherNativeBridgeApi.h>
#include <windows.h>

#include <cstdint>
#include <string>

namespace wnb_example
{
	namespace
	{
		void Log(const std::string& text)
		{
			OutputDebugStringA(("WNBExample: " + text + "\n").c_str());
		}

		// WitcherScript:
		// import function Example_Int(value : int) : int;
		void IntNative(void*, void* frame, void* result)
		{
			int value = WNB_ReadIntParameter(frame);
			WNB_AdvanceFrame(frame);

			WNB_WriteIntResult(result, value);
		}

		// WitcherScript:
		// import function Example_Bool(value : bool) : bool;
		void BoolNative(void*, void* frame, void* result)
		{
			bool value = WNB_ReadBoolParameter(frame);
			WNB_AdvanceFrame(frame);

			WNB_WriteBoolResult(result, value);
		}

		// WitcherScript:
		// import function Example_Float(value : float) : float;
		void FloatNative(void*, void* frame, void* result)
		{
			float value = WNB_ReadFloatParameter(frame);
			WNB_AdvanceFrame(frame);

			WNB_WriteFloatResult(result, value);
		}

		// WitcherScript:
		// import function Example_String(value : string) : string;
		void StringNative(void*, void* frame, void* result)
		{
			WNB_String input{};
			WNB_ReadStringParameter(frame, &input);
			WNB_AdvanceFrame(frame);

			std::wstring value;

			if (input.data && input.size > 0)
				value.assign(input.data, input.data + input.size - 1);

			WNB_WriteStringResult(result, value.c_str(), static_cast<uint32_t>(value.size()));
		}

		// WitcherScript:
		// import function Example_Name(value : name) : name;
		void NameNative(void*, void* frame, void* result)
		{
			WNB_Name value = WNB_ReadNameParameter(frame);
			WNB_AdvanceFrame(frame);

			WNB_WriteNameIndexResult(result, value);
		}

		struct NativeRegistration
		{
			const char* name;
			WNB_NativeImplementation implementation;
		};
	} // namespace

	bool RegisterAll()
	{
		const uint32_t apiVersion = WNB_GetApiVersion();
		if (apiVersion < WNB_API_VERSION)
		{
			Log("WitcherNativeBridge API is too old. Found " + std::to_string(apiVersion) + ", need " + std::to_string(WNB_API_VERSION));
			return false;
		}

		// create a list of all our natives here to register
		const NativeRegistration registrations[] = {
			{"Example_Int", &IntNative},
			{"Example_Bool", &BoolNative},
			{"Example_Float", &FloatNative},
			{"Example_String", &StringNative},
			{"Example_Name", &NameNative},
		};

		bool success = true;
		int registered = 0;

		for (const NativeRegistration& registration : registrations)
		{
			if (WNB_RegisterNative(registration.name, registration.implementation))
			{
				++registered;
				continue;
			}

			Log(std::string("failed to queue native: ") + registration.name);
			success = false;
		}

		Log("queued " + std::to_string(registered) + "/" + std::to_string(sizeof(registrations) / sizeof(registrations[0])) + " example natives");

		return success;
	}
} // namespace wnb_example
