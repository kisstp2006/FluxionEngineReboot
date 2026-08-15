#include <Fluxion/Assets/VirtualFileSystem.h>

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Platform/File.h>

#include <string.h>

#define FLUXION_VFS_LOG_CATEGORY "Vfs"

typedef struct FluxionVfsMount
{
    char scheme[FLUXION_VFS_MAX_SCHEME_LENGTH + 1];
    FluxionVfsSource* sources[FLUXION_VFS_MAX_SOURCES_PER_MOUNT];
    u32 sourceCount;
} FluxionVfsMount;

static FluxionAllocator* s_allocator = NULL;
static FluxionVfsMount s_mounts[FLUXION_VFS_MAX_MOUNTS];
static u32 s_mountCount = 0;
static bool s_initialized = false;

void Fluxion_Vfs_Init(FluxionAllocator* allocator)
{
    FLUXION_ASSERT_MSG(!s_initialized, "Fluxion_Vfs_Init called twice without a Shutdown in between");

    s_allocator = allocator ? allocator : Fluxion_DefaultAllocator();
    memset(s_mounts, 0, sizeof(s_mounts));
    s_mountCount = 0;
    s_initialized = true;
}

void Fluxion_Vfs_Shutdown(void)
{
    if (!s_initialized) return;

    for (u32 mountIndex = 0; mountIndex < s_mountCount; ++mountIndex)
    {
        FluxionVfsMount* mount = &s_mounts[mountIndex];
        for (u32 sourceIndex = 0; sourceIndex < mount->sourceCount; ++sourceIndex)
        {
            FluxionVfsSource* source = mount->sources[sourceIndex];
            if (source && source->vtable && source->vtable->destroy) source->vtable->destroy(source);
        }
    }

    memset(s_mounts, 0, sizeof(s_mounts));
    s_mountCount = 0;
    s_allocator = NULL;
    s_initialized = false;
}

bool Fluxion_Vfs_IsInitialized(void)
{
    return s_initialized;
}

FluxionAllocator* Fluxion_Vfs_GetAllocator(void)
{
    return s_allocator;
}

void Fluxion_Vfs_FreeBuffer(u8* buffer, usize size)
{
    if (!buffer) return;
    Fluxion_Allocator_Free(s_allocator ? s_allocator : Fluxion_DefaultAllocator(), buffer, size);
}

static bool Fluxion_Vfs_IsSchemeCharacter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

bool Fluxion_Vfs_SplitPath(const char* path,
                           char* outScheme, usize schemeCapacity,
                           char* outRelative, usize relativeCapacity)
{
    if (!path || !outScheme || !outRelative || schemeCapacity == 0 || relativeCapacity == 0) return false;

    const char* separator = strstr(path, "://");
    if (!separator || separator == path) return false;

    const usize schemeLength = (usize)(separator - path);
    if (schemeLength >= schemeCapacity) return false;

    for (usize i = 0; i < schemeLength; ++i)
    {
        if (!Fluxion_Vfs_IsSchemeCharacter(path[i])) return false;
    }

    const char* relative = separator + 3;
    const usize relativeLength = strlen(relative);
    if (relativeLength == 0 || relativeLength >= relativeCapacity) return false;

    // Everything below is one question asked several ways: can this path
    // name anything outside the mount it claims to be inside? A single
    // ".." makes an asset path a way out, so none of these are style.
    if (relative[0] == '/') return false;

    usize segmentStart = 0;
    for (usize i = 0; i <= relativeLength; ++i)
    {
        const char c = relative[i];

        if (c == '\\' || c == ':') return false;

        if (c == '/' || c == '\0')
        {
            const usize segmentLength = i - segmentStart;
            if (segmentLength == 0) return false;
            if (segmentLength == 1 && relative[segmentStart] == '.') return false;
            if (segmentLength == 2 && relative[segmentStart] == '.' && relative[segmentStart + 1] == '.') return false;
            segmentStart = i + 1;
        }
    }

    memcpy(outScheme, path, schemeLength);
    outScheme[schemeLength] = '\0';
    memcpy(outRelative, relative, relativeLength + 1);
    return true;
}

