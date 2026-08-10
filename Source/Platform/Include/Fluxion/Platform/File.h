#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Raw OS-handle-based file I/O. This is NOT a virtual file system — no
// mounting, no archives, no search paths, just direct paths on disk.
typedef struct FluxionFile
{
    void* handle;
} FluxionFile;

typedef enum FluxionFileOpenMode
{
    FLUXION_FILE_OPEN_READ,    // read-only
    FLUXION_FILE_OPEN_WRITE,   // create/truncate
    FLUXION_FILE_OPEN_APPEND, // create/append at the end
} FluxionFileOpenMode;

bool  Fluxion_Platform_FileOpen(FluxionFile* file, const char* path, FluxionFileOpenMode mode);
void  Fluxion_Platform_FileClose(FluxionFile* file);
usize Fluxion_Platform_FileRead(FluxionFile* file, void* buffer, usize size);
usize Fluxion_Platform_FileWrite(FluxionFile* file, const void* buffer, usize size);
i64   Fluxion_Platform_FileSize(FluxionFile* file);

bool  Fluxion_Platform_FileExists(const char* path);
bool  Fluxion_Platform_FileDelete(const char* path);

#ifdef __cplusplus
}
#endif
