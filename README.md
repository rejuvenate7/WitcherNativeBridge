# WitcherNativeBridge

Shared native bridge API for **The Witcher 3**, allowing C++ ASI mods to register custom WitcherScript import functions through one API.

## Building

Requirements:

- Visual Studio 2022
- x64
- C++17

Open `WitcherNativeBridge.sln` and build `Release | x64`.

The build produces:

```text
WitcherNativeBridge.asi
WitcherNativeBridge.lib
```

`WitcherNativeBridge.asi` is the runtime bridge.

`WitcherNativeBridge.lib` and `include/WitcherNativeBridgeApi.h` are used by ASI mods that want to register their own WitcherScript imports.

## Using the API

Include the public header:

```cpp
#include <WitcherNativeBridgeApi.h>
```

Create a native function:

```cpp
void ExampleInt(void*, void* frame, void* result)
{
	int value = WNB_ReadIntParameter(frame);
	WNB_AdvanceFrame(frame);

	WNB_WriteIntResult(result, value);
}
```

Register it:

```cpp
WNB_RegisterNative("MyMod_ExampleInt", &ExampleInt);
```

Then import the same function name in WitcherScript:

```witcherscript
import function MyMod_ExampleInt(value : int) : int;
```

When reading multiple parameters, read them in the same order as the WitcherScript declaration and call `WNB_AdvanceFrame(frame)` once after all parameters have been read.

## Supported Types

WitcherNativeBridge currently supports reading and writing:

- `int`
- `bool`
- `float`
- `string`
- `name`

## Built-in Imports

The bridge also provides:

```witcherscript
import function WNB_GetApiVersion() : int;
import function WNB_StringToName(value : string) : name;
```
## Example Project

A ready-to-build example project is included here:

[`examples/WitcherNativeBridgeExample`](examples/WitcherNativeBridgeExample)

It already includes the required header, import library, linker settings, and simple examples for each supported type.

## Notes

- Native function names must be unique. It is recommended to prefix your import functions with your mod name like `WO_Send()`.
- Functions must be registered at game startup and registered callbacks should remain loaded for the lifetime of the game.
- `WitcherNativeBridge.asi` must be installed for dependent mods to work.

## Known mods that are using WitcherNativeBridge
- [rejuvenate7/WitcherOnline](https://www.nexusmods.com/witcher3/mods/11590)