static FluxionVfsMount* Fluxion_Vfs_FindMount(const char* scheme)
{
    for (u32 i = 0; i < s_mountCount; ++i)
    {
        if (strcmp(s_mounts[i].scheme, scheme) == 0) return &s_mounts[i];
    }
    return NULL;
}

static void Fluxion_Vfs_DestroySource(FluxionVfsSource* source)
{
    if (source && source->vtable && source->vtable->destroy) source->vtable->destroy(source);
}

bool Fluxion_Vfs_Mount(const char* scheme, FluxionVfsSource* source)
{
    // The source is owned from here on even when this fails: a caller that
    // was handed a source by a Create function has no other way to
    // destroy it, so returning it unowned would leak it every time.
    if (!s_initialized || !scheme || !source || !source->vtable)
    {
        Fluxion_Vfs_DestroySource(source);
        return false;
    }

    const usize schemeLength = strlen(scheme);
    if (schemeLength == 0 || schemeLength > FLUXION_VFS_MAX_SCHEME_LENGTH)
    {
        Fluxion_Vfs_DestroySource(source);
        return false;
    }

    for (usize i = 0; i < schemeLength; ++i)
    {
        if (!Fluxion_Vfs_IsSchemeCharacter(scheme[i]))
        {
            Fluxion_Vfs_DestroySource(source);
            return false;
        }
    }

    FluxionVfsMount* mount = Fluxion_Vfs_FindMount(scheme);
    if (!mount)
    {
        if (s_mountCount >= FLUXION_VFS_MAX_MOUNTS)
        {
            FLUXION_LOG_ERROR(FLUXION_VFS_LOG_CATEGORY, "cannot mount '%s': no free mount slot", scheme);
            Fluxion_Vfs_DestroySource(source);
            return false;
        }
        mount = &s_mounts[s_mountCount++];
        memcpy(mount->scheme, scheme, schemeLength + 1);
        mount->sourceCount = 0;
    }

    if (mount->sourceCount >= FLUXION_VFS_MAX_SOURCES_PER_MOUNT)
    {
        FLUXION_LOG_ERROR(FLUXION_VFS_LOG_CATEGORY, "cannot mount another source under '%s'", scheme);
        Fluxion_Vfs_DestroySource(source);
        return false;
    }

    mount->sources[mount->sourceCount++] = source;
    return true;
}

bool Fluxion_Vfs_UnmountAll(const char* scheme)
{
    if (!s_initialized || !scheme) return false;

    FluxionVfsMount* mount = Fluxion_Vfs_FindMount(scheme);
    if (!mount) return false;

    for (u32 i = 0; i < mount->sourceCount; ++i) Fluxion_Vfs_DestroySource(mount->sources[i]);

    const u32 mountIndex = (u32)(mount - s_mounts);
    const u32 lastIndex = s_mountCount - 1;
    if (mountIndex != lastIndex) s_mounts[mountIndex] = s_mounts[lastIndex];
    memset(&s_mounts[lastIndex], 0, sizeof(s_mounts[lastIndex]));
    --s_mountCount;
    return true;
}

u32 Fluxion_Vfs_GetSourceCount(const char* scheme)
{
    if (!s_initialized || !scheme) return 0;
    const FluxionVfsMount* mount = Fluxion_Vfs_FindMount(scheme);
    return mount ? mount->sourceCount : 0;
}

// Turns a full path into the mount it names and the part below it. NULL
// when the path is malformed, points outside its mount, or names a scheme
// nothing is mounted under -- three different mistakes with the same
// answer, because none of them can be served.
static FluxionVfsMount* Fluxion_Vfs_Resolve(const char* path, char* outRelative, usize relativeCapacity)
{
    char scheme[FLUXION_VFS_MAX_SCHEME_LENGTH + 1];

    if (!s_initialized) return NULL;
    if (!Fluxion_Vfs_SplitPath(path, scheme, sizeof(scheme), outRelative, relativeCapacity)) return NULL;
    return Fluxion_Vfs_FindMount(scheme);
}

