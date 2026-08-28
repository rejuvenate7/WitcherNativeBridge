# WitcherNativeBridge Example Mod

Minimal Visual Studio 2022 x64 starter project for adding native WitcherScript import functions through **WitcherNativeBridge**.

The project is already configured to:

- include `WitcherNativeBridge/include`
- link `WitcherNativeBridge/lib/WitcherNativeBridge.lib`
- build as an `.asi`
- target x64 only
- use C++17
- avoid precompiled headers
- register native callbacks through `WNB_RegisterNative`

## Repository layout

```text
WitcherNativeBridgeExample/
├── WitcherNativeBridgeExample.sln
├── WitcherNativeBridgeExample.vcxproj
├── src/
│   ├── dllmain.cpp
│   ├── NativeImports.cpp
│   └── NativeImports.h
├── witcherscript/
│   └── example_imports.ws
└── WitcherNativeBridge/
    ├── include/
    │   └── WitcherNativeBridgeApi.h
    └── lib/
        └── WitcherNativeBridge.lib
```

## Setup

1. Put the current public API header at:

   `WitcherNativeBridge/include/WitcherNativeBridgeApi.h`

2. Put the x64 import library at:

   `WitcherNativeBridge/lib/WitcherNativeBridge.lib`

3. Open `WitcherNativeBridgeExample.sln` in Visual Studio 2022.

4. Build `Debug | x64` or `Release | x64`.

The output is:

```text
build/Release/WitcherNativeBridgeExample.asi
```

For runtime use, install both your built ASI and `WitcherNativeBridge.asi` where your Witcher 3 ASI loader loads plugins.

## Included examples

| WitcherScript import | Demonstrates |
| --- | --- |
| `WNBExample_GetApiVersion()` | no parameters + int result |
| `WNBExample_AddInts(a, b)` | int parameters/result |
| `WNBExample_And(a, b)` | bool parameters/result |
| `WNBExample_MultiplyFloats(a, b)` | float parameters/result |
| `WNBExample_Echo(value)` | string parameter/result |
| `WNBExample_StringToName(value)` | string parameter + name result |

API v1 currently exposes a name **writer**, but not a direct name-parameter reader, so the name example accepts a string and returns a `name`.

## Adding your own import

Most changes happen in `src/NativeImports.cpp`.

A native callback always has this shape:

```cpp
void MyNative(void*, void* frame, void* result)
{
    const int value = WNB_ReadIntParameter(frame);

    // Call this exactly once after all parameters have been read.
    WNB_AdvanceFrame(frame);

    WNB_WriteIntResult(result, value * 2);
}
```

Add it to the registration table:

```cpp
{"MyMod_Double", &MyNative},
```

Then declare the matching WitcherScript import:

```witcherscript
import function MyMod_Double(value : int) : int;
```

The string passed to `WNB_RegisterNative` is the exact function name WitcherScript must import.

## Important rules

- Read parameters in the same order as the WitcherScript declaration.
- Call `WNB_AdvanceFrame(frame)` exactly once after reading all parameters, including functions with no parameters.
- Use the matching `WNB_Write*Result` helper for the return type.
- Keep callback functions available for the lifetime of the game. Do not hot-unload an ASI after registering native callbacks.
- Give your natives a unique prefix. Rename `WNBExample_` before publishing a real mod.
- Do not define `WITCHER_NATIVE_BRIDGE_EXPORTS` in consumer mods. That macro belongs only to WitcherNativeBridge itself.

## Linking is already configured

You should not need to edit Visual Studio linker settings. The project contains:

```text
Additional Include Directories:
$(ProjectDir)WitcherNativeBridge\include

Additional Library Directories:
$(ProjectDir)WitcherNativeBridge\lib

Additional Dependencies:
WitcherNativeBridge.lib
```

There is no delay-load configuration in this sample. WitcherNativeBridge is treated as a required runtime dependency.
