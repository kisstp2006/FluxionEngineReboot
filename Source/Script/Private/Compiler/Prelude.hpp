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
// of them. A value type is not free in the same way -- it is laid out
// whether or not anything names it -- but it is only a handful of numbers
// and a set of methods, and having every program agree on what a position
// or a colour is worth far more than that.
const char* PreludeSource();

// The name diagnostics from the prelude are reported against. A message
// carrying it means the prelude itself failed to compile, which is a
// defect in this module rather than anything the caller did.
const char* PreludeSourceName();

} // namespace Fluxion::Script
