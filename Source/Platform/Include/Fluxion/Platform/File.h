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

// When a file was last written, and how large it is, WITHOUT opening it.
//
// For noticing that a file changed. Opening it to find out costs a handle
// and, on a file something else is in the middle of writing, may fail for
// a reason that has nothing to do with the question being asked.
//
// The time is in whatever unit the platform keeps -- it is meant to be
// compared with an earlier answer for the same file and nothing else. The
// two are reported together because neither is enough on its own: a file
// system that keeps times to the second cannot tell two writes in the
// same second apart, and a file rewritten with different contents of the
// same length has the same size as before.
//
// False when the file is not there or cannot be asked about, and the two
// outputs are then left alone.
bool  Fluxion_Platform_FileStat(const char* path, u64* outModifiedTime, u64* outSize);

bool  Fluxion_Platform_DirectoryExists(const char* path);

// Creates ONE directory, whose parent must already be there. True when it
// is created and also when it was already there -- a caller that wanted a
// directory to exist has what it wanted either way, and making the
// difference visible only invites everyone to ignore it.
bool  Fluxion_Platform_DirectoryCreate(const char* path);

#ifdef __cplusplus
}
#endif
