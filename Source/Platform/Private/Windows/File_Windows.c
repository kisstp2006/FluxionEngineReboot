#include <Fluxion/Platform/File.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

bool Fluxion_Platform_FileOpen(FluxionFile* file, const char* path, FluxionFileOpenMode mode)
{
    DWORD access = 0;
    DWORD creation = 0;

    switch (mode)
    {
        case FLUXION_FILE_OPEN_READ:
            access = GENERIC_READ;
            creation = OPEN_EXISTING;
            break;
        case FLUXION_FILE_OPEN_WRITE:
            access = GENERIC_WRITE;
            creation = CREATE_ALWAYS;
            break;
        case FLUXION_FILE_OPEN_APPEND:
            access = GENERIC_WRITE;
            creation = OPEN_ALWAYS;
            break;
    }

    HANDLE handle = CreateFileA(path, access, FILE_SHARE_READ, NULL, creation, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        file->handle = NULL;
        return false;
    }

    if (mode == FLUXION_FILE_OPEN_APPEND)
    {
        SetFilePointer(handle, 0, NULL, FILE_END);
    }

    file->handle = (void*)handle;
    return true;
}

void Fluxion_Platform_FileClose(FluxionFile* file)
{
    if (file->handle)
    {
        CloseHandle((HANDLE)file->handle);
        file->handle = NULL;
    }
}

usize Fluxion_Platform_FileRead(FluxionFile* file, void* buffer, usize size)
{
    DWORD bytesRead = 0;
    if (!ReadFile((HANDLE)file->handle, buffer, (DWORD)size, &bytesRead, NULL))
    {
        return 0;
    }
    return (usize)bytesRead;
}

usize Fluxion_Platform_FileWrite(FluxionFile* file, const void* buffer, usize size)
{
    DWORD bytesWritten = 0;
    if (!WriteFile((HANDLE)file->handle, buffer, (DWORD)size, &bytesWritten, NULL))
    {
        return 0;
    }
    return (usize)bytesWritten;
}

i64 Fluxion_Platform_FileSize(FluxionFile* file)
{
    LARGE_INTEGER size;
    if (!GetFileSizeEx((HANDLE)file->handle, &size))
    {
        return -1;
    }
    return (i64)size.QuadPart;
}

bool Fluxion_Platform_FileSeek(FluxionFile* file, i64 offset)
{
    if (!file->handle || offset < 0) return false;

    LARGE_INTEGER distance;
    distance.QuadPart = offset;
    return SetFilePointerEx((HANDLE)file->handle, distance, NULL, FILE_BEGIN) != 0;
}

bool Fluxion_Platform_FileExists(const char* path)
{
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool Fluxion_Platform_FileDelete(const char* path)
{
    return DeleteFileA(path) != 0;
}

bool Fluxion_Platform_DirectoryExists(const char* path)
{
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool Fluxion_Platform_DirectoryCreate(const char* path)
{
    if (CreateDirectoryA(path, NULL)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS && Fluxion_Platform_DirectoryExists(path);
}
