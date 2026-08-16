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

// An engine-owned wrapper around whatever opaque blob a backend's own
// pipeline cache hands out.
//
// Every backend's blob carries some notion of what produced it, but what
// they do about a mismatch differs, and none of them tells the caller.
// A Vulkan driver silently ignores a blob whose header does not match and
// still reports the cache as created; a D3D12 pipeline library reports a
// specific mismatch but only for the cases it recognises. In both cases a
// caller asking "did my cache load?" gets yes for a file that contributed
// nothing -- which is the same answer it gets for a file that worked, so
// there is no way to tell a cold start from a broken one.
//
// Wrapping the blob in a header we write ourselves moves that decision to
// where the answer is knowable: a file that was not written by this
// engine, this format version, this backend, or this adapter is refused
// here, before the driver ever sees it, and the refusal is reported.
//
// The payload hash is not redundant with those fields. They catch a file
// meant for something else; the hash catches a file meant for exactly
// this and damaged since -- a truncated write, a half-synced copy.

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/RHI.h>

#include <vector>

struct FluxionRHIPipelineCacheIdentity
{
    FluxionRHIBackendType backend;
    u32 vendorId;
    u32 deviceId;
    u32 driverVersion;

    // Whatever else the backend considers part of "the same device", for
    // the ones that have no numeric ids to compare. OpenGL has only the
    // vendor/renderer/version strings, so it hashes those into here;
    // backends with real ids leave it at zero.
    u64 extra;
};

// Writes header + payload to a temporary alongside `path` and renames it
// over the target, so a reader never sees a partially written file and a
// failed write leaves any previous cache intact.
bool Fluxion_RHIPipelineCacheFile_Write(const char* path, const FluxionRHIPipelineCacheIdentity& identity, const void* payload, usize payloadSize);

// Fills outPayload only on success. Returns false for a missing,
// truncated, foreign, or damaged file -- all of which mean the same thing
// to the caller (start cold), and none of which are an error worth
// failing a frame over.
bool Fluxion_RHIPipelineCacheFile_Read(const char* path, const FluxionRHIPipelineCacheIdentity& identity, std::vector<u8>* outPayload);
