# WitcherNativeBridge Example

A minimal Visual Studio 2022 example for creating custom WitcherScript import functions with [WitcherNativeBridge](../../).

The project is already set up to include and link WitcherNativeBridge. Open the solution, add your functions in `src/NativeImports.cpp`.

## Using it

A native function can read WitcherScript parameters and return a value:

```cpp
void IntNative(void*, void* frame, void* result)
{
	int value = WNB_ReadIntParameter(frame);

	WNB_ReturnInt(frame, result, value);
}
```

`WNB_Return*` automatically finalizes the WitcherScript call before returning the result.

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

The included example contains simple read/return examples for:

```witcherscript
import function Example_Int(value : int) : int;
import function Example_Bool(value : bool) : bool;
import function Example_Float(value : float) : float;
import function Example_String(value : string) : string;
import function Example_Name(value : name) : name;
```

When adding multiple parameters, read them in the same order as the WitcherScript declaration, then finish the callback with the matching `WNB_Return*` function.

For a function with no return value, use:

```cpp
WNB_ReturnVoid(frame);
```

## Building

Build the solution in `Release | x64`.

Copy the built `.asi` to both:

```text
The Witcher 3/bin/x64
The Witcher 3/bin/x64_dx12
```

Both `WitcherNativeBridge.asi` and your own built `.asi` must be placed in the root of those directories, near `witcher3.exe`, and loaded using an [ASI loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases).
