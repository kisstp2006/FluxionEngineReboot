#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

bool Fluxion_Clipboard_SetText(const char* text);

// Returns false if the clipboard is empty/unavailable, or the text
// doesn't fit in bufferSize.
bool Fluxion_Clipboard_GetText(char* outBuffer, usize bufferSize);

#ifdef __cplusplus
}
#endif
