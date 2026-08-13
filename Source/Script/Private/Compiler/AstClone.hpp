#pragma once

#include <Fluxion/Script/Compiler/Ast.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace Fluxion::Script
{

// A declaration written with type parameters is a pattern, not a type: it
// is never laid out and never emitted. Each distinct set of arguments it
// is used with gets its own copy of the whole declaration -- fields,
// method bodies and all -- with the parameter names replaced by the
// arguments, and that copy is what gets a layout, a field bitmap, tables
// and instructions of its own.
//
// Copying the tree rather than walking the original once per argument set
// is what keeps the two apart: the analyzer annotates nodes in place, so
// two argument sets sharing one tree would each overwrite the other's
// resolved types, frame slots and call targets.
std::unique_ptr<ClassDecl> CloneClassDecl(const ClassDecl& source);

// Replaces every mention of a type parameter in `target` with the type it
// stands for. Only types are rewritten -- a name used as a value is left
// alone, so a parameter used where a value belongs is reported as the
// undeclared name it is.
//
// `substitution` maps a parameter name to an already-resolved type, and a
// parameter written with `[]` after it keeps those: `T[]` under `T = int`
// becomes `int[]`.
void SubstituteTypeParameters(ClassDecl& target, const std::unordered_map<std::string, TypeRef>& substitution);

} // namespace Fluxion::Script
