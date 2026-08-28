# WitcherNativeBridge Example

A minimal Visual Studio 2022 example for creating custom WitcherScript import functions with [WitcherNativeBridge](../../).

The project is already set up to include and link WitcherNativeBridge. Open the solution, build x64, and add your functions in `src/NativeImports.cpp`.

## Using it

A native function can read WitcherScript parameters and return a value:

```cpp
void IntNative(void*, void* frame, void* result)
{
	int value = WNB_ReadIntParameter(frame);
	WNB_AdvanceFrame(frame);

	WNB_WriteIntResult(result, value);
}
```

Register the function:

```cpp
const NativeRegistration registrations[] = {
	{"Example_Int", &IntNative},
};
```

Then import the same name in WitcherScript:

```witcherscript
import function Example_Int(value : int) : int;
```

The included example contains simple read/write examples for:

```witcherscript
import function Example_Int(value : int) : int;
import function Example_Bool(value : bool) : bool;
import function Example_Float(value : float) : float;
import function Example_String(value : string) : string;
import function Example_Name(value : name) : name;
```

When adding parameters, read them in the same order as the WitcherScript declaration and call `WNB_AdvanceFrame(frame)` once after all parameters have been read.

## Building
Build the solution in Release mode and include the .asi in The Witcher 3's `bin/x64` and `bin/x64_dx12` folders.

Both `WitcherNativeBridge.asi` (API) and your own built .asi must be in the root directories (near witcher3.exe), and loaded using an [.asi loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases).
