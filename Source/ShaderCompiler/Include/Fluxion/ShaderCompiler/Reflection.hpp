#pragma once

#include <Fluxion/ShaderCompiler/IR/ShaderIR.hpp>

namespace Fluxion::ShaderCompiler
{

// The IR module already carries every piece of reflection data a caller
// needs (stage IO locations, resource bindings, push-constant layout) --
// this alias just names that view for callers that only care about
// reflection, without a second struct duplicating the same fields.
using ShaderReflection = ShaderIRModule;

} // namespace Fluxion::ShaderCompiler
