#include "NativeImports.h"

#include <WitcherNativeBridgeApi.h>
#include <windows.h>

#include <cstdint>
#include <string>
#include <climits>

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

			WNB_ReturnInt(frame, result, value);
		}

		// WitcherScript:
		// import function Example_Bool(value : bool) : bool;
		void BoolNative(void*, void* frame, void* result)
		{
			bool value = WNB_ReadBoolParameter(frame);

			WNB_ReturnBool(frame, result, value);
		}

		// WitcherScript:
		// import function Example_Float(value : float) : float;
		void FloatNative(void*, void* frame, void* result)
		{
			float value = WNB_ReadFloatParameter(frame);

			WNB_ReturnFloat(frame, result, value);
		}

		// helper function
		std::wstring ReadWideString(void* frame)
		{
			WNB_String input{};
			if (WNB_ReadStringParameter(frame, &input) <= 0 || !input.data || input.size <= 1 || input.size > INT_MAX)
				return {};

			const char* utf8 = reinterpret_cast<const char*>(input.data);
			const int bytes = static_cast<int>(input.size - 1); // Exclude trailing NUL

			const int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, bytes, nullptr, 0);
			if (chars <= 0)
				return {};

			std::wstring value(static_cast<size_t>(chars), L'\0');
			if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, bytes, value.data(), chars) != chars)
				return {};

			return value;
		}

		// WitcherScript:
		// import function Example_String(value : string) : string;
		void StringNative(void*, void* frame, void* result)
		{
			std::wstring value = ReadWideString(frame);

			WNB_ReturnString(frame, result, value.c_str(), static_cast<uint32_t>(value.size()));
		}

		// WitcherScript:
		// import function Example_Name(value : name) : name;
		void NameNative(void*, void* frame, void* result)
		{
			WNB_Name value = WNB_ReadNameParameter(frame);

			WNB_ReturnName(frame, result, value);
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
		    {"Example_Int", &IntNative}, {"Example_Bool", &BoolNative}, {"Example_Float", &FloatNative}, {"Example_String", &StringNative}, {"Example_Name", &NameNative},
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
