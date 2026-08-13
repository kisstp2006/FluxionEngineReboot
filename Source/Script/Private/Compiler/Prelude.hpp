#pragma once

namespace Fluxion::Script
{

// Types every program can use without declaring them, written in the
// language itself and compiled ahead of the caller's source into the same
// module. Nothing here is special-cased anywhere else: a growable list is
// a declaration with type parameters over a sequence, which is precisely
// what a program of the caller's own could have written.
//
// A declaration with type parameters costs nothing until something names
// it with arguments, so a program that never uses these carries no trace
// of them.
const char* PreludeSource();

// The name diagnostics from the prelude are reported against. A message
// carrying it means the prelude itself failed to compile, which is a
// defect in this module rather than anything the caller did.
const char* PreludeSourceName();

} // namespace Fluxion::Script
