#pragma once

#include <Fluxion/Foundation/Result.hpp>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Script.hpp>

#include <string>

namespace Fluxion::Script
{

// Compiling is the slowest thing a host does with a script, and almost
// every run compiles exactly what the last run did. This keeps the image
// the compiler produced beside the source it was produced from, so a run
// that would have produced the same thing reads it instead.
//
// What counts as "the same thing" is everything that goes into the answer
// and nothing else: the source, the language's own prelude, whatever
// prelude the host added, the versions of the language and the instruction
// set this build understands, and the shape of the engine types the source
// was compiled against. Change any of them and the entry no longer
// answers -- a stale image would otherwise be loaded against an engine it
// no longer matches.
//
// A cache is an optimization and never a source of failure. A file that is
// missing, truncated, filled with something else, or written by an older
// build is a miss and nothing more: the compiler runs, and the answer
// replaces whatever was there. Nothing here reports such a file as an
// error, because from the caller's point of view nothing went wrong.

struct CompileCacheOptions
{
    // Where the images are kept. Created if it is not there. Empty turns
    // the whole thing off: every lookup misses and nothing is written, so
    // a host that does not want a cache does not have to reach for a
    // different call.
    std::string directory;

    // Set to leave whatever is on disk alone. A hit still answers out of
    // it; a miss still compiles. This is for a caller that wants to read a
    // cache it did not build and must not disturb.
    bool readOnly = false;
};

struct CompileCacheReport
{
    // True when the module came out of a file rather than the compiler.
    bool wasCached = false;

    // True when this call put the compiler's answer on disk. False on a
    // hit, when the cache is off or read-only, and when the file could not
    // be written -- which is not an error either.
    bool wrote = false;

    // The file this call looked in, empty when the cache is off. Named so
    // a host can say where its cache lives without working out the
    // arrangement for itself.
    std::string path;
};

// Compiles `source` unless an image of exactly this compilation is already
// on disk. Diagnostics are the compiler's own: a hit produces none, since
// nothing was compiled, and a warning a previous compilation reported does
// not come back with the image.
Fluxion::Foundation::Result<CompiledModule> CompileCached(const std::string& source, const CompileOptions& options,
    const CompileCacheOptions& cache, DiagnosticList& outDiagnostics, CompileCacheReport& outReport);

// How many compilations this process has actually run through the call
// above, and how many it answered out of a file instead. Both only ever go
// up. "The cache is working" is otherwise only visible as the run being
// quicker, which proves nothing on a machine doing anything else at the
// same time -- these are what a test asserts against. Counted without any
// synchronization, so they are exact only for a host driving compilation
// from one thread.
struct CompileCacheCounters
{
    u64 compiled = 0;
    u64 loaded = 0;
};

CompileCacheCounters GetCompileCacheCounters();

} // namespace Fluxion::Script
