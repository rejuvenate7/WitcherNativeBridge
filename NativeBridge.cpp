#include "NativeBridge.h"

#include "GameModule.h"
#include "InlineHook.h"
#include "SignatureScanner.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace witcher_native_bridge
{
	namespace
	{
		struct ScriptApi
		{
			void* alloc = nullptr;
			void* memsetFn = nullptr;
			void* namePool = nullptr;
			void* addName = nullptr;
			void* functionCtor = nullptr;
			void* scriptSystem = nullptr;
			void* registerGlobal = nullptr;

			void* opcodeTable = nullptr;
			void* bufferAlloc = nullptr;
			void* bufferCopy = nullptr;
			const wchar_t** emptyString = nullptr;
			const int* emptyStringLength = nullptr;

			size_t matches = 0;
			size_t consistent = 0;

			bool IsComplete() const
			{
				return alloc && memsetFn && namePool && addName && functionCtor && scriptSystem && registerGlobal;
			}

			bool CanMarshalStrings() const
			{
				return opcodeTable && bufferAlloc && bufferCopy && emptyString && emptyStringLength;
			}
		};

		using AllocFn = void* (*)(size_t size, size_t alignment);
		using MemsetFn = void* (*)(void* destination, int value, size_t size);
		using NamePoolFn = void* (*)();
		using AddNameFn = int (*)(void* pool, const wchar_t* name);
		using FunctionCtorFn = void* (*)(void* self, int* nameIndex, void* implementation);
		using ScriptSystemFn = void* (*)();
		using RegisterGlobalFn = void (*)(void* system, void* function);
		using OpcodeHandlerFn = void (*)(void* context, void* frame, void* destination);
		using BufferAllocFn = void* (*)(size_t zero, size_t bytes, size_t kind, size_t tag);
		using BufferCopyFn = void* (*)(void* destination, const void* source, size_t bytes);

		constexpr const char* kRegistrationSignature = "BA 10 00 00 00 B9 C0 00 00 00 E8 ?? ?? ?? ?? 48 8B F8 48 85 C0 74 ?? "
		                                               "33 D2 41 B8 C0 00 00 00 48 8B C8 E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? "
		                                               "48 8D 15 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 4C 8D 05 ?? ?? ?? ?? "
		                                               "89 45 10 48 8D 55 10 48 8B CF E8 ?? ?? ?? ?? "
		                                               "48 8B F8 EB ?? 48 8B FB E8 ?? ?? ?? ?? 48 8B C8 48 8B D7 E8 ?? ?? ?? ??";

		constexpr int kOffsetName = 44;
		constexpr int kOffsetImplementation = 59;
		constexpr int kOffsetAlloc = 10;
		constexpr int kOffsetMemset = 34;
		constexpr int kOffsetNamePool = 39;
		constexpr int kOffsetAddName = 54;
		constexpr int kOffsetFunctionCtor = 76;
		constexpr int kOffsetScriptSystem = 89;
		constexpr int kOffsetRegisterGlobal = 100;
		constexpr size_t kMinimumMatches = 32;

		constexpr size_t kFunctionObjectSize = 0xC0;
		constexpr size_t kFunctionObjectAlignment = 0x10;

		ScriptApi g_api{};
		bool g_bindingAttempted = false;
		bool g_bindingResolved = false;
		std::map<std::string, void*> g_existingNatives;

		struct PendingNativeRegistration
		{
			std::string name;
			NativeImplementation implementation = nullptr;
		};

		InlineHook g_registerHook;
		std::atomic<bool> g_registrationPhaseStarted{false};
		std::atomic<DWORD> g_registrationThreadId{0};
		std::mutex g_registrationMutex;
		std::vector<PendingNativeRegistration> g_pendingRegistrations;
		std::set<std::string> g_claimedNativeNames;
		std::string g_registrationError;

		std::mutex g_logMutex;
		std::ofstream g_log;

		void* ResolveCall(uint8_t* site, int offset)
		{
			return SignatureScanner::ResolveRelative(site + offset, 1, 5);
		}

		void* ResolveLea(uint8_t* site, int offset)
		{
			return SignatureScanner::ResolveRelative(site + offset, 3, 7);
		}

		std::string NarrowAsciiName(const wchar_t* value, size_t limit = 96)
		{
			std::string out;
			if (!value)
				return out;

			for (size_t i = 0; i < limit && value[i] != L'\0'; ++i)
			{
				const wchar_t ch = value[i];
				if (ch < 32 || ch > 126)
					return {};
				out.push_back(static_cast<char>(ch));
			}
			return out;
		}

		bool IsInsideGameImage(void* address)
		{
			const ModuleRegion& image = GameModule::Image();
			if (!address || !image.IsValid())
				return false;

			auto* p = static_cast<uint8_t*>(address);
			return p >= image.base && p < (image.base + image.size);
		}

		bool LooksLikeOpcodeTable(void* address, size_t requiredEntries = 32)
		{
			if (!IsInsideGameImage(address))
				return false;

			const ModuleRegion& text = GameModule::Text();
			auto** entries = static_cast<uint8_t**>(address);
			size_t valid = 0;

			for (size_t i = 0; i < requiredEntries; ++i)
			{
				uint8_t* entry = entries[i];
				if (!entry)
					continue;
				if (entry < text.base || entry >= text.base + text.size)
					return false;
				++valid;
			}

			return valid * 2 >= requiredEntries;
		}

		void* FindExistingNative(const std::string& name)
		{
			const auto it = g_existingNatives.find(name);
			return it == g_existingNatives.end() ? nullptr : it->second;
		}

		void ResolveStringMarshalling(ScriptApi& api)
		{
			void* logChannel = FindExistingNative("LogChannel");
			if (!logChannel)
				return;

			auto* code = static_cast<uint8_t*>(logChannel);

			for (int i = 0; i < 0x40; ++i)
			{
				if (code[i] != 0x48 && code[i] != 0x4C)
					continue;
				if (code[i + 1] != 0x8D)
					continue;
				if ((code[i + 2] & 0xC7) != 0x05)
					continue;

				void* candidate = SignatureScanner::ResolveRelative(code + i, 3, 7);
				if (IsInsideGameImage(candidate))
				{
					api.opcodeTable = candidate;
					break;
				}
			}

			const SignaturePattern allocatorPattern = SignaturePattern::Parse("44 8D 49 0E 44 8D 41 02 E8 ?? ?? ?? ??");

			for (int i = 0; i < 0xC0; ++i)
			{
				if (allocatorPattern.MatchesAt(code + i))
				{
					api.bufferAlloc = SignatureScanner::ResolveRelative(code + i + 8, 1, 5);
					break;
				}
			}

			for (int i = 0; i < 0xC0; ++i)
			{
				if (code[i] == 0x48 && code[i + 1] == 0x8B && code[i + 2] == 0x15)
				{
					api.emptyString = reinterpret_cast<const wchar_t**>(SignatureScanner::ResolveRelative(code + i, 3, 7));
					break;
				}
			}

			for (int i = 0; i < 0x40; ++i)
			{
				if (code[i] == 0x8B && code[i + 1] == 0x05)
				{
					api.emptyStringLength = reinterpret_cast<const int*>(SignatureScanner::ResolveRelative(code + i, 2, 6));
					break;
				}
			}

			if (api.bufferAlloc)
			{
				for (int i = 0; i < 0xC0; ++i)
				{
					if (code[i] != 0xE8)
						continue;

					void* target = SignatureScanner::ResolveRelative(code + i, 1, 5);
					if (target != api.bufferAlloc)
						continue;

					for (int j = i + 5; j < i + 0x30; ++j)
					{
						if (code[j] == 0xE8)
						{
							api.bufferCopy = SignatureScanner::ResolveRelative(code + j, 1, 5);
							break;
						}
					}
					break;
				}
			}
		}

		void* ConsensusAddress(const std::vector<void*>& values, size_t& agreeing)
		{
			std::map<void*, size_t> counts;
			for (void* value : values)
			{
				if (value)
					++counts[value];
			}

			void* winner = nullptr;
			agreeing = 0;
			for (const auto& [address, count] : counts)
			{
				if (count > agreeing)
				{
					winner = address;
					agreeing = count;
				}
			}
			return winner;
		}

		void DispatchParameter(void* frame, void* destination)
		{
			auto** instruction = reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(frame) + 0x30);
			if (!instruction || !*instruction)
				return;

			const uint8_t opcode = **instruction;
			*instruction += 1;

			void* context = *reinterpret_cast<void**>(frame);
			if (!LooksLikeOpcodeTable(g_api.opcodeTable))
				return;

			auto* table = static_cast<OpcodeHandlerFn*>(g_api.opcodeTable);
			OpcodeHandlerFn handler = table[opcode];
			if (handler)
				handler(context, frame, destination);
		}

		bool MakeEmptyScriptString(ScriptString& value)
		{
			if (!g_api.CanMarshalStrings())
				return false;

			const int length = *g_api.emptyStringLength;
			const wchar_t* source = *g_api.emptyString;
			if (length < 0 || length > 64)
				return false;

			value = {};
			value.size = static_cast<uint32_t>(length);
			if (length == 0)
				return true;
			if (!source)
				return false;

			auto allocate = reinterpret_cast<BufferAllocFn>(g_api.bufferAlloc);
			auto copy = reinterpret_cast<BufferCopyFn>(g_api.bufferCopy);

			void* buffer = allocate(0, static_cast<size_t>(length) * sizeof(wchar_t), 2, 14);
			if (!buffer)
				return false;

			copy(buffer, source, static_cast<size_t>(length) * sizeof(wchar_t));
			value.data = static_cast<wchar_t*>(buffer);
			return true;
		}

		uint32_t LogicalStringLength(const ScriptString& value)
		{
			return (value.data && value.size > 0) ? value.size - 1 : 0;
		}

		int InternNameUnguarded(const wchar_t* value)
		{
			auto getPool = reinterpret_cast<NamePoolFn>(g_api.namePool);
			auto addName = reinterpret_cast<AddNameFn>(g_api.addName);

			void* pool = getPool();
			if (!pool)
				return 0;

			return addName(pool, value);
		}

		int InternNameGuarded(const wchar_t* value)
		{
			__try
			{
				return InternNameUnguarded(value);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return 0;
			}
		}

		bool WriteScriptStringUnguarded(void* result, const wchar_t* value, size_t length)
		{
			if (!result || !value || !g_api.CanMarshalStrings())
				return false;

			auto allocate = reinterpret_cast<BufferAllocFn>(g_api.bufferAlloc);
			auto copy = reinterpret_cast<BufferCopyFn>(g_api.bufferCopy);

			const size_t characters = length + 1;
			void* buffer = allocate(0, characters * sizeof(wchar_t), 2, 14);
			if (!buffer)
				return false;

			if (length > 0)
				copy(buffer, value, length * sizeof(wchar_t));
			static_cast<wchar_t*>(buffer)[length] = L'\0';

			auto* destination = static_cast<ScriptString*>(result);
			destination->data = static_cast<wchar_t*>(buffer);
			destination->size = static_cast<uint32_t>(characters);
			destination->padding = 0;
			return true;
		}

		bool WriteScriptStringGuarded(void* result, const wchar_t* value, size_t length)
		{
			__try
			{
				return WriteScriptStringUnguarded(result, value, length);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		void* RegisterNativeUnguarded(const wchar_t* name, NativeImplementation implementation)
		{
			auto alloc = reinterpret_cast<AllocFn>(g_api.alloc);
			auto zero = reinterpret_cast<MemsetFn>(g_api.memsetFn);
			auto getPool = reinterpret_cast<NamePoolFn>(g_api.namePool);
			auto addName = reinterpret_cast<AddNameFn>(g_api.addName);
			auto construct = reinterpret_cast<FunctionCtorFn>(g_api.functionCtor);
			auto getSystem = reinterpret_cast<ScriptSystemFn>(g_api.scriptSystem);
			auto registerGlobal = reinterpret_cast<RegisterGlobalFn>(g_registerHook.Trampoline());

			void* storage = alloc(kFunctionObjectSize, kFunctionObjectAlignment);
			if (!storage)
				return nullptr;

			zero(storage, 0, kFunctionObjectSize);

			void* pool = getPool();
			if (!pool)
				return nullptr;

			int nameIndex = addName(pool, name);
			void* function = construct(storage, &nameIndex, reinterpret_cast<void*>(implementation));
			if (!function)
				return nullptr;

			void* system = getSystem();
			if (!system)
				return nullptr;

			registerGlobal(system, function);
			return function;
		}

		bool IsValidPublicNativeName(const char* name)
		{
			if (!name || name[0] == '\0')
				return false;

			size_t length = 0;
			for (; name[length] != '\0'; ++length)
			{
				const unsigned char ch = static_cast<unsigned char>(name[length]);
				if (ch < 32 || ch > 126 || length >= 127)
					return false;
			}

			return length > 0;
		}

		std::wstring WidenAsciiName(const std::string& name)
		{
			return std::wstring(name.begin(), name.end());
		}

		bool ConflictsWithExistingGameNative(const std::string& name)
		{
			return g_existingNatives.find(name) != g_existingNatives.end();
		}

		bool RegisterNativeImmediateGuarded(const wchar_t* name, NativeImplementation implementation)
		{
			__try
			{
				return RegisterNativeUnguarded(name, implementation) != nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool RegisterNativeImmediate(const PendingNativeRegistration& registration)
		{
			if (!g_bindingResolved || !g_registerHook.IsInstalled())
				return false;

			if (ConflictsWithExistingGameNative(registration.name))
			{
				DebugLog("native name conflicts with existing REDengine function: " + registration.name);
				return false;
			}

			const std::wstring wideName = WidenAsciiName(registration.name);
			return RegisterNativeImmediateGuarded(wideName.c_str(), registration.implementation);
		}

		void FlushPendingRegistrations()
		{
			std::vector<PendingNativeRegistration> pending;
			{
				std::lock_guard<std::mutex> lock(g_registrationMutex);
				if (g_pendingRegistrations.empty())
					return;
				pending.swap(g_pendingRegistrations);
			}

			for (const PendingNativeRegistration& registration : pending)
			{
				const bool registered = RegisterNativeImmediate(registration);
				const std::string& name = registration.name;
				DebugLog("native registration: " + name + "=" + (registered ? "OK" : "FAILED"));

				if (!registered)
				{
					if (!g_registrationError.empty())
						g_registrationError += ", ";
					g_registrationError += name;
				}
			}
		}

		void RegisterGlobalDetour(void* system, void* function)
		{
			auto original = reinterpret_cast<RegisterGlobalFn>(g_registerHook.Trampoline());
			original(system, function);

			g_registrationThreadId.store(GetCurrentThreadId(), std::memory_order_release);
			g_registrationPhaseStarted.store(true, std::memory_order_release);
			FlushPendingRegistrations();
		}
	} // namespace

	void InitLog(const std::string& path)
	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		g_log.open(path, std::ios::out | std::ios::trunc);
	}

	void ShutdownLog()
	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		if (g_log.is_open())
		{
			g_log.flush();
			g_log.close();
		}
	}

	void DebugLog(const std::string& text)
	{
		SYSTEMTIME now{};
		GetLocalTime(&now);

		char timestamp[32]{};
		sprintf_s(timestamp, "%02u:%02u:%02u.%03u", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

		const std::string line = std::string(timestamp) + " WitcherNativeBridge: " + text;
		OutputDebugStringA((line + "\n").c_str());

		std::lock_guard<std::mutex> lock(g_logMutex);
		if (g_log.is_open())
		{
			g_log << line << '\n';
			g_log.flush();
		}
	}

	bool ResolveScriptApi()
	{
		if (g_bindingAttempted)
			return g_bindingResolved;

		g_bindingAttempted = true;

		if (!GameModule::Resolve() || GameModule::IsSelfHosted())
			return false;

		const SignaturePattern pattern = SignaturePattern::Parse(kRegistrationSignature);
		if (!pattern.IsValid())
			return false;

		std::vector<uint8_t*> sites = SignatureScanner::FindAll(GameModule::Text(), pattern);
		g_api.matches = sites.size();
		if (sites.size() < kMinimumMatches)
			return false;

		std::vector<void*> allocs;
		std::vector<void*> memsets;
		std::vector<void*> pools;
		std::vector<void*> addNames;
		std::vector<void*> constructors;
		std::vector<void*> systems;
		std::vector<void*> registrars;

		allocs.reserve(sites.size());
		memsets.reserve(sites.size());
		pools.reserve(sites.size());
		addNames.reserve(sites.size());
		constructors.reserve(sites.size());
		systems.reserve(sites.size());
		registrars.reserve(sites.size());

		g_existingNatives.clear();

		for (uint8_t* site : sites)
		{
			const auto* name = static_cast<const wchar_t*>(ResolveLea(site, kOffsetName));
			void* implementation = ResolveLea(site, kOffsetImplementation);

			if (name && implementation)
			{
				const std::string decoded = NarrowAsciiName(name);
				if (!decoded.empty())
					g_existingNatives[decoded] = implementation;
			}

			allocs.push_back(ResolveCall(site, kOffsetAlloc));
			memsets.push_back(ResolveCall(site, kOffsetMemset));
			pools.push_back(ResolveCall(site, kOffsetNamePool));
			addNames.push_back(ResolveCall(site, kOffsetAddName));
			constructors.push_back(ResolveCall(site, kOffsetFunctionCtor));
			systems.push_back(ResolveCall(site, kOffsetScriptSystem));
			registrars.push_back(ResolveCall(site, kOffsetRegisterGlobal));
		}

		size_t agreeing = 0;
		size_t worstAgreement = sites.size();

		g_api.alloc = ConsensusAddress(allocs, agreeing);
		worstAgreement = (std::min)(worstAgreement, agreeing);

		g_api.memsetFn = ConsensusAddress(memsets, agreeing);
		worstAgreement = (std::min)(worstAgreement, agreeing);

		g_api.namePool = ConsensusAddress(pools, agreeing);
		worstAgreement = (std::min)(worstAgreement, agreeing);

		g_api.addName = ConsensusAddress(addNames, agreeing);
		worstAgreement = (std::min)(worstAgreement, agreeing);

		g_api.functionCtor = ConsensusAddress(constructors, agreeing);
		worstAgreement = (std::min)(worstAgreement, agreeing);

		g_api.scriptSystem = ConsensusAddress(systems, agreeing);
		worstAgreement = (std::min)(worstAgreement, agreeing);

		g_api.registerGlobal = ConsensusAddress(registrars, agreeing);
		worstAgreement = (std::min)(worstAgreement, agreeing);

		g_api.consistent = worstAgreement;
		g_bindingResolved = g_api.IsComplete() && worstAgreement == sites.size();

		if (g_bindingResolved)
			ResolveStringMarshalling(g_api);

		DebugLog("registration sites=" + std::to_string(g_api.matches) + " consensus=" + std::to_string(g_api.consistent));

		return g_bindingResolved;
	}

	bool CanMarshalStrings()
	{
		return g_api.CanMarshalStrings();
	}

	bool InstallRegistrationHook()
	{
		if (!g_bindingResolved)
			return false;
		if (g_registerHook.IsInstalled())
			return true;

		const std::vector<uint8_t> expectedPrologue = {0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20};

		if (!g_registerHook.Install(g_api.registerGlobal, reinterpret_cast<void*>(&RegisterGlobalDetour), expectedPrologue))
		{
			g_registrationError = g_registerHook.Error();
			return false;
		}

		return true;
	}

	void RemoveRegistrationHook()
	{
		g_registerHook.Remove();
	}

	const std::string& RegistrationError()
	{
		return g_registrationError;
	}

	bool QueueNativeRegistration(const char* name, NativeImplementation implementation)
	{
		if (!IsValidPublicNativeName(name) || !implementation)
			return false;

		const std::string ownedName(name);

		{
			std::lock_guard<std::mutex> lock(g_registrationMutex);
			if (!g_claimedNativeNames.insert(ownedName).second)
			{
				DebugLog("duplicate native rejected: " + ownedName);
				return false;
			}

			g_pendingRegistrations.push_back({ownedName, implementation});
		}

		DebugLog("queued native: " + ownedName);

		if (g_registrationPhaseStarted.load(std::memory_order_acquire) && g_registrationThreadId.load(std::memory_order_acquire) == GetCurrentThreadId())
			FlushPendingRegistrations();

		return true;
	}

	void AdvanceFrame(void* frame)
	{
		if (!frame)
			return;

		auto** instruction = reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(frame) + 0x30);
		if (instruction && *instruction)
			*instruction += 1;
	}

	int ReadIntParameter(void* frame)
	{
		int value = 0;
		__try
		{
			DispatchParameter(frame, &value);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			value = 0;
		}
		return value;
	}

	bool ReadBoolParameter(void* frame)
	{
		bool value = false;
		__try
		{
			DispatchParameter(frame, &value);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			value = false;
		}
		return value;
	}

	float ReadFloatParameter(void* frame)
	{
		float value = 0.0f;
		__try
		{
			DispatchParameter(frame, &value);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			value = 0.0f;
		}
		return value;
	}

	WNB_Name ReadNameParameter(void* frame)
	{
		WNB_Name value = 0;
		__try
		{
			DispatchParameter(frame, &value);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			value = 0;
		}
		return value;
	}

	int ReadStringParameter(void* frame, ScriptString& text)
	{
		__try
		{
			if (!MakeEmptyScriptString(text))
				return -2;

			DispatchParameter(frame, &text);
			return static_cast<int>(text.size);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return -3;
		}
	}

	std::wstring ScriptStringToWide(const ScriptString& text)
	{
		if (!text.data)
			return {};

		const uint32_t length = LogicalStringLength(text);
		return std::wstring(text.data, text.data + length);
	}

	std::string ScriptStringToUtf8Lossy(const ScriptString& text)
	{
		std::string out;
		if (!text.data)
			return out;

		const uint32_t length = LogicalStringLength(text);
		out.reserve(length);
		for (uint32_t i = 0; i < length; ++i)
		{
			const wchar_t ch = text.data[i];
			out.push_back(ch < 256 ? static_cast<char>(ch) : '?');
		}
		return out;
	}

	void WriteIntResult(void* result, int value)
	{
		if (result)
			*static_cast<int*>(result) = value;
	}

	void WriteBoolResult(void* result, bool value)
	{
		if (result)
			*static_cast<bool*>(result) = value;
	}

	void WriteFloatResult(void* result, float value)
	{
		if (result)
			*static_cast<float*>(result) = value;
	}

	bool WriteStringResult(void* result, const wchar_t* value, size_t length)
	{
		return WriteScriptStringGuarded(result, value, length);
	}

	bool WriteStringResult(void* result, const std::wstring& value)
	{
		return WriteStringResult(result, value.c_str(), value.size());
	}

	void WriteNameIndexResult(void* result, WNB_Name value)
	{
		if (result)
			*static_cast<WNB_Name*>(result) = value;
	}

	void WriteNameResult(void* result, const wchar_t* value)
	{
		if (!result)
			return;

		if (!value || value[0] == L'\0' || wcscmp(value, L"None") == 0)
		{
			*static_cast<WNB_Name*>(result) = 0;
			return;
		}

		*static_cast<WNB_Name*>(result) = static_cast<WNB_Name>(InternNameGuarded(value));
	}

	void WriteNameResult(void* result, const std::wstring& value)
	{
		WriteNameResult(result, value.c_str());
	}
} // namespace witcher_native_bridge

extern "C" uint32_t WNB_GetApiVersion()
{
	return WNB_API_VERSION;
}

extern "C" bool WNB_RegisterNative(const char* name, WNB_NativeImplementation implementation)
{
	return witcher_native_bridge::QueueNativeRegistration(name, implementation);
}

extern "C" void WNB_AdvanceFrame(void* frame)
{
	witcher_native_bridge::AdvanceFrame(frame);
}

extern "C" int WNB_ReadIntParameter(void* frame)
{
	return witcher_native_bridge::ReadIntParameter(frame);
}

extern "C" bool WNB_ReadBoolParameter(void* frame)
{
	return witcher_native_bridge::ReadBoolParameter(frame);
}

extern "C" float WNB_ReadFloatParameter(void* frame)
{
	return witcher_native_bridge::ReadFloatParameter(frame);
}

extern "C" WNB_Name WNB_ReadNameParameter(void* frame)
{
	return witcher_native_bridge::ReadNameParameter(frame);
}

extern "C" int WNB_ReadStringParameter(void* frame, WNB_String* text)
{
	if (!text)
		return -1;

	return witcher_native_bridge::ReadStringParameter(frame, *text);
}

extern "C" void WNB_WriteIntResult(void* result, int value)
{
	witcher_native_bridge::WriteIntResult(result, value);
}

extern "C" void WNB_WriteBoolResult(void* result, bool value)
{
	witcher_native_bridge::WriteBoolResult(result, value);
}

extern "C" void WNB_WriteFloatResult(void* result, float value)
{
	witcher_native_bridge::WriteFloatResult(result, value);
}

extern "C" bool WNB_WriteStringResult(void* result, const wchar_t* value, uint32_t length)
{
	if (!value)
		return false;

	return witcher_native_bridge::WriteStringResult(result, value, static_cast<size_t>(length));
}

extern "C" void WNB_WriteNameIndexResult(void* result, WNB_Name value)
{
	witcher_native_bridge::WriteNameIndexResult(result, value);
}

extern "C" void WNB_WriteNameResult(void* result, const wchar_t* value)
{
	witcher_native_bridge::WriteNameResult(result, value);
}