bool Fluxion_Vfs_Exists(const char* path)
{
    char relative[FLUXION_VFS_MAX_PATH];
    const FluxionVfsMount* mount = Fluxion_Vfs_Resolve(path, relative, sizeof(relative));
    if (!mount) return false;

    // Later mounts shadow earlier ones, so every search runs backwards.
    for (u32 i = mount->sourceCount; i > 0; --i)
    {
        FluxionVfsSource* source = mount->sources[i - 1];
        if (source->vtable->exists && source->vtable->exists(source, relative)) return true;
    }
    return false;
}

u8* Fluxion_Vfs_ReadAll(const char* path, usize* outSize)
{
    if (outSize) *outSize = 0;

    char relative[FLUXION_VFS_MAX_PATH];
    const FluxionVfsMount* mount = Fluxion_Vfs_Resolve(path, relative, sizeof(relative));
    if (!mount) return NULL;

    for (u32 i = mount->sourceCount; i > 0; --i)
    {
        FluxionVfsSource* source = mount->sources[i - 1];
        if (!source->vtable->readAll) continue;

        usize size = 0;
        u8* bytes = source->vtable->readAll(source, relative, &size);
        if (bytes)
        {
            if (outSize) *outSize = size;
            return bytes;
        }
    }

    return NULL;
}

bool Fluxion_Vfs_WriteAll(const char* path, const void* data, usize size)
{
    char relative[FLUXION_VFS_MAX_PATH];
    const FluxionVfsMount* mount = Fluxion_Vfs_Resolve(path, relative, sizeof(relative));
    if (!mount) return false;

    for (u32 i = mount->sourceCount; i > 0; --i)
    {
        FluxionVfsSource* source = mount->sources[i - 1];
        if (source->vtable->writeAll) return source->vtable->writeAll(source, relative, data, size);
    }

    FLUXION_LOG_ERROR(FLUXION_VFS_LOG_CATEGORY, "'%s' has no writable source", path);
    return false;
}

// ---------------------------------------------------------------------
// The directory source.
//
// The one place allowed to believe a path opens a file. Everything above
// it goes through the source interface instead, which is what lets a
// platform that has no such path be added underneath without any of them
// changing.
// ---------------------------------------------------------------------

typedef struct FluxionVfsDirectorySource
{
    FluxionVfsSource base;
    char root[FLUXION_VFS_MAX_PATH];
    usize rootLength;
} FluxionVfsDirectorySource;

static bool Fluxion_VfsDirectorySource_BuildPath(const FluxionVfsDirectorySource* self, const char* relative, char* outPath, usize capacity)
{
    const usize relativeLength = strlen(relative);
    const bool needsSeparator = self->rootLength > 0 && self->root[self->rootLength - 1] != '/';
    const usize total = self->rootLength + (needsSeparator ? 1u : 0u) + relativeLength;
    if (total + 1 > capacity) return false;

    memcpy(outPath, self->root, self->rootLength);
    usize offset = self->rootLength;
    if (needsSeparator) outPath[offset++] = '/';
    memcpy(outPath + offset, relative, relativeLength);
    outPath[offset + relativeLength] = '\0';
    return true;
}

static bool Fluxion_VfsDirectorySource_Exists(FluxionVfsSource* source, const char* relative)
{
    const FluxionVfsDirectorySource* self = (const FluxionVfsDirectorySource*)source;
    char full[FLUXION_VFS_MAX_PATH];
    if (!Fluxion_VfsDirectorySource_BuildPath(self, relative, full, sizeof(full))) return false;
    return Fluxion_Platform_FileExists(full);
}

