#include <Fluxion/Script/Runtime/ModuleSerializer.hpp>

#include <bit>
#include <string>
#include <vector>

namespace Fluxion::Script
{

namespace
{

// The image is written a byte at a time, least significant first, rather
// than by copying the host's own representation of a number. A module
// written on one machine is meant to be readable on another, and a raw
// copy would carry that machine's byte order and its padding along with
// the value.

// The lowest number of bytes one entry of each list can possibly occupy.
// A count read out of the image is checked against these before anything
// is reserved for it, so a length of four thousand million is refused
// against the size of the file rather than attempted.
constexpr u32 kMinBytesInstruction = 6;
constexpr u32 kMinBytesU32 = 4;
constexpr u32 kMinBytesI32 = 4;
constexpr u32 kMinBytesF32 = 4;
constexpr u32 kMinBytesString = 4;
constexpr u32 kMinBytesValueType = 1;
constexpr u32 kMinBytesClass = 32;
constexpr u32 kMinBytesFunction = 32;
constexpr u32 kMinBytesBoundCall = 8;
constexpr u32 kMinBytesAttribute = 8;
constexpr u32 kMinBytesAttributeArgument = 8;
constexpr u32 kMinBytesField = 8;
constexpr u32 kMinBytesInterface = 8;

class ImageWriter
{
public:
    explicit ImageWriter(std::vector<u8>& target) : m_bytes(target) {}

    bool Ok() const { return m_ok; }

    void U8(u8 value) { m_bytes.push_back(value); }
    void Bool(bool value) { U8(value ? 1u : 0u); }

    void U16(u16 value)
    {
        U8((u8)(value & 0xFFu));
        U8((u8)((value >> 8) & 0xFFu));
    }

    void U32(u32 value)
    {
        U8((u8)(value & 0xFFu));
        U8((u8)((value >> 8) & 0xFFu));
        U8((u8)((value >> 16) & 0xFFu));
        U8((u8)((value >> 24) & 0xFFu));
    }

    void I32(i32 value) { U32((u32)value); }
    void F32(f32 value) { U32(std::bit_cast<u32>(value)); }
    void Type(ValueType value) { U8((u8)value); }

    // Every list in the image is a count followed by that many entries.
    // The count is what a reader checks the rest of the file against, so
    // a list too long to name is refused here rather than written out
    // wrapped around.
    bool Count(size_t value)
    {
        if (value > 0xFFFFFFFFull)
        {
            m_ok = false;
            return false;
        }
        U32((u32)value);
        return true;
    }

    bool Text(const std::string& value)
    {
        if (!Count(value.size())) return false;
        for (char character : value) U8((u8)character);
        return true;
    }

private:
    std::vector<u8>& m_bytes;
    bool m_ok = true;
};

class ImageReader
{
public:
    ImageReader(const u8* bytes, size_t byteCount) : m_bytes(bytes), m_size(byteCount) {}

    bool Ok() const { return m_ok; }
    size_t Remaining() const { return m_ok ? m_size - m_cursor : 0; }
    bool AtEnd() const { return m_ok && m_cursor == m_size; }

    // Every read goes through here, so running off the end is recorded
    // once and every read afterwards answers zero rather than reading
    // whatever happens to follow the buffer.
    u8 U8()
    {
        if (!m_ok || m_cursor >= m_size)
        {
            m_ok = false;
            return 0;
        }
        return m_bytes[m_cursor++];
    }

    bool Bool() { return U8() != 0u; }

    u16 U16()
    {
        const u16 low = U8();
        const u16 high = U8();
        return (u16)(low | (high << 8));
    }

    u32 U32()
    {
        const u32 b0 = U8();
        const u32 b1 = U8();
        const u32 b2 = U8();
        const u32 b3 = U8();
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }

    i32 I32() { return (i32)U32(); }
    f32 F32() { return std::bit_cast<f32>(U32()); }

    // A count is only accepted when the bytes it claims could actually be
    // there. `minBytesPerEntry` is the smallest an entry of that list can
    // be, so this is a lower bound rather than an exact check -- but it is
    // enough to keep a corrupt length from being reserved for.
    bool Count(u32 minBytesPerEntry, u32& outCount)
    {
        const u32 count = U32();
        if (!m_ok) return false;
        if ((u64)count * (u64)minBytesPerEntry > (u64)Remaining())
        {
            m_ok = false;
            return false;
        }
        outCount = count;
        return true;
    }

