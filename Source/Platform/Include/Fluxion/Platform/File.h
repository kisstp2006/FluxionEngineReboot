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

// PUTS `from` WHERE `to` IS, IN ONE STEP, replacing whatever was there.
// False leaves both where they were.
//
// One step is the point. A file written in place does not exist as a
// half-written file for a moment -- it exists as one for as long as the
// write takes, and anything that reads it in that moment reads nonsense
// that looks like data. Writing beside the target and then moving it
// here means a reader sees the old file or the new one, never a mixture.
//
// Measured, before this existed: two copies of the same program running
// at once, one rewriting the assets while the other's file watcher read
// them back, produced an asset whose name was four bytes of rubbish --
// and the program that read it shut itself down over a file that was
// perfectly good a moment later.
bool  Fluxion_Platform_FileReplace(const char* from, const char* to);

// When a file was last written and how large it is, WITHOUT opening it
// -- for noticing change. The time is in whatever unit the platform
// keeps, comparable only with an earlier answer for the same file. Both
// are reported because neither suffices alone: second-granular times
// cannot tell two writes apart, and a same-length rewrite keeps its
// size. False when the file cannot be asked about; the outputs are then
// left alone.
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