static u8* Fluxion_VfsDirectorySource_ReadAll(FluxionVfsSource* source, const char* relative, usize* outSize)
{
    const FluxionVfsDirectorySource* self = (const FluxionVfsDirectorySource*)source;
    char full[FLUXION_VFS_MAX_PATH];
    if (!Fluxion_VfsDirectorySource_BuildPath(self, relative, full, sizeof(full))) return NULL;

    FluxionFile file;
    if (!Fluxion_Platform_FileOpen(&file, full, FLUXION_FILE_OPEN_READ)) return NULL;

    const i64 fileSize = Fluxion_Platform_FileSize(&file);
    if (fileSize < 0)
    {
        Fluxion_Platform_FileClose(&file);
        return NULL;
    }

    const usize size = (usize)fileSize;

    // A zero-byte file is a file: it must not come back as NULL, which is
    // how "this source does not have it" is said. One byte is allocated so
    // the answer is a pointer either way.
    u8* bytes = (u8*)Fluxion_Allocator_Alloc(Fluxion_Vfs_GetAllocator(), size > 0 ? size : 1u, FLUXION_DEFAULT_ALIGNMENT);
    if (!bytes)
    {
        Fluxion_Platform_FileClose(&file);
        return NULL;
    }

    // Read in a loop rather than once: a read is allowed to hand back
    // fewer bytes than were asked for, and treating a short read as a
    // failure would make large files fail sometimes rather than never.
    usize filled = 0;
    while (filled < size)
    {
        const usize read = Fluxion_Platform_FileRead(&file, bytes + filled, size - filled);
        if (read == 0)
        {
            Fluxion_Allocator_Free(Fluxion_Vfs_GetAllocator(), bytes, size > 0 ? size : 1u);
            Fluxion_Platform_FileClose(&file);
            return NULL;
        }
        filled += read;
    }

    Fluxion_Platform_FileClose(&file);
    if (outSize) *outSize = size;
    return bytes;
}

// Makes every directory the path leads through, below the root. A mount
// that can be written to, but only where someone already made a folder,
// is a trap: the first save a game ever writes is exactly the one that
// would fail.
//
// The walk starts below the root on purpose. Above it are directories
// this has no business making -- and on a host whose root path begins
// with a drive, trying would fail at the first step and take an
// otherwise fine write with it.
static bool Fluxion_VfsDirectorySource_MakeParents(char* full, usize rootLength)
{
    for (usize i = rootLength; full[i] != '\0'; ++i)
    {
        if (full[i] != '/') continue;

        full[i] = '\0';
        const bool ok = Fluxion_Platform_DirectoryCreate(full);
        full[i] = '/';

        if (!ok) return false;
    }

    return true;
}

static bool Fluxion_VfsDirectorySource_WriteAll(FluxionVfsSource* source, const char* relative, const void* data, usize size)
{
    const FluxionVfsDirectorySource* self = (const FluxionVfsDirectorySource*)source;
    char full[FLUXION_VFS_MAX_PATH];
    if (!Fluxion_VfsDirectorySource_BuildPath(self, relative, full, sizeof(full))) return false;
    if (!Fluxion_VfsDirectorySource_MakeParents(full, self->rootLength)) return false;

    FluxionFile file;
    if (!Fluxion_Platform_FileOpen(&file, full, FLUXION_FILE_OPEN_WRITE)) return false;

    // Same reasoning as the read above: a write may take fewer bytes than
    // it was offered.
    bool ok = true;
    usize written = 0;
    while (written < size)
    {
        const usize step = Fluxion_Platform_FileWrite(&file, (const u8*)data + written, size - written);
        if (step == 0)
        {
            ok = false;
            break;
        }
        written += step;
    }

    Fluxion_Platform_FileClose(&file);
    return ok;
}

static void Fluxion_VfsDirectorySource_Destroy(FluxionVfsSource* source)
{
    Fluxion_Allocator_Free(Fluxion_Vfs_GetAllocator(), source, sizeof(FluxionVfsDirectorySource));
}

static const FluxionVfsSourceVTable s_directoryVTable = {
    Fluxion_VfsDirectorySource_Exists,
    Fluxion_VfsDirectorySource_ReadAll,
    Fluxion_VfsDirectorySource_WriteAll,
    Fluxion_VfsDirectorySource_Destroy,
};

FluxionVfsSource* Fluxion_VfsDirectorySource_Create(const char* rootPath)
{
    if (!s_initialized || !rootPath) return NULL;

    const usize rootLength = strlen(rootPath);
    if (rootLength == 0 || rootLength >= FLUXION_VFS_MAX_PATH) return NULL;

    FluxionVfsDirectorySource* self =
        (FluxionVfsDirectorySource*)Fluxion_Allocator_Alloc(s_allocator, sizeof(FluxionVfsDirectorySource), FLUXION_DEFAULT_ALIGNMENT);
    if (!self) return NULL;

    self->base.vtable = &s_directoryVTable;
    memcpy(self->root, rootPath, rootLength + 1);
    self->rootLength = rootLength;
    return &self->base;
}
