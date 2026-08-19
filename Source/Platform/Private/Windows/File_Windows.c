// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

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

bool Fluxion_Platform_FileReplace(const char* from, const char* to)
{
    if (from == NULL || to == NULL) return false;

    // WRITE_THROUGH as well as REPLACE_EXISTING: the move is only worth
    // making if what arrives is the whole file, and this asks for the
    // contents to be on the disc before the name points at them.
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool Fluxion_Platform_FileStat(const char* path, u64* outModifiedTime, u64* outSize)
{
    if (path == NULL || outModifiedTime == NULL || outSize == NULL) return false;

    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) return false;
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;

    // Two thirty-two-bit halves of one number, which is how this platform
    // reports a time. Put back together rather than compared as a pair,
    // so that whoever asked has one value to remember.
    *outModifiedTime = ((u64)info.ftLastWriteTime.dwHighDateTime << 32) | (u64)info.ftLastWriteTime.dwLowDateTime;
    *outSize = ((u64)info.nFileSizeHigh << 32) | (u64)info.nFileSizeLow;
    return true;
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
