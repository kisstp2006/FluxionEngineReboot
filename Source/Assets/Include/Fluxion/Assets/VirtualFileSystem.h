#pragma once

#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Where bytes come from, when the answer must not be "a path on a disk".
//
// The raw file interface takes real paths and opens real files, and says
// so in its own header. That is not enough everywhere: on some platforms
// the shipped data is not a file at all -- it lives inside the installed
// package, and only the system's own reader will hand it over. Code that
// waits for a path there does not get a wrong answer, it gets nothing.
//
// So a read goes to a scheme, and the scheme is backed by one or more
// sources. A source may be a directory, a package, or -- later, without
// any caller changing -- whatever the platform insists on. The editor and
// the built game then run THE SAME loading code, with a different thing
// underneath it, rather than two paths that can drift apart.

// "scheme://relative/path"
//
// Two schemes are expected to exist everywhere, though nothing here
// enforces the names:
//
//   assets://   what shipped. Read-only.
//   user://     saves, settings, logs. Writable.
#define FLUXION_VFS_MAX_PATH              512
#define FLUXION_VFS_MAX_SCHEME_LENGTH     15
#define FLUXION_VFS_MAX_MOUNTS            8

// More than one source may back a scheme -- a package plus a directory
// that overrides parts of it, say. The most recently mounted is searched
// first, so a later mount shadows an earlier one rather than being
// shadowed by it.
#define FLUXION_VFS_MAX_SOURCES_PER_MOUNT 4

typedef struct FluxionVfsSource FluxionVfsSource;

typedef struct FluxionVfsSourceVTable
{
    bool (*exists)(FluxionVfsSource* self, const char* path);

    // The whole file, or NULL when this source does not have it. The
    // bytes come from the file system's own allocator and are handed back
    // with Fluxion_Vfs_FreeBuffer -- a source never frees what it read,
    // so a caller never has to know which source answered.
    u8* (*readAll)(FluxionVfsSource* self, const char* path, usize* outSize);

    // NULL on a read-only source, which is what makes "assets:// cannot
    // be written" a fact about the source rather than a rule someone
    // remembered to check.
    bool (*writeAll)(FluxionVfsSource* self, const char* path, const void* data, usize size);

    void (*destroy)(FluxionVfsSource* self);
} FluxionVfsSourceVTable;

// Every concrete source embeds this as its first member, so the file
// system can hold them all by one pointer type.
struct FluxionVfsSource
{
    const FluxionVfsSourceVTable* vtable;
};

void Fluxion_Vfs_Init(FluxionAllocator* allocator);

// Destroys every mounted source. A source handed to Mount is owned from
// that moment on, including when Mount itself fails -- otherwise the
// caller of a failed mount would be left holding something it has no way
// to destroy.
void Fluxion_Vfs_Shutdown(void);
bool Fluxion_Vfs_IsInitialized(void);

// Mounting is a startup act, not a runtime one: it is NOT safe to mount
// while another thread is reading. Reads themselves are safe to do from
// several threads at once, which is what the loader relies on.
bool Fluxion_Vfs_Mount(const char* scheme, FluxionVfsSource* source);
bool Fluxion_Vfs_UnmountAll(const char* scheme);
u32  Fluxion_Vfs_GetSourceCount(const char* scheme);

bool Fluxion_Vfs_Exists(const char* path);

// Reads through the scheme's sources, newest mount first, and stops at
// the first one that has the file. NULL when no source does, or when the
// path is not one this will accept (see SplitPath).
u8*  Fluxion_Vfs_ReadAll(const char* path, usize* outSize);

// Writes through the first source of the scheme that can be written at
// all. False on a scheme whose sources are all read-only -- which is the
// intended answer for assets://, not a failure to be worked around.
bool Fluxion_Vfs_WriteAll(const char* path, const void* data, usize size);

// `size` must be the size that came back with the buffer.
void Fluxion_Vfs_FreeBuffer(u8* buffer, usize size);

// Splits "scheme://relative" and rejects anything that could leave the
// mount it names.
//
// Refused, deliberately: a ".." segment, a leading separator, a backslash,
// a further colon (a drive letter), an empty or "." segment. Without this
// an asset path is a way out of the mount, and the whole point of naming
// data by scheme is that it is not.
bool Fluxion_Vfs_SplitPath(const char* path,
                           char* outScheme, usize schemeCapacity,
                           char* outRelative, usize relativeCapacity);

// The allocator every read is made from. Sources need it; so does anyone
// building one.
FluxionAllocator* Fluxion_Vfs_GetAllocator(void);

// A plain directory. `rootPath` is a real path in the host's own form,
// and every read below it is joined onto that root -- the only place in
// the engine that is allowed to assume a path opens a file.
//
// Writable: this is what backs user://, and what backs assets:// while a
// project is being worked on rather than shipped.
FluxionVfsSource* Fluxion_VfsDirectorySource_Create(const char* rootPath);

#ifdef __cplusplus
}
#endif
