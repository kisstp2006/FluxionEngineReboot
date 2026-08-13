#include <Fluxion/Script/Runtime/Bytecode.hpp>

namespace Fluxion::Script
{

const char* OpCodeName(OpCode op)
{
    switch (op)
    {
        case OpCode::Nop: return "Nop";

        case OpCode::PushInt: return "PushInt";
        case OpCode::PushFloat: return "PushFloat";
        case OpCode::PushBool: return "PushBool";
        case OpCode::PushString: return "PushString";

        case OpCode::LoadLocal: return "LoadLocal";
        case OpCode::StoreLocal: return "StoreLocal";
        case OpCode::Pop: return "Pop";

        case OpCode::AddInt: return "AddInt";
        case OpCode::SubInt: return "SubInt";
        case OpCode::MulInt: return "MulInt";
        case OpCode::DivInt: return "DivInt";
        case OpCode::ModInt: return "ModInt";
        case OpCode::NegInt: return "NegInt";

        case OpCode::AddFloat: return "AddFloat";
        case OpCode::SubFloat: return "SubFloat";
        case OpCode::MulFloat: return "MulFloat";
        case OpCode::DivFloat: return "DivFloat";
        case OpCode::ModFloat: return "ModFloat";
        case OpCode::NegFloat: return "NegFloat";

        case OpCode::ConcatString: return "ConcatString";

        case OpCode::EqualInt: return "EqualInt";
        case OpCode::NotEqualInt: return "NotEqualInt";
        case OpCode::LessInt: return "LessInt";
        case OpCode::GreaterInt: return "GreaterInt";
        case OpCode::LessEqualInt: return "LessEqualInt";
        case OpCode::GreaterEqualInt: return "GreaterEqualInt";

        case OpCode::EqualFloat: return "EqualFloat";
        case OpCode::NotEqualFloat: return "NotEqualFloat";
        case OpCode::LessFloat: return "LessFloat";
        case OpCode::GreaterFloat: return "GreaterFloat";
        case OpCode::LessEqualFloat: return "LessEqualFloat";
        case OpCode::GreaterEqualFloat: return "GreaterEqualFloat";

        case OpCode::EqualBool: return "EqualBool";
        case OpCode::NotEqualBool: return "NotEqualBool";
        case OpCode::EqualString: return "EqualString";
        case OpCode::NotEqualString: return "NotEqualString";

        case OpCode::NotBool: return "NotBool";

        case OpCode::IntToFloat: return "IntToFloat";
        case OpCode::IntToString: return "IntToString";
        case OpCode::FloatToString: return "FloatToString";
        case OpCode::BoolToString: return "BoolToString";

        case OpCode::Jump: return "Jump";
        case OpCode::JumpIfFalse: return "JumpIfFalse";
        case OpCode::JumpIfTrue: return "JumpIfTrue";

        case OpCode::Call: return "Call";
        case OpCode::CallNative: return "CallNative";
        case OpCode::Return: return "Return";
        case OpCode::ReturnVoid: return "ReturnVoid";

        case OpCode::Halt: return "Halt";
        default: return "<unknown opcode>";
    }
}

const NativeFunctionSignature* NativeFunctionTable()
{
    // Ordered to match NativeFunctionId exactly -- the index is the
    // identity, both for the compiler binding a call and for the
    // interpreter dispatching one.
    static const NativeFunctionSignature table[kNativeFunctionCount] = {
        { "Console.WriteLine", ValueType::String },
        { "Console.WriteLine", ValueType::Int },
        { "Console.WriteLine", ValueType::Float },
        { "Console.WriteLine", ValueType::Bool },

        { "Console.Write", ValueType::String },
        { "Console.Write", ValueType::Int },
        { "Console.Write", ValueType::Float },
        { "Console.Write", ValueType::Bool },

        { "Debug.Log", ValueType::String },
        { "Debug.LogWarning", ValueType::String },
        { "Debug.LogError", ValueType::String },
    };
    return table;
}

} // namespace Fluxion::Script
