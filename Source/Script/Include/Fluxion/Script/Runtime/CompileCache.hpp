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
