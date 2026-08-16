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
        case OpCode::PushNull: return "PushNull";
        case OpCode::PushZero: return "PushZero";

        case OpCode::LoadLocal: return "LoadLocal";
        case OpCode::StoreLocal: return "StoreLocal";
        case OpCode::LoadLocalWide: return "LoadLocalWide";
        case OpCode::StoreLocalWide: return "StoreLocalWide";
        case OpCode::Pop: return "Pop";
        case OpCode::Dup: return "Dup";
        case OpCode::Dup2: return "Dup2";
        case OpCode::DiscardUnder: return "DiscardUnder";

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
        case OpCode::EqualHandle: return "EqualHandle";
        case OpCode::NotEqualHandle: return "NotEqualHandle";
        case OpCode::EqualRef: return "EqualRef";
        case OpCode::NotEqualRef: return "NotEqualRef";

        case OpCode::NotBool: return "NotBool";

        case OpCode::IntToFloat: return "IntToFloat";
        case OpCode::FloatToInt: return "FloatToInt";
        case OpCode::IntToString: return "IntToString";
        case OpCode::FloatToString: return "FloatToString";
        case OpCode::BoolToString: return "BoolToString";

        case OpCode::NewObject: return "NewObject";
        case OpCode::LoadField: return "LoadField";
        case OpCode::StoreField: return "StoreField";
        case OpCode::LoadFieldWide: return "LoadFieldWide";
        case OpCode::StoreFieldWide: return "StoreFieldWide";

        case OpCode::NewArray: return "NewArray";
        case OpCode::ArrayLength: return "ArrayLength";
        case OpCode::LoadElement: return "LoadElement";
        case OpCode::StoreElement: return "StoreElement";
        case OpCode::LoadElementField: return "LoadElementField";
        case OpCode::StoreElementField: return "StoreElementField";

        case OpCode::Jump: return "Jump";
        case OpCode::JumpIfFalse: return "JumpIfFalse";
        case OpCode::JumpIfTrue: return "JumpIfTrue";

        case OpCode::Call: return "Call";
        case OpCode::CallVirtual: return "CallVirtual";
        case OpCode::CallInterface: return "CallInterface";
        case OpCode::CallNative: return "CallNative";
        case OpCode::CallBound: return "CallBound";
        case OpCode::Return: return "Return";
        case OpCode::ReturnVoid: return "ReturnVoid";
        case OpCode::ReturnWide: return "ReturnWide";

        case OpCode::SafePoint: return "SafePoint";

        case OpCode::Halt: return "Halt";
        default: return "<unknown opcode>";
    }
}

namespace
{

// One shared list per accepted argument list, so a signature points at
// storage that outlives every module and no entry has to own anything.
constexpr ValueType kOneString[] = { ValueType::String };
constexpr ValueType kOneInt[] = { ValueType::Int };
constexpr ValueType kOneFloat[] = { ValueType::Float };
constexpr ValueType kOneBool[] = { ValueType::Bool };

} // namespace

const NativeFunctionSignature* NativeFunctionTable()
{
    // Ordered to match NativeFunctionId exactly -- the index is the
    // identity, both for the compiler binding a call and for the
    // interpreter dispatching one. A null parameter list means the
    // built-in takes nothing at all.
    static const NativeFunctionSignature table[kNativeFunctionCount] = {
        { "Console.WriteLine", kOneString, 1, ValueType::Void },
        { "Console.WriteLine", kOneInt, 1, ValueType::Void },
        { "Console.WriteLine", kOneFloat, 1, ValueType::Void },
        { "Console.WriteLine", kOneBool, 1, ValueType::Void },

        { "Console.Write", kOneString, 1, ValueType::Void },
        { "Console.Write", kOneInt, 1, ValueType::Void },
        { "Console.Write", kOneFloat, 1, ValueType::Void },
        { "Console.Write", kOneBool, 1, ValueType::Void },

        { "Debug.Log", kOneString, 1, ValueType::Void },
        { "Debug.LogWarning", kOneString, 1, ValueType::Void },
        { "Debug.LogError", kOneString, 1, ValueType::Void },

        { "Gc.Collect", nullptr, 0, ValueType::Void },
        { "Gc.LiveObjects", nullptr, 0, ValueType::Int },

        { "Math.Sqrt", kOneFloat, 1, ValueType::Float },
        { "Math.Sin", kOneFloat, 1, ValueType::Float },
        { "Math.Cos", kOneFloat, 1, ValueType::Float },
        { "Math.Floor", kOneFloat, 1, ValueType::Float },
        { "Math.Ceil", kOneFloat, 1, ValueType::Float },
    };
    return table;
}

} // namespace Fluxion::Script
