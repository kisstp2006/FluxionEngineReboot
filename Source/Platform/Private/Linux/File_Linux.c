#include <Fluxion/Platform/File.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

bool Fluxion_Platform_FileOpen(FluxionFile* file, const char* path, FluxionFileOpenMode mode)
{
    int flags = 0;
    mode_t permissions = 0644;

    switch (mode)
    {
        case FLUXION_FILE_OPEN_READ:
            flags = O_RDONLY;
            break;
        case FLUXION_FILE_OPEN_WRITE:
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            break;
        case FLUXION_FILE_OPEN_APPEND:
            flags = O_WRONLY | O_CREAT | O_APPEND;
            break;
    }

    int fd = open(path, flags, permissions);
    if (fd < 0)
    {
        file->handle = NULL;
        return false;
    }

    // NULL is the "no handle" sentinel; fd 0 (stdin) is never returned
    // here in practice since the standard streams already hold fds 0-2
    // open for the lifetime of the process.
    file->handle = (void*)(intptr_t)fd;
    return true;
}

void Fluxion_Platform_FileClose(FluxionFile* file)
{
    if (file->handle)
    {
        close((int)(intptr_t)file->handle);
        file->handle = NULL;
    }
}

usize Fluxion_Platform_FileRead(FluxionFile* file, void* buffer, usize size)
{
    ssize_t result = read((int)(intptr_t)file->handle, buffer, size);
    return result < 0 ? 0 : (usize)result;
}

usize Fluxion_Platform_FileWrite(FluxionFile* file, const void* buffer, usize size)
{
    ssize_t result = write((int)(intptr_t)file->handle, buffer, size);
    return result < 0 ? 0 : (usize)result;
}

i64 Fluxion_Platform_FileSize(FluxionFile* file)
{
    struct stat st;
    if (fstat((int)(intptr_t)file->handle, &st) != 0)
    {
        return -1;
    }
    return (i64)st.st_size;
}

bool Fluxion_Platform_FileSeek(FluxionFile* file, i64 offset)
{
    if (!file->handle || offset < 0) return false;
    return lseek((int)(intptr_t)file->handle, (off_t)offset, SEEK_SET) == (off_t)offset;
}

bool Fluxion_Platform_FileExists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool Fluxion_Platform_FileDelete(const char* path)
{
    return unlink(path) == 0;
}

bool Fluxion_Platform_FileStat(const char* path, u64* outModifiedTime, u64* outSize)
{
    if (path == NULL || outModifiedTime == NULL || outSize == NULL) return false;

    struct stat info;
    if (stat(path, &info) != 0) return false;
    if (S_ISDIR(info.st_mode)) return false;

    // Nanoseconds where the file system keeps them. Seconds alone cannot
    // tell two writes in the same second apart, and that is exactly the
    // case somebody editing a file and looking straight at the result
    // runs into.
    *outModifiedTime = (u64)info.st_mtim.tv_sec * 1000000000ull + (u64)info.st_mtim.tv_nsec;
    *outSize = (u64)info.st_size;
    return true;
}

bool Fluxion_Platform_DirectoryExists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool Fluxion_Platform_DirectoryCreate(const char* path)
{
    if (mkdir(path, 0755) == 0) return true;
    return errno == EEXIST && Fluxion_Platform_DirectoryExists(path);
}
