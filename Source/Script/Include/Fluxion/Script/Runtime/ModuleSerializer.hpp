#pragma once

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Bytecode.hpp>

#include <cstddef>
#include <vector>

namespace Fluxion::Script
{

// Turning a compiled module into bytes and back, so a module can outlive
// the run that produced it.
//
// The versioned header a module already carries -- a signature plus the
// language, instruction-set and engine-interface versions it was built
// against -- is what makes this safe to do at all, and it is written out
// first so a reader can refuse before it has allocated anything.
//
// The reader assumes nothing about what it is handed. A file on disk may
// have been truncated by a crash, half-written by another process, or
// filled with something else entirely, so no length and no index read out
// of it is trusted until it has been checked against how many bytes
// actually arrived and against what the rest of the image says. A reader
// that finds anything it cannot account for refuses the whole image and
// says why, rather than handing back something half-formed for the
// interpreter to discover the hard way.

// Appends the module's image to `outBytes`, which is cleared first. Fails
// only when some part of the module is larger than the format can name,
// which takes a module with more than four thousand million of something.
bool WriteModule(const BytecodeModule& module, std::vector<u8>& outBytes);

// Reads one back. `outModule` is left as it was when this refuses, and
// `outDiagnostics` says what was wrong with the image. The whole of
// `byteCount` has to be one module: bytes left over at the end are as much
// a refusal as bytes missing from the middle, since either means this is
// not the image it claims to be.
bool ReadModule(const u8* bytes, size_t byteCount, BytecodeModule& outModule, DiagnosticList& outDiagnostics);

} // namespace Fluxion::Script
