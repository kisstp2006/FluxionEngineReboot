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

// Moves the next read or write to `offset` bytes from the start of the
// file. False on a negative offset, or when the underlying file cannot be
// moved through at all.
//
// Reading part of a file rather than the whole of it needs this: an
// archive holds many things in one file, and getting at the third of them
// otherwise means reading past the first two.
bool  Fluxion_Platform_FileSeek(FluxionFile* file, i64 offset);

bool  Fluxion_Platform_FileExists(const char* path);
bool  Fluxion_Platform_FileDelete(const char* path);

bool  Fluxion_Platform_DirectoryExists(const char* path);

// Creates ONE directory, whose parent must already be there. True when it
// is created and also when it was already there -- a caller that wanted a
// directory to exist has what it wanted either way, and making the
// difference visible only invites everyone to ignore it.
bool  Fluxion_Platform_DirectoryCreate(const char* path);

#ifdef __cplusplus
}
#endif
