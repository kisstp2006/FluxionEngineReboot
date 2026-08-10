#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// All three return false if the value doesn't fit in bufferSize (bytes,
// including the null terminator) or on failure; outBuffer is always left
// null-terminated on success.
bool Fluxion_Platform_GetEnvironmentVariable(const char* name, char* outBuffer, usize bufferSize);
bool Fluxion_Platform_GetExecutablePath(char* outBuffer, usize bufferSize);
bool Fluxion_Platform_GetCurrentWorkingDirectory(char* outBuffer, usize bufferSize);

#ifdef __cplusplus
}
#endif
