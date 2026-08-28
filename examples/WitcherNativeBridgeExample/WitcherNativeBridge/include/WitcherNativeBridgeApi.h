#pragma once

#include <cstdint>

#ifdef WITCHER_NATIVE_BRIDGE_EXPORTS
#define WNB_API __declspec(dllexport)
#else
#define WNB_API __declspec(dllimport)
#endif

using WNB_NativeImplementation = void (*)(void* context, void* frame, void* result);
using WNB_Name = int32_t;

struct WNB_String
{
	wchar_t* data;
	uint32_t size;
	uint32_t padding;
};

inline constexpr uint32_t WNB_API_VERSION = 1;

extern "C"
{
	WNB_API uint32_t WNB_GetApiVersion();
	WNB_API bool WNB_RegisterNative(const char* name, WNB_NativeImplementation implementation);

	WNB_API void WNB_AdvanceFrame(void* frame);
	WNB_API int WNB_ReadIntParameter(void* frame);
	WNB_API bool WNB_ReadBoolParameter(void* frame);
	WNB_API float WNB_ReadFloatParameter(void* frame);
	WNB_API WNB_Name WNB_ReadNameParameter(void* frame);
	WNB_API int WNB_ReadStringParameter(void* frame, WNB_String* text);

	WNB_API void WNB_WriteIntResult(void* result, int value);
	WNB_API void WNB_WriteBoolResult(void* result, bool value);
	WNB_API void WNB_WriteFloatResult(void* result, float value);
	WNB_API bool WNB_WriteStringResult(void* result, const wchar_t* value, uint32_t length);
	WNB_API void WNB_WriteNameIndexResult(void* result, WNB_Name value);
	WNB_API void WNB_WriteNameResult(void* result, const wchar_t* value);
}
