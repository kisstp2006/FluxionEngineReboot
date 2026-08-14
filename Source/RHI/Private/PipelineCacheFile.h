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
