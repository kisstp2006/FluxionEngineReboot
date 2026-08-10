#include <Fluxion/Foundation/Defines.h>

// Minimal loadable library used only by Test_DynamicLibrary.c to prove
// LoadDynamicLibrary/GetSymbol/UnloadDynamicLibrary work end-to-end.
FLUXION_EXPORT int TestPlugin_GetMagicNumber(void)
{
    return 424242;
}
