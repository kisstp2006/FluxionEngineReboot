#include <Fluxion/Script/Runtime/Vm.hpp>

#include <Runtime/ObjectHeap.hpp>

#include <cmath>
#include <cstdio>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace Fluxion::Script
{

namespace
{

// Deep enough for ordinary recursive code, shallow enough that runaway
// recursion is reported instead of exhausting the host's own stack.
constexpr size_t kMaxCallDepth = 512;

// An upper bound on the value stack so a mis-emitted or hand-edited
// module cannot grow it without limit.
constexpr size_t kMaxStackSlots = 1u << 16;

// How many objects may be created between collections before one is
// asked for. Low enough that a loop allocating in a tight cycle is kept
// in check, high enough that a handful of short-lived objects costs
// nothing.
constexpr u32 kAllocationsBetweenCollections = 128;

// An upper bound on how many elements one sequence may hold, so a length
// computed from bad arithmetic is reported rather than turned into an
// allocation that exhausts the host.
constexpr u32 kMaxArrayElements = 1u << 24;

std::string FormatInt(i32 value) { return std::to_string(value); }

std::string FormatFloat(f32 value)
{
    // Shortest readable form: a whole number prints without a trailing
    // decimal point, and trailing zeroes are dropped.
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", (double)value);
    return std::string(buffer);
}

std::string FormatBool(bool value) { return value ? "true" : "false"; }

void DefaultOutputHandler(void* user, OutputChannel channel, const char* text)
{
    (void)user;
    std::FILE* stream = (channel == OutputChannel::LogWarning || channel == OutputChannel::LogError) ? stderr : stdout;
    std::fputs(text, stream);
}

struct Frame
{
    u32 functionIndex = 0;

    // Instruction index within the function's own code range, so a
    // function's jump targets do not depend on where it landed in the
    // module's flat stream.
    u32 ip = 0;

    // Index of this frame's slot zero in the value stack. The receiver,
    // parameters and locals live there; the operand stack grows above
    // them.
    u32 localBase = 0;
};

} // namespace

struct Vm
{
    BytecodeModule module;

    OutputHandler outputHandler = nullptr;
    void* outputUser = nullptr;

    // Strings are immutable and interned: identical text is stored once,
    // and a value slot carries only the index. The table only ever grows
    // -- entries produced by concatenation stay alive until the machine
    // is destroyed, at which point the whole table goes away together.
    std::vector<std::string> strings;
    std::unordered_map<std::string, u32> stringIds;

    // Interned identity of each of the module's string constants, so a
    // constant push is a plain lookup.
    std::vector<u32> constantStringIds;

    std::vector<Slot> stack;
    std::vector<Frame> frames;

    ObjectHeap heap;

    // Set when something asked for a collection -- an allocation crossing
    // the threshold, the script, or the host while a script is running.
    // Honoured at the next point the whole value stack is quiet.
    bool collectPending = false;

    // Objects native code is holding onto. Listed one entry per pin, so
    // two holders releasing independently do not surprise each other.
    std::vector<ObjectHandle> pinnedRoots;
};

namespace
{

u32 Intern(Vm& vm, const std::string& text)
{
    auto found = vm.stringIds.find(text);
    if (found != vm.stringIds.end()) return found->second;

    const u32 id = (u32)vm.strings.size();
    vm.strings.push_back(text);
    vm.stringIds.emplace(text, id);
    return id;
}

const std::string& StringAt(const Vm& vm, u32 id)
{
    static const std::string empty;
    return id < vm.strings.size() ? vm.strings[id] : empty;
}

void Write(Vm& vm, OutputChannel channel, const std::string& text)
{
    if (vm.outputHandler) vm.outputHandler(vm.outputUser, channel, text.c_str());
    else DefaultOutputHandler(nullptr, channel, text.c_str());
}

// True when no frame has anything on its operand stack, which is the case
// exactly when every active call is sitting between two statements. Only
// then are a frame's declared slots the whole truth about which
// references it is holding, and only then may a collection run.
bool StackIsQuiet(const Vm& vm)
{
    size_t expected = 0;
    for (const Frame& frame : vm.frames)
    {
        if (frame.functionIndex >= vm.module.functions.size()) return false;
        if ((size_t)frame.localBase != expected) return false;
        expected = (size_t)frame.localBase + vm.module.functions[frame.functionIndex].localSlotCount;
    }
    return expected == vm.stack.size();
}

void Collect(Vm& vm)
{
    vm.heap.BeginCollection();

    // The roots of an active call are the slots its function declared as
    // references, and nothing else: the emitter recorded them, so no
    // guessing is involved and no live value is ever mistaken for one.
    for (const Frame& frame : vm.frames)
    {
        const FunctionInfo& function = vm.module.functions[frame.functionIndex];
        for (u32 slot = 0; slot < function.localSlotCount; ++slot)
        {
            if (!IsReferenceBitSet(function.localReferenceBits, slot)) continue;

            const size_t index = (size_t)frame.localBase + slot;
            if (index >= vm.stack.size()) break;
            vm.heap.Mark(SlotAsObject(vm.stack[index]), vm.module.classes);
        }
    }

    for (ObjectHandle pinned : vm.pinnedRoots)
        vm.heap.Mark(pinned, vm.module.classes);

    vm.heap.Sweep();
    vm.collectPending = false;
}

// Fault codes reported through the Result returned by Invoke. The
// message that travels with them is a static string; the code is what
// distinguishes the cases.
constexpr i32 kFaultCodeMalformed = 10;
constexpr i32 kFaultCodeStackUnderflow = 11;
constexpr i32 kFaultCodeStackOverflow = 12;
constexpr i32 kFaultCodeCallDepth = 13;
constexpr i32 kFaultCodeDivideByZero = 14;
constexpr i32 kFaultCodeNullReference = 15;
constexpr i32 kFaultCodeStaleReference = 16;
constexpr i32 kFaultCodeArrayLength = 17;
constexpr i32 kFaultCodeElementRange = 18;

class Interpreter
{
public:
    explicit Interpreter(Vm& vm) : m_vm(vm) {}

    bool Faulted() const { return m_faulted; }
    i32 FaultCode() const { return m_faultCode; }
    const char* FaultMessage() const { return m_faultMessage; }
    Slot ReturnSlot() const { return m_returnSlot; }

    void Run()
    {
        while (!m_faulted && !m_vm.frames.empty())
            Step();
    }

private:
    Vm& m_vm;
    bool m_faulted = false;
    i32 m_faultCode = 0;
    const char* m_faultMessage = "script runtime fault";
    Slot m_returnSlot;

    void Fault(i32 code, const char* message)
    {
        if (m_faulted) return;
        m_faulted = true;
        m_faultCode = code;
        m_faultMessage = message;
    }

    Slot Pop()
    {
        if (m_vm.stack.empty())
        {
            Fault(kFaultCodeStackUnderflow, "script value stack underflowed");
            return Slot{};
        }
        Slot slot = m_vm.stack.back();
        m_vm.stack.pop_back();
        return slot;
    }

    void Push(Slot slot)
    {
        if (m_vm.stack.size() >= kMaxStackSlots)
        {
            Fault(kFaultCodeStackOverflow, "script value stack overflowed");
            return;
        }
        m_vm.stack.push_back(slot);
    }

    void PushInt(i32 value) { Push(MakeIntSlot(value)); }
    void PushFloat(f32 value) { Push(MakeFloatSlot(value)); }
    void PushBool(bool value) { Push(MakeBoolSlot(value)); }
    void PushText(const std::string& text) { Push(MakeStringSlot(Intern(m_vm, text))); }

    // Turns a slot into the object it names, or faults saying why it
    // cannot: the language has no way to express an address, so the only
    // two failures are a reference that was never given an object and one
    // whose object has since been reclaimed.
    bool RequireObject(Slot slot, ObjectHandle& outHandle)
    {
        if (SlotIsNull(slot))
        {
            Fault(kFaultCodeNullReference, "script reached through a reference that holds null");
            return false;
        }
        outHandle = SlotAsObject(slot);
        if (!m_vm.heap.IsAlive(outHandle))
        {
            Fault(kFaultCodeStaleReference, "script reached through a reference whose object no longer exists");
            return false;
        }
        return true;
    }

    void Step()
    {
        Frame& frame = m_vm.frames.back();
        const FunctionInfo& function = m_vm.module.functions[frame.functionIndex];

        if (frame.ip >= function.codeLength)
        {
            Fault(kFaultCodeMalformed, "script execution ran past the end of a method");
            return;
        }

        const Instruction instruction = m_vm.module.code[function.codeOffset + frame.ip];
        ++frame.ip;
        Execute(instruction);
    }

    void Execute(Instruction instruction)
    {
        switch (instruction.op)
        {
            case OpCode::Nop: break;

            case OpCode::PushInt:
                if (instruction.operand >= m_vm.module.intConstants.size()) { Fault(kFaultCodeMalformed, "script module references a missing int constant"); break; }
                PushInt(m_vm.module.intConstants[instruction.operand]);
                break;

            case OpCode::PushFloat:
                if (instruction.operand >= m_vm.module.floatConstants.size()) { Fault(kFaultCodeMalformed, "script module references a missing float constant"); break; }
                PushFloat(m_vm.module.floatConstants[instruction.operand]);
                break;

            case OpCode::PushBool:
                PushBool(instruction.operand != 0);
                break;

            case OpCode::PushString:
                if (instruction.operand >= m_vm.constantStringIds.size()) { Fault(kFaultCodeMalformed, "script module references a missing string constant"); break; }
                Push(MakeStringSlot(m_vm.constantStringIds[instruction.operand]));
                break;

            case OpCode::PushNull: Push(MakeNullSlot()); break;

            case OpCode::LoadLocal: {
                const size_t index = (size_t)m_vm.frames.back().localBase + instruction.operand;
                if (index >= m_vm.stack.size()) { Fault(kFaultCodeMalformed, "script method read a local outside its frame"); break; }
                Push(m_vm.stack[index]);
                break;
            }

            case OpCode::StoreLocal: {
                const size_t index = (size_t)m_vm.frames.back().localBase + instruction.operand;
                const Slot value = Pop();
                if (index >= m_vm.stack.size()) { Fault(kFaultCodeMalformed, "script method wrote a local outside its frame"); break; }
                m_vm.stack[index] = value;
                break;
            }

            case OpCode::Pop: Pop(); break;

            case OpCode::Dup: {
                if (m_vm.stack.empty()) { Fault(kFaultCodeStackUnderflow, "script value stack underflowed"); break; }
                Push(m_vm.stack.back());
                break;
            }

            case OpCode::Dup2: {
                if (m_vm.stack.size() < 2) { Fault(kFaultCodeStackUnderflow, "script value stack underflowed"); break; }
                // Read both out before pushing: a push may move the stack
                // out from under a reference into it.
                const Slot lower = m_vm.stack[m_vm.stack.size() - 2];
                const Slot upper = m_vm.stack[m_vm.stack.size() - 1];
                Push(lower);
                Push(upper);
                break;
            }

            case OpCode::AddInt: { const i32 b = SlotAsInt(Pop()); const i32 a = SlotAsInt(Pop()); PushInt((i32)((u32)a + (u32)b)); break; }
            case OpCode::SubInt: { const i32 b = SlotAsInt(Pop()); const i32 a = SlotAsInt(Pop()); PushInt((i32)((u32)a - (u32)b)); break; }
            case OpCode::MulInt: { const i32 b = SlotAsInt(Pop()); const i32 a = SlotAsInt(Pop()); PushInt((i32)((u32)a * (u32)b)); break; }

            case OpCode::DivInt: {
                const i32 b = SlotAsInt(Pop());
                const i32 a = SlotAsInt(Pop());
                if (b == 0) { Fault(kFaultCodeDivideByZero, "script divided an int by zero"); break; }
                // The most negative int has no positive counterpart, so
                // its division by -1 is wrapped explicitly rather than
                // left to the host.
                PushInt(b == -1 ? (i32)(0u - (u32)a) : a / b);
                break;
            }

            case OpCode::ModInt: {
                const i32 b = SlotAsInt(Pop());
                const i32 a = SlotAsInt(Pop());
                if (b == 0) { Fault(kFaultCodeDivideByZero, "script took an int remainder by zero"); break; }
                PushInt(b == -1 ? 0 : a % b);
                break;
            }

            case OpCode::NegInt: { const i32 a = SlotAsInt(Pop()); PushInt((i32)(0u - (u32)a)); break; }

            case OpCode::AddFloat: { const f32 b = SlotAsFloat(Pop()); const f32 a = SlotAsFloat(Pop()); PushFloat(a + b); break; }
            case OpCode::SubFloat: { const f32 b = SlotAsFloat(Pop()); const f32 a = SlotAsFloat(Pop()); PushFloat(a - b); break; }
            case OpCode::MulFloat: { const f32 b = SlotAsFloat(Pop()); const f32 a = SlotAsFloat(Pop()); PushFloat(a * b); break; }
            case OpCode::DivFloat: { const f32 b = SlotAsFloat(Pop()); const f32 a = SlotAsFloat(Pop()); PushFloat(a / b); break; }
            case OpCode::ModFloat: { const f32 b = SlotAsFloat(Pop()); const f32 a = SlotAsFloat(Pop()); PushFloat(std::fmod(a, b)); break; }
            case OpCode::NegFloat: { const f32 a = SlotAsFloat(Pop()); PushFloat(-a); break; }

            case OpCode::ConcatString: {
                const u32 b = SlotAsStringId(Pop());
                const u32 a = SlotAsStringId(Pop());
                PushText(StringAt(m_vm, a) + StringAt(m_vm, b));
                break;
            }

            case OpCode::EqualInt: { const i32 b = SlotAsInt(Pop()); const i32 a = SlotAsInt(Pop()); PushBool(a == b); break; }
            case OpCode::NotEqualInt: { const i32 b = SlotAsInt(Pop()); const i32 a = SlotAsInt(Pop()); PushBool(a != b); break; }
            case OpCode::LessInt: { const i32 b = SlotAsInt(Pop()); const i32 a = SlotAsInt(Pop()); PushBool(a < b); break; }
            case OpCode::GreaterInt: { const i32 b = SlotAsInt(Pop()); const i32 a = SlotAsInt(Pop()); PushBool(a > b); break; }
            case OpCode::LessEqualInt: { const i32 b = SlotAsInt(Pop()); const i32 a = SlotAsInt(Pop()); PushBool(a <= b); break; }
            case OpCode::GreaterEqualInt: { const i32 b = SlotAsInt(Pop()); const i32 a = SlotAsInt(Pop()); PushBool(a >= b); break; }

            case OpCode::EqualFloat: { const f32 b = SlotAsFloat(Pop()); const f32 a = SlotAsFloat(Pop()); PushBool(a == b); break; }
            case OpCode::NotEqualFloat: { const f32 b = SlotAsFloat(Pop()); const f32 a = SlotAsFloat(Pop()); PushBool(a != b); break; }
            case OpCode::LessFloat: { const f32 b = SlotAsFloat(Pop()); const f32 a = SlotAsFloat(Pop()); PushBool(a < b); break; }
            case OpCode::GreaterFloat: { const f32 b = SlotAsFloat(Pop()); const f32 a = SlotAsFloat(Pop()); PushBool(a > b); break; }
            case OpCode::LessEqualFloat: { const f32 b = SlotAsFloat(Pop()); const f32 a = SlotAsFloat(Pop()); PushBool(a <= b); break; }
            case OpCode::GreaterEqualFloat: { const f32 b = SlotAsFloat(Pop()); const f32 a = SlotAsFloat(Pop()); PushBool(a >= b); break; }

            case OpCode::EqualBool: { const bool b = SlotAsBool(Pop()); const bool a = SlotAsBool(Pop()); PushBool(a == b); break; }
            case OpCode::NotEqualBool: { const bool b = SlotAsBool(Pop()); const bool a = SlotAsBool(Pop()); PushBool(a != b); break; }

            case OpCode::EqualString: {
                const u32 b = SlotAsStringId(Pop());
                const u32 a = SlotAsStringId(Pop());
                PushBool(StringAt(m_vm, a) == StringAt(m_vm, b));
                break;
            }
            case OpCode::NotEqualString: {
                const u32 b = SlotAsStringId(Pop());
                const u32 a = SlotAsStringId(Pop());
                PushBool(StringAt(m_vm, a) != StringAt(m_vm, b));
                break;
            }

            // Identity, and nothing more: the whole handle is the
            // object's name, so comparing the bits is comparing which
            // object each side holds.
            case OpCode::EqualRef: { const Slot b = Pop(); const Slot a = Pop(); PushBool(a.bits == b.bits); break; }
            case OpCode::NotEqualRef: { const Slot b = Pop(); const Slot a = Pop(); PushBool(a.bits != b.bits); break; }

            case OpCode::NotBool: PushBool(!SlotAsBool(Pop())); break;

            case OpCode::IntToFloat: PushFloat((f32)SlotAsInt(Pop())); break;
            case OpCode::IntToString: PushText(FormatInt(SlotAsInt(Pop()))); break;
            case OpCode::FloatToString: PushText(FormatFloat(SlotAsFloat(Pop()))); break;
            case OpCode::BoolToString: PushText(FormatBool(SlotAsBool(Pop()))); break;

            case OpCode::NewObject: ExecuteNewObject(instruction.operand); break;

            case OpCode::LoadField: {
                ObjectHandle handle;
                if (!RequireObject(Pop(), handle)) break;

                Slot value;
                if (!m_vm.heap.ReadField(handle, instruction.operand, value))
                {
                    Fault(kFaultCodeMalformed, "script read a field the object does not have");
                    break;
                }
                Push(value);
                break;
            }

            case OpCode::StoreField: {
                const Slot value = Pop();
                ObjectHandle handle;
                if (!RequireObject(Pop(), handle)) break;

                if (!m_vm.heap.WriteField(handle, instruction.operand, value))
                {
                    Fault(kFaultCodeMalformed, "script wrote a field the object does not have");
                    break;
                }
                break;
            }

            case OpCode::NewArray: ExecuteNewArray(instruction.operand); break;

            case OpCode::ArrayLength: {
                ObjectHandle handle;
                if (!RequireObject(Pop(), handle)) break;

                u32 count = 0;
                if (!RequireSequence(handle, count)) break;
                PushInt((i32)count);
                break;
            }

            case OpCode::LoadElement: {
                const i32 position = SlotAsInt(Pop());
                ObjectHandle handle;
                if (!RequireObject(Pop(), handle)) break;

                u32 count = 0;
                if (!RequireSequence(handle, count)) break;
                if (!RequireInRange(position, count)) break;

                Slot value;
                if (!m_vm.heap.ReadField(handle, (u32)position, value))
                {
                    Fault(kFaultCodeMalformed, "script read a sequence element that does not exist");
                    break;
                }
                Push(value);
                break;
            }

            case OpCode::StoreElement: {
                const Slot value = Pop();
                const i32 position = SlotAsInt(Pop());
                ObjectHandle handle;
                if (!RequireObject(Pop(), handle)) break;

                u32 count = 0;
                if (!RequireSequence(handle, count)) break;
                if (!RequireInRange(position, count)) break;

                if (!m_vm.heap.WriteField(handle, (u32)position, value))
                {
                    Fault(kFaultCodeMalformed, "script wrote a sequence element that does not exist");
                    break;
                }
                break;
            }

            case OpCode::Jump: m_vm.frames.back().ip = instruction.operand; break;

            case OpCode::JumpIfFalse:
                if (!SlotAsBool(Pop())) m_vm.frames.back().ip = instruction.operand;
                break;

            case OpCode::JumpIfTrue:
                if (SlotAsBool(Pop())) m_vm.frames.back().ip = instruction.operand;
                break;

            case OpCode::Call: ExecuteCall(instruction.operand); break;
            case OpCode::CallVirtual: ExecuteVirtualCall(instruction.operand); break;
            case OpCode::CallInterface: ExecuteInterfaceCall(instruction.operand); break;
            case OpCode::CallNative: ExecuteNativeCall(instruction.operand); break;

            case OpCode::Return: ExecuteReturn(true); break;
            case OpCode::ReturnVoid: ExecuteReturn(false); break;

            case OpCode::SafePoint:
                if (m_vm.collectPending && StackIsQuiet(m_vm)) Collect(m_vm);
                break;

            case OpCode::Halt:
                Fault(kFaultCodeMalformed, "script execution reached a halt instruction");
                break;

            default:
                Fault(kFaultCodeMalformed, "script module contains an unrecognized instruction");
                break;
        }
    }

    void ExecuteNewObject(u32 classIndex)
    {
        if (classIndex >= m_vm.module.classes.size())
        {
            Fault(kFaultCodeMalformed, "script allocation names a class the module does not contain");
            return;
        }

        const ClassInfo& classInfo = m_vm.module.classes[classIndex];
        const ObjectHandle handle = m_vm.heap.Allocate(classIndex, classInfo.fieldSlotCount);
        if (handle.IsNull())
        {
            Fault(kFaultCodeMalformed, "script could not create an object");
            return;
        }

        // Allocating never collects: this is the middle of an
        // expression, where the stack still holds values only that
        // expression knows the shape of. The request is recorded and the
        // next quiet point acts on it.
        if (m_vm.heap.AllocationsSinceCollection() >= kAllocationsBetweenCollections)
            m_vm.collectPending = true;

        Push(MakeObjectSlot(handle));
    }

    void ExecuteNewArray(u32 classIndex)
    {
        const i32 requested = SlotAsInt(Pop());

        if (classIndex >= m_vm.module.classes.size() || !m_vm.module.classes[classIndex].isArray)
        {
            Fault(kFaultCodeMalformed, "script allocation names a sequence type the module does not contain");
            return;
        }

        // A count that came out negative is a mistake in the script, not
        // a request for a very large sequence: it is reported here rather
        // than wrapped around into one.
        if (requested < 0)
        {
            Fault(kFaultCodeArrayLength, "script asked for a sequence with a negative number of elements");
            return;
        }
        if ((u32)requested > kMaxArrayElements)
        {
            Fault(kFaultCodeArrayLength, "script asked for a sequence with more elements than one may hold");
            return;
        }

        const ObjectHandle handle = m_vm.heap.Allocate(classIndex, (u32)requested);
        if (handle.IsNull())
        {
            Fault(kFaultCodeMalformed, "script could not create a sequence");
            return;
        }

        if (m_vm.heap.AllocationsSinceCollection() >= kAllocationsBetweenCollections)
            m_vm.collectPending = true;

        Push(MakeObjectSlot(handle));
    }

    // The object is known to be live by the time this runs; what is left
    // to establish is that it is a sequence at all, and how long it is.
    bool RequireSequence(ObjectHandle handle, u32& outCount)
    {
        const u32 classIndex = m_vm.heap.ClassOf(handle);
        if (classIndex >= m_vm.module.classes.size() || !m_vm.module.classes[classIndex].isArray)
        {
            Fault(kFaultCodeMalformed, "script used something that is not a sequence as one");
            return false;
        }
        if (!m_vm.heap.TrySlotCount(handle, outCount))
        {
            Fault(kFaultCodeStaleReference, "script reached through a reference whose object no longer exists");
            return false;
        }
        return true;
    }

    bool RequireInRange(i32 position, u32 count)
    {
        if (position >= 0 && (u32)position < count) return true;
        Fault(kFaultCodeElementRange, "script reached a sequence element outside the range the sequence was created with");
        return false;
    }

    // How many stack slots a call to `function` consumes: its declared
    // parameters, plus the receiver when it has one.
    static size_t IncomingSlotCount(const FunctionInfo& function)
    {
        return function.parameterTypes.size() + (function.isInstance ? 1u : 0u);
    }

    // Reads back the receiver a call already pushed, without disturbing
    // anything above it.
    bool PeekReceiver(const FunctionInfo& declared, ObjectHandle& outHandle)
    {
        const size_t incoming = IncomingSlotCount(declared);
        if (m_vm.stack.size() < incoming)
        {
            Fault(kFaultCodeStackUnderflow, "script call is missing its arguments");
            return false;
        }
        return RequireObject(m_vm.stack[m_vm.stack.size() - incoming], outHandle);
    }

    void ExecuteVirtualCall(u32 declaredIndex)
    {
        if (declaredIndex >= m_vm.module.functions.size())
        {
            Fault(kFaultCodeMalformed, "script call names a method the module does not contain");
            return;
        }

        const FunctionInfo& declared = m_vm.module.functions[declaredIndex];
        ObjectHandle receiver;
        if (!PeekReceiver(declared, receiver)) return;

        const u32 classIndex = m_vm.heap.ClassOf(receiver);
        if (classIndex >= m_vm.module.classes.size() || declared.vtableSlot >= m_vm.module.classes[classIndex].vtable.size())
        {
            Fault(kFaultCodeMalformed, "script call found no implementation for a method on the object it was made on");
            return;
        }

        ExecuteCall(m_vm.module.classes[classIndex].vtable[declared.vtableSlot]);
    }

    void ExecuteInterfaceCall(u32 declaredIndex)
    {
        if (declaredIndex >= m_vm.module.functions.size())
        {
            Fault(kFaultCodeMalformed, "script call names a method the module does not contain");
            return;
        }

        const FunctionInfo& declared = m_vm.module.functions[declaredIndex];
        ObjectHandle receiver;
        if (!PeekReceiver(declared, receiver)) return;

        const u32 classIndex = m_vm.heap.ClassOf(receiver);
        if (classIndex >= m_vm.module.classes.size())
        {
            Fault(kFaultCodeMalformed, "script call was made on an object of an unknown class");
            return;
        }

        for (const InterfaceImplementation& implementation : m_vm.module.classes[classIndex].interfaces)
        {
            if (implementation.interfaceClass != declared.owningClass) continue;
            if (declared.vtableSlot >= implementation.methods.size()) break;
            ExecuteCall(implementation.methods[declared.vtableSlot]);
            return;
        }

        Fault(kFaultCodeMalformed, "script call found no implementation for an interface method on the object it was made on");
    }

    void ExecuteCall(u32 functionIndex)
    {
        if (functionIndex >= m_vm.module.functions.size())
        {
            Fault(kFaultCodeMalformed, "script call names a method the module does not contain");
            return;
        }

        const FunctionInfo& callee = m_vm.module.functions[functionIndex];
        if (!callee.hasBody)
        {
            Fault(kFaultCodeMalformed, "script call reached a method that has no body");
            return;
        }
        if (m_vm.frames.size() >= kMaxCallDepth)
        {
            Fault(kFaultCodeCallDepth, "script call nesting grew too deep");
            return;
        }

        const size_t incoming = IncomingSlotCount(callee);
        if (m_vm.stack.size() < incoming)
        {
            Fault(kFaultCodeStackUnderflow, "script call is missing its arguments");
            return;
        }

        if (callee.isInstance)
        {
            ObjectHandle receiver;
            if (!RequireObject(m_vm.stack[m_vm.stack.size() - incoming], receiver)) return;
        }

        // The receiver and arguments are already sitting on the stack in
        // order, so they simply become the new frame's lowest slots.
        Frame frame;
        frame.functionIndex = functionIndex;
        frame.ip = 0;
        frame.localBase = (u32)(m_vm.stack.size() - incoming);

        const size_t frameEnd = (size_t)frame.localBase + callee.localSlotCount;
        if (frameEnd > kMaxStackSlots)
        {
            Fault(kFaultCodeStackOverflow, "script value stack overflowed");
            return;
        }
        m_vm.stack.resize(frameEnd);
        m_vm.frames.push_back(frame);
    }

    void ExecuteReturn(bool withValue)
    {
        const Slot value = withValue ? Pop() : Slot{};
        const u32 base = m_vm.frames.back().localBase;
        m_vm.frames.pop_back();
        m_vm.stack.resize(base);

        if (m_vm.frames.empty())
        {
            m_returnSlot = value;
            return;
        }
        if (withValue) Push(value);
    }

    void ExecuteNativeCall(u32 nativeIndex)
    {
        if (nativeIndex >= kNativeFunctionCount)
        {
            Fault(kFaultCodeMalformed, "script call names a built-in that does not exist");
            return;
        }

        const auto id = (NativeFunctionId)nativeIndex;
        switch (id)
        {
            case NativeFunctionId::ConsoleWriteLineString: Write(m_vm, OutputChannel::Console, StringAt(m_vm, SlotAsStringId(Pop())) + "\n"); break;
            case NativeFunctionId::ConsoleWriteLineInt: Write(m_vm, OutputChannel::Console, FormatInt(SlotAsInt(Pop())) + "\n"); break;
            case NativeFunctionId::ConsoleWriteLineFloat: Write(m_vm, OutputChannel::Console, FormatFloat(SlotAsFloat(Pop())) + "\n"); break;
            case NativeFunctionId::ConsoleWriteLineBool: Write(m_vm, OutputChannel::Console, FormatBool(SlotAsBool(Pop())) + "\n"); break;

            case NativeFunctionId::ConsoleWriteString: Write(m_vm, OutputChannel::Console, StringAt(m_vm, SlotAsStringId(Pop()))); break;
            case NativeFunctionId::ConsoleWriteInt: Write(m_vm, OutputChannel::Console, FormatInt(SlotAsInt(Pop()))); break;
            case NativeFunctionId::ConsoleWriteFloat: Write(m_vm, OutputChannel::Console, FormatFloat(SlotAsFloat(Pop()))); break;
            case NativeFunctionId::ConsoleWriteBool: Write(m_vm, OutputChannel::Console, FormatBool(SlotAsBool(Pop()))); break;

            case NativeFunctionId::DebugLog: Write(m_vm, OutputChannel::LogInfo, StringAt(m_vm, SlotAsStringId(Pop())) + "\n"); break;
            case NativeFunctionId::DebugLogWarning: Write(m_vm, OutputChannel::LogWarning, StringAt(m_vm, SlotAsStringId(Pop())) + "\n"); break;
            case NativeFunctionId::DebugLogError: Write(m_vm, OutputChannel::LogError, StringAt(m_vm, SlotAsStringId(Pop())) + "\n"); break;

            // Asking is all a script can do: the collection itself waits
            // for the next point between two statements.
            case NativeFunctionId::GcCollect: m_vm.collectPending = true; break;
            case NativeFunctionId::GcLiveObjects: PushInt((i32)m_vm.heap.LiveObjects()); break;

            default:
                Fault(kFaultCodeMalformed, "script call names a built-in that does not exist");
                break;
        }
    }
};

bool ValidateHeader(const ModuleHeader& header, const std::string& sourceName, DiagnosticList& outDiagnostics)
{
    const SourceLocation location{ sourceName, 0, 0 };

    for (int i = 0; i < 4; ++i)
    {
        if (header.magic[i] == kModuleMagic[i]) continue;
        outDiagnostics.AddError(location, "this is not a script module: the leading signature does not match");
        return false;
    }

    if (header.bytecodeVersion != kBytecodeVersion)
    {
        outDiagnostics.AddError(location, "script module was built for bytecode version " + std::to_string(header.bytecodeVersion) +
                                              ", but this build understands version " + std::to_string(kBytecodeVersion));
        return false;
    }

    if (header.engineAbiVersion != kEngineAbiVersion)
    {
        outDiagnostics.AddError(location, "script module was built against engine interface version " + std::to_string(header.engineAbiVersion) +
                                              ", but this build provides version " + std::to_string(kEngineAbiVersion));
        return false;
    }

    // languageVersion describes the source the module was written in,
    // which no longer affects how the instructions run, so it is carried
    // for reporting rather than enforced here.
    return true;
}

ScriptValue ValueFromSlot(const Vm& vm, ValueType type, Slot slot)
{
    ScriptValue value;
    value.type = type;
    switch (type)
    {
        case ValueType::Int: value.intValue = SlotAsInt(slot); break;
        case ValueType::Float: value.floatValue = SlotAsFloat(slot); break;
        case ValueType::Bool: value.boolValue = SlotAsBool(slot); break;
        case ValueType::String: value.stringValue = StringAt(vm, SlotAsStringId(slot)); break;
        case ValueType::Object: value.objectValue = SlotAsObject(slot); break;
        default: break;
    }
    return value;
}

} // namespace

Vm* CreateVm(const BytecodeModule& module, DiagnosticList& outDiagnostics)
{
    if (!ValidateHeader(module.header, module.sourceName, outDiagnostics)) return nullptr;

    // The module is built without exceptions, so a failed allocation
    // comes back as a null pointer and is reported like any other load
    // failure.
    Vm* vm = new (std::nothrow) Vm();
    if (!vm)
    {
        outDiagnostics.AddError(SourceLocation{ module.sourceName, 0, 0 }, "could not allocate a script virtual machine");
        return nullptr;
    }
    vm->module = module;

    // Slot zero of the string table is the empty string, so a local that
    // was never assigned still reads as valid, empty text.
    Intern(*vm, std::string());

    vm->constantStringIds.reserve(vm->module.stringConstants.size());
    for (const std::string& constant : vm->module.stringConstants)
        vm->constantStringIds.push_back(Intern(*vm, constant));

    return vm;
}

void DestroyVm(Vm* vm)
{
    delete vm;
}

void SetOutputHandler(Vm* vm, OutputHandler handler, void* user)
{
    if (!vm) return;
    vm->outputHandler = handler;
    vm->outputUser = user;
}

Fluxion::Foundation::Result<ScriptValue> Invoke(Vm* vm, const char* qualifiedName)
{
    using ResultType = Fluxion::Foundation::Result<ScriptValue>;

    if (!vm) return ResultType::Error(1, "script virtual machine handle is null");
    if (!qualifiedName) return ResultType::Error(2, "script method name is null");

    size_t functionIndex = vm->module.functions.size();
    for (size_t i = 0; i < vm->module.functions.size(); ++i)
    {
        if (vm->module.functions[i].qualifiedName == qualifiedName) { functionIndex = i; break; }
    }
    if (functionIndex == vm->module.functions.size())
        return ResultType::Error(2, "the script module has no method with the requested name");

    const FunctionInfo& entry = vm->module.functions[functionIndex];
    if (!entry.parameterTypes.empty())
        return ResultType::Error(3, "the requested script method takes parameters and cannot be invoked this way");
    if (entry.isInstance)
        return ResultType::Error(4, "the requested script method belongs to an instance and cannot be invoked this way");
    if (!entry.hasBody)
        return ResultType::Error(5, "the requested script method has no body");

    vm->stack.clear();
    vm->frames.clear();
    vm->stack.resize(entry.localSlotCount);

    Frame frame;
    frame.functionIndex = (u32)functionIndex;
    frame.ip = 0;
    frame.localBase = 0;
    vm->frames.push_back(frame);

    Interpreter interpreter(*vm);
    interpreter.Run();

    if (interpreter.Faulted())
    {
        vm->stack.clear();
        vm->frames.clear();
        return ResultType::Error(interpreter.FaultCode(), interpreter.FaultMessage());
    }

    ScriptValue value = ValueFromSlot(*vm, entry.returnType, interpreter.ReturnSlot());
    vm->stack.clear();
    return ResultType::Ok(std::move(value));
}

HeapStats GetHeapStats(const Vm* vm)
{
    HeapStats stats;
    if (!vm) return stats;
    stats.liveObjects = vm->heap.LiveObjects();
    stats.totalAllocations = vm->heap.TotalAllocations();
    stats.collectionCount = vm->heap.CollectionCount();
    return stats;
}

void CollectGarbage(Vm* vm)
{
    if (!vm) return;
    if (StackIsQuiet(*vm))
    {
        Collect(*vm);
        return;
    }
    vm->collectPending = true;
}

bool IsObjectAlive(const Vm* vm, ObjectHandle handle)
{
    if (!vm) return false;
    return vm->heap.IsAlive(handle);
}

bool PinObject(Vm* vm, ObjectHandle handle)
{
    if (!vm || !vm->heap.IsAlive(handle)) return false;
    vm->pinnedRoots.push_back(handle);
    return true;
}

void UnpinObject(Vm* vm, ObjectHandle handle)
{
    if (!vm) return;
    for (size_t i = vm->pinnedRoots.size(); i > 0; --i)
    {
        if (vm->pinnedRoots[i - 1] != handle) continue;
        vm->pinnedRoots.erase(vm->pinnedRoots.begin() + (ptrdiff_t)(i - 1));
        return;
    }
}

} // namespace Fluxion::Script