    bool Text(std::string& outText)
    {
        u32 length = 0;
        if (!Count(1, length)) return false;

        outText.resize(length);
        for (u32 i = 0; i < length; ++i) outText[i] = (char)U8();
        return m_ok;
    }

    ValueType Type()
    {
        const u8 raw = U8();
        if (raw > (u8)ValueType::Unknown)
        {
            m_ok = false;
            return ValueType::Void;
        }
        return (ValueType)raw;
    }

    void Fail() { m_ok = false; }

private:
    const u8* m_bytes = nullptr;
    size_t m_size = 0;
    size_t m_cursor = 0;
    bool m_ok = true;
};

// --- Writing ------------------------------------------------------------

bool WriteAttributes(ImageWriter& writer, const std::vector<Attribute>& attributes)
{
    if (!writer.Count(attributes.size())) return false;
    for (const Attribute& attribute : attributes)
    {
        if (!writer.Text(attribute.name)) return false;
        if (!writer.Count(attribute.arguments.size())) return false;

        for (const AttributeArgument& argument : attribute.arguments)
        {
            writer.U8((u8)argument.kind);
            writer.I32(argument.intValue);
            writer.F32(argument.floatValue);
            if (!writer.Text(argument.stringValue)) return false;
            writer.U32(argument.classIndex);
        }
    }
    return true;
}

bool WriteTypeList(ImageWriter& writer, const std::vector<ValueType>& types)
{
    if (!writer.Count(types.size())) return false;
    for (ValueType type : types) writer.Type(type);
    return true;
}

bool WriteU32List(ImageWriter& writer, const std::vector<u32>& values)
{
    if (!writer.Count(values.size())) return false;
    for (u32 value : values) writer.U32(value);
    return true;
}

// --- Reading ------------------------------------------------------------

bool ReadAttributes(ImageReader& reader, std::vector<Attribute>& outAttributes)
{
    u32 count = 0;
    if (!reader.Count(kMinBytesAttribute, count)) return false;

    outAttributes.reserve(count);
    for (u32 i = 0; i < count; ++i)
    {
        Attribute attribute;
        if (!reader.Text(attribute.name)) return false;

        u32 argumentCount = 0;
        if (!reader.Count(kMinBytesAttributeArgument, argumentCount)) return false;

        attribute.arguments.reserve(argumentCount);
        for (u32 a = 0; a < argumentCount; ++a)
        {
            AttributeArgument argument;

            const u8 kind = reader.U8();
            if (kind > (u8)AttributeArgumentKind::Class)
            {
                reader.Fail();
                return false;
            }
            argument.kind = (AttributeArgumentKind)kind;
            argument.intValue = reader.I32();
            argument.floatValue = reader.F32();
            if (!reader.Text(argument.stringValue)) return false;
            argument.classIndex = reader.U32();

            attribute.arguments.push_back(std::move(argument));
        }
        outAttributes.push_back(std::move(attribute));
    }
    return reader.Ok();
}

bool ReadTypeList(ImageReader& reader, std::vector<ValueType>& outTypes)
{
    u32 count = 0;
    if (!reader.Count(kMinBytesValueType, count)) return false;

    outTypes.reserve(count);
    for (u32 i = 0; i < count; ++i) outTypes.push_back(reader.Type());
    return reader.Ok();
}

bool ReadU32List(ImageReader& reader, std::vector<u32>& outValues)
{
    u32 count = 0;
    if (!reader.Count(kMinBytesU32, count)) return false;

    outValues.reserve(count);
    for (u32 i = 0; i < count; ++i) outValues.push_back(reader.U32());
    return reader.Ok();
}

// --- What the image has to agree with itself about ----------------------

// Reading an image through without running out of bytes says only that it
// was the right length. These are the questions it also has to answer
// before anything is allowed to run on it: an index that names nothing, a
// function whose instructions lie outside the code that arrived, an opcode
// this build does not have -- each would otherwise be discovered by the
// interpreter partway through a frame, which is far too late and says far
// less.

bool IndexIsValid(u32 index, size_t limit, u32 noneValue) { return index == noneValue || (size_t)index < limit; }

bool CheckConsistency(const BytecodeModule& module, const std::string& sourceName, DiagnosticList& outDiagnostics)
{
    const SourceLocation location{ sourceName, 0, 0 };
    const size_t classCount = module.classes.size();
    const size_t functionCount = module.functions.size();

    if (module.sourceLines.size() != module.code.size())
    {
        outDiagnostics.AddError(location, "this script module says a different number of instructions and source lines");
        return false;
    }

    for (const Instruction& instruction : module.code)
    {
        if ((u16)instruction.op <= (u16)OpCode::Halt) continue;
        outDiagnostics.AddError(location, "this script module holds an instruction this build does not have");
        return false;
    }

    for (const ClassInfo& classInfo : module.classes)
    {
        const bool linksAreValid = IndexIsValid(classInfo.baseClass, classCount, kNoClass) &&
                                   IndexIsValid(classInfo.constructorFunction, functionCount, kNoFunction);
        if (!linksAreValid)
        {
            outDiagnostics.AddError(location, "'" + classInfo.name + "' in this script module names a class or a method that is not in it");
            return false;
        }

        for (u32 entry : classInfo.vtable)
        {
            if (IndexIsValid(entry, functionCount, kNoFunction)) continue;
            outDiagnostics.AddError(location, "'" + classInfo.name + "' in this script module dispatches to a method that is not in it");
            return false;
        }

        for (const InterfaceImplementation& implementation : classInfo.interfaces)
        {
            bool valid = IndexIsValid(implementation.interfaceClass, classCount, kNoClass);
            for (u32 entry : implementation.methods)
                valid = valid && IndexIsValid(entry, functionCount, kNoFunction);
            if (valid) continue;

            outDiagnostics.AddError(location, "'" + classInfo.name + "' in this script module satisfies an interface that is not in it");
            return false;
        }

        for (const FieldInfo& field : classInfo.fields)
        {
            // A field holding an engine handle names one of the host's
            // types rather than one of the module's, and the module
            // carries no list of those -- what that number means is
            // settled when the module is loaded against a table, not
            // here.
            const bool namesOwnClass = field.type == ValueType::Object || field.type == ValueType::Struct ||
                                       field.type == ValueType::Enum;
            if (!namesOwnClass || IndexIsValid(field.typeClass, classCount, kNoClass)) continue;

            outDiagnostics.AddError(location, "'" + classInfo.name + "." + field.name + "' in this script module has a type that is not in it");
            return false;
        }
    }

    for (const FunctionInfo& function : module.functions)
    {
        const bool linksAreValid = IndexIsValid(function.owningClass, classCount, kNoClass);
        if (!linksAreValid)
        {
            outDiagnostics.AddError(location, "'" + function.qualifiedName + "' in this script module belongs to a class that is not in it");
            return false;
        }

        // A method that only names a signature carries no instructions,
        // but it is still written down with a position, and that position
        // is held to the same rule as any other. The machine refuses to
        // enter a bodyless method at every door it has, so a nonsensical
        // position here cannot be reached -- which is exactly why it is
        // worth refusing at the door instead: whether it can be reached
        // then stops being something a reader has to work out.
        const u64 end = (u64)function.codeOffset + (u64)function.codeLength;
        if (end > (u64)module.code.size())
        {
            outDiagnostics.AddError(location, "'" + function.qualifiedName + "' in this script module runs past the end of its own instructions");
            return false;
        }
    }

    // An annotation written as `typeof(Name)` carries the class the name
    // was resolved to, so it is an index into this same module and has to
    // name something in it.
    for (const ClassInfo& classInfo : module.classes)
    {
        auto attributesAreValid = [&](const std::vector<Attribute>& attributes) {
            for (const Attribute& attribute : attributes)
            {
                for (const AttributeArgument& argument : attribute.arguments)
                {
                    if (argument.kind != AttributeArgumentKind::Class) continue;
                    if (IndexIsValid(argument.classIndex, classCount, kNoClass)) continue;
                    return false;
                }
            }
            return true;
        };

        bool valid = attributesAreValid(classInfo.attributes);
        for (const FieldInfo& field : classInfo.fields) valid = valid && attributesAreValid(field.attributes);
        if (valid) continue;

        outDiagnostics.AddError(location, "'" + classInfo.name + "' in this script module was annotated with a type that is not in it");
        return false;
    }

    return true;
}

} // namespace

bool WriteModule(const BytecodeModule& module, std::vector<u8>& outBytes)
{
    outBytes.clear();
    ImageWriter writer(outBytes);

    for (u8 byte : module.header.magic) writer.U8(byte);
    writer.U32(module.header.languageVersion);
    writer.U32(module.header.bytecodeVersion);
    writer.U32(module.header.engineAbiVersion);
    writer.U32(module.header.moduleVersion);

    if (!writer.Text(module.sourceName)) return false;

    if (!writer.Count(module.code.size())) return false;
    for (const Instruction& instruction : module.code)
    {
        writer.U16((u16)instruction.op);
        writer.U32(instruction.operand);
    }

    if (!writer.Count(module.sourceLines.size())) return false;
    for (u32 line : module.sourceLines) writer.U32(line);

    if (!writer.Count(module.intConstants.size())) return false;
    for (i32 value : module.intConstants) writer.I32(value);

    if (!writer.Count(module.floatConstants.size())) return false;
    for (f32 value : module.floatConstants) writer.F32(value);

    if (!writer.Count(module.stringConstants.size())) return false;
    for (const std::string& value : module.stringConstants)
    {
        if (!writer.Text(value)) return false;
    }

    if (!writer.Count(module.classes.size())) return false;
    for (const ClassInfo& classInfo : module.classes)
    {
        if (!writer.Text(classInfo.name)) return false;
        writer.U32(classInfo.baseClass);
        writer.Bool(classInfo.isInterface);
        writer.Bool(classInfo.isStatic);
        writer.Bool(classInfo.isStruct);
        writer.Bool(classInfo.isEnum);
        writer.Bool(classInfo.isArray);
        writer.Bool(classInfo.elementIsReference);
        writer.U32(classInfo.elementSlotCount);
        writer.U32(classInfo.fieldSlotCount);
        if (!WriteU32List(writer, classInfo.fieldReferenceBits)) return false;
        if (!WriteU32List(writer, classInfo.vtable)) return false;

        if (!writer.Count(classInfo.interfaces.size())) return false;
        for (const InterfaceImplementation& implementation : classInfo.interfaces)
        {
            writer.U32(implementation.interfaceClass);
            if (!WriteU32List(writer, implementation.methods)) return false;
        }

        writer.U32(classInfo.constructorFunction);
        if (!WriteAttributes(writer, classInfo.attributes)) return false;

        if (!writer.Count(classInfo.fields.size())) return false;
        for (const FieldInfo& field : classInfo.fields)
        {
            if (!writer.Text(field.name)) return false;
            writer.Type(field.type);
            writer.U32(field.typeClass);
            writer.U32(field.slot);
            if (!WriteAttributes(writer, field.attributes)) return false;
        }
    }

    if (!writer.Count(module.functions.size())) return false;
    for (const FunctionInfo& function : module.functions)
    {
        if (!writer.Text(function.qualifiedName)) return false;
        if (!writer.Text(function.sourceFile)) return false;
        writer.Type(function.returnType);
        if (!WriteTypeList(writer, function.parameterTypes)) return false;
        writer.U32(function.returnSlotCount);
        writer.U32(function.parameterSlotCount);
        writer.U32(function.receiverSlotCount);
        writer.Bool(function.receiverIsValue);
        writer.U32(function.localSlotCount);
        writer.U32(function.codeOffset);
        writer.U32(function.codeLength);
        writer.Bool(function.hasBody);
        writer.U32(function.owningClass);
        writer.U32(function.vtableSlot);
        writer.Bool(function.isInstance);
        if (!WriteU32List(writer, function.localReferenceBits)) return false;
    }

    if (!writer.Count(module.boundCalls.size())) return false;
    for (const BoundCallSite& site : module.boundCalls)
    {
        if (!writer.Text(site.typeName)) return false;
        if (!writer.Text(site.methodName)) return false;
        writer.Bool(site.isInstance);
        if (!WriteTypeList(writer, site.parameterTypes)) return false;
        writer.Type(site.returnType);
    }

    return writer.Ok();
}

bool ReadModule(const u8* bytes, size_t byteCount, BytecodeModule& outModule, DiagnosticList& outDiagnostics)
{
    // Nothing is known about where this came from yet, so a message about
    // it can only name the image itself. Once the module's own source name
    // has been read, the checks below use that instead.
    const SourceLocation unknown{ "<script module image>", 0, 0 };

    if (!bytes || byteCount == 0)
    {
        outDiagnostics.AddError(unknown, "there are no bytes here to read a script module out of");
        return false;
    }

    ImageReader reader(bytes, byteCount);
    BytecodeModule module;

    for (int i = 0; i < 4; ++i) module.header.magic[i] = reader.U8();
    if (!reader.Ok())
    {
        outDiagnostics.AddError(unknown, "this is too short to be a script module: it ends inside its own signature");
        return false;
    }
    for (int i = 0; i < 4; ++i)
    {
        if (module.header.magic[i] == kModuleMagic[i]) continue;
        outDiagnostics.AddError(unknown, "this is not a script module: the leading signature does not match");
        return false;
    }

    module.header.languageVersion = reader.U32();
    module.header.bytecodeVersion = reader.U32();
    module.header.engineAbiVersion = reader.U32();
    module.header.moduleVersion = reader.U32();
    if (!reader.Ok())
    {
        outDiagnostics.AddError(unknown, "this script module ends inside the versions it was built against");
        return false;
    }

    // Refused here rather than at load: everything past this point is laid
    // out the way this build's instruction set and engine interface say it
    // is, so reading on would be reading one layout as another.
    if (module.header.bytecodeVersion != kBytecodeVersion)
    {
        outDiagnostics.AddError(unknown, "this script module was built for bytecode version " +
                                             std::to_string(module.header.bytecodeVersion) + ", but this build understands version " +
                                             std::to_string(kBytecodeVersion));
        return false;
    }
    if (module.header.engineAbiVersion != kEngineAbiVersion)
    {
        outDiagnostics.AddError(unknown, "this script module was built against engine interface version " +
                                             std::to_string(module.header.engineAbiVersion) + ", but this build provides version " +
                                             std::to_string(kEngineAbiVersion));
        return false;
    }

    if (!reader.Text(module.sourceName))
    {
        outDiagnostics.AddError(unknown, "this script module ends inside the name of the source it was built from");
        return false;
    }

    const SourceLocation location{ module.sourceName.empty() ? unknown.file : module.sourceName, 0, 0 };

    u32 count = 0;

    if (!reader.Count(kMinBytesInstruction, count))
    {
        outDiagnostics.AddError(location, "this script module claims more instructions than it carries");
        return false;
    }
    module.code.reserve(count);
    for (u32 i = 0; i < count; ++i)
    {
        Instruction instruction;
        instruction.op = (OpCode)reader.U16();
        instruction.operand = reader.U32();
        module.code.push_back(instruction);
    }

    if (!reader.Count(kMinBytesU32, count))
    {
        outDiagnostics.AddError(location, "this script module claims more source lines than it carries");
        return false;
    }
    module.sourceLines.reserve(count);
    for (u32 i = 0; i < count; ++i) module.sourceLines.push_back(reader.U32());

    if (!reader.Count(kMinBytesI32, count))
    {
        outDiagnostics.AddError(location, "this script module claims more whole-number constants than it carries");
        return false;
    }
    module.intConstants.reserve(count);
    for (u32 i = 0; i < count; ++i) module.intConstants.push_back(reader.I32());

    if (!reader.Count(kMinBytesF32, count))
    {
        outDiagnostics.AddError(location, "this script module claims more fractional constants than it carries");
        return false;
    }
    module.floatConstants.reserve(count);
    for (u32 i = 0; i < count; ++i) module.floatConstants.push_back(reader.F32());

    if (!reader.Count(kMinBytesString, count))
    {
        outDiagnostics.AddError(location, "this script module claims more text constants than it carries");
        return false;
    }
    module.stringConstants.reserve(count);
    for (u32 i = 0; i < count; ++i)
    {
        std::string text;
        if (!reader.Text(text))
        {
            outDiagnostics.AddError(location, "this script module ends inside one of its text constants");
            return false;
        }
        module.stringConstants.push_back(std::move(text));
    }

    if (!reader.Count(kMinBytesClass, count))
    {
        outDiagnostics.AddError(location, "this script module claims more classes than it carries");
        return false;
    }
    module.classes.reserve(count);
    for (u32 i = 0; i < count; ++i)
    {
        ClassInfo classInfo;
        if (!reader.Text(classInfo.name)) break;

        classInfo.baseClass = reader.U32();
        classInfo.isInterface = reader.Bool();
        classInfo.isStatic = reader.Bool();
        classInfo.isStruct = reader.Bool();
        classInfo.isEnum = reader.Bool();
        classInfo.isArray = reader.Bool();
        classInfo.elementIsReference = reader.Bool();
        classInfo.elementSlotCount = reader.U32();
        classInfo.fieldSlotCount = reader.U32();
        if (!ReadU32List(reader, classInfo.fieldReferenceBits)) break;
        if (!ReadU32List(reader, classInfo.vtable)) break;

        u32 interfaceCount = 0;
        if (!reader.Count(kMinBytesInterface, interfaceCount)) break;
        classInfo.interfaces.reserve(interfaceCount);
        for (u32 k = 0; k < interfaceCount; ++k)
        {
            InterfaceImplementation implementation;
            implementation.interfaceClass = reader.U32();
            if (!ReadU32List(reader, implementation.methods)) break;
            classInfo.interfaces.push_back(std::move(implementation));
        }
        if (!reader.Ok()) break;

        classInfo.constructorFunction = reader.U32();
        if (!ReadAttributes(reader, classInfo.attributes)) break;

        u32 fieldCount = 0;
        if (!reader.Count(kMinBytesField, fieldCount)) break;
        classInfo.fields.reserve(fieldCount);
        for (u32 k = 0; k < fieldCount; ++k)
        {
            FieldInfo field;
            if (!reader.Text(field.name)) break;
            field.type = reader.Type();
            field.typeClass = reader.U32();
            field.slot = reader.U32();
            if (!ReadAttributes(reader, field.attributes)) break;
            classInfo.fields.push_back(std::move(field));
        }
        if (!reader.Ok()) break;

        module.classes.push_back(std::move(classInfo));
    }
    if (!reader.Ok() || module.classes.size() != count)
    {
        outDiagnostics.AddError(location, "this script module ends inside the description of one of its classes");
        return false;
    }

    if (!reader.Count(kMinBytesFunction, count))
    {
        outDiagnostics.AddError(location, "this script module claims more methods than it carries");
        return false;
    }
    module.functions.reserve(count);
    for (u32 i = 0; i < count; ++i)
    {
        FunctionInfo function;
        if (!reader.Text(function.qualifiedName)) break;
        if (!reader.Text(function.sourceFile)) break;
        function.returnType = reader.Type();
        if (!ReadTypeList(reader, function.parameterTypes)) break;
        function.returnSlotCount = reader.U32();
        function.parameterSlotCount = reader.U32();
        function.receiverSlotCount = reader.U32();
        function.receiverIsValue = reader.Bool();
        function.localSlotCount = reader.U32();
        function.codeOffset = reader.U32();
        function.codeLength = reader.U32();
        function.hasBody = reader.Bool();
        function.owningClass = reader.U32();
        function.vtableSlot = reader.U32();
        function.isInstance = reader.Bool();
        if (!ReadU32List(reader, function.localReferenceBits)) break;

        module.functions.push_back(std::move(function));
    }
    if (!reader.Ok() || module.functions.size() != count)
    {
        outDiagnostics.AddError(location, "this script module ends inside the description of one of its methods");
        return false;
    }

    if (!reader.Count(kMinBytesBoundCall, count))
    {
        outDiagnostics.AddError(location, "this script module claims more calls into the engine than it carries");
        return false;
    }
    module.boundCalls.reserve(count);
    for (u32 i = 0; i < count; ++i)
    {
        BoundCallSite site;
        if (!reader.Text(site.typeName)) break;
        if (!reader.Text(site.methodName)) break;
        site.isInstance = reader.Bool();
        if (!ReadTypeList(reader, site.parameterTypes)) break;
        site.returnType = reader.Type();

        module.boundCalls.push_back(std::move(site));
    }
    if (!reader.Ok() || module.boundCalls.size() != count)
    {
        outDiagnostics.AddError(location, "this script module ends inside one of its calls into the engine");
        return false;
    }

    // Anything after the last thing the image said it holds means this is
    // not the image it claims to be, whatever else was readable about it.
    if (!reader.AtEnd())
    {
        outDiagnostics.AddError(location, "there are bytes after the end of this script module");
        return false;
    }

    if (!CheckConsistency(module, location.file, outDiagnostics)) return false;

    outModule = std::move(module);
    return true;
}

} // namespace Fluxion::Script
