#include "TestFramework.h"

#include <Fluxion/Script/Runtime/CompileCache.hpp>
#include <Fluxion/Script/Runtime/ModuleSerializer.hpp>
#include <Fluxion/Script/Script.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using namespace Fluxion::Script;

namespace
{

void ReportDiagnostics(const DiagnosticList& diagnostics)
{
    for (const Diagnostic& entry : diagnostics.entries)
    {
        std::fprintf(stderr, "  %s:%u:%u: %s\n", entry.location.file.c_str(), entry.location.line, entry.location.column,
            entry.message.c_str());
    }
}

// Something of every shape the image has to be able to carry: a class with
// a base, an interface satisfied by both, a value type, a named set, a
// sequence, annotations with each kind of argument, text constants and a
// method that answers with a number a test can check.
const char* const kRichSource =
    "interface Named\n"
    "{\n"
    "    int Rank();\n"
    "}\n"
    "\n"
    "enum Grade { Low = 1, High = 7 }\n"
    "\n"
    "struct Pair\n"
    "{\n"
    "    int left;\n"
    "    int right;\n"
    "\n"
    "    Pair(int a, int b) { this.left = a; this.right = b; }\n"
    "    int Sum() { return this.left + this.right; }\n"
    "}\n"
    "\n"
    "class Base : Named\n"
    "{\n"
    "    [SerializeField]\n"
    "    [Range(0.0, 4.5)]\n"
    "    [Tooltip(\"a field with something said about it\")]\n"
    "    [Header(\"numbers\")]\n"
    "    int seed;\n"
    "\n"
    "    Pair pair;\n"
    "    Grade grade;\n"
    "    string label;\n"
    "\n"
    "    Base() { this.seed = 3; this.pair = new Pair(2, 5); this.grade = Grade.High; this.label = \"base\"; }\n"
    "\n"
    "    virtual int Rank() { return this.seed; }\n"
    "}\n"
    "\n"
    "[RequireComponent(typeof(Base))]\n"
    "class Derived : Base\n"
    "{\n"
    "    Base other;\n"
    "\n"
    "    override int Rank() { return this.seed * 10 + this.pair.Sum(); }\n"
    "}\n"
    "\n"
    "static class Entry\n"
    "{\n"
    "    static int Total()\n"
    "    {\n"
    "        Derived d = new Derived();\n"
    "        int[] numbers = new int[4];\n"
    "        for (int i = 0; i < 4; i += 1) { numbers[i] = i * i; }\n"
    "\n"
    "        List<int> grown = new List<int>();\n"
    "        grown.Add(11);\n"
    "        grown.Add(22);\n"
    "\n"
    "        Named seen = d;\n"
    "        int bonus = 0;\n"
    "        if (d.grade == Grade.High) { bonus = 1; }\n"
    "        return seen.Rank() + numbers[3] + grown.Get(1) + bonus;\n"
    "    }\n"
    "}\n";

bool CompileRich(TestContext& ctx, const char* label, CompiledModule& outModule)
{
    DiagnosticList diagnostics;
    CompileOptions options;
    options.fileName = label;

    auto compiled = Compile(kRichSource, options, diagnostics);
    if (!compiled.IsOk())
    {
        std::fprintf(stderr, "  FAIL: '%s' did not compile\n", label);
        ReportDiagnostics(diagnostics);
        ++ctx.failures;
        return false;
    }

    outModule = compiled.Value();
    return true;
}

// What the module says about itself, compared piece by piece rather than
// as a memory image: the point is that everything the interpreter reads
// survived, not that two structs happen to sit the same way in memory.
bool SameModule(const BytecodeModule& a, const BytecodeModule& b)
{
    if (a.sourceName != b.sourceName) return false;
    if (a.header.languageVersion != b.header.languageVersion) return false;
    if (a.header.bytecodeVersion != b.header.bytecodeVersion) return false;
    if (a.header.engineAbiVersion != b.header.engineAbiVersion) return false;
    if (a.header.moduleVersion != b.header.moduleVersion) return false;

    if (a.code.size() != b.code.size()) return false;
    for (size_t i = 0; i < a.code.size(); ++i)
    {
        if (a.code[i].op != b.code[i].op || a.code[i].operand != b.code[i].operand) return false;
    }

    if (a.sourceLines != b.sourceLines) return false;
    if (a.intConstants != b.intConstants) return false;
    if (a.stringConstants != b.stringConstants) return false;

    if (a.floatConstants.size() != b.floatConstants.size()) return false;
    for (size_t i = 0; i < a.floatConstants.size(); ++i)
    {
        // Written out as the exact bits it was held as, so this is
        // equality and not a tolerance.
        if (a.floatConstants[i] != b.floatConstants[i]) return false;
    }

    if (a.classes.size() != b.classes.size()) return false;
    for (size_t i = 0; i < a.classes.size(); ++i)
    {
        const ClassInfo& left = a.classes[i];
        const ClassInfo& right = b.classes[i];

        const bool sameShape = left.name == right.name && left.baseClass == right.baseClass &&
                               left.isInterface == right.isInterface && left.isStatic == right.isStatic &&
                               left.isStruct == right.isStruct && left.isEnum == right.isEnum && left.isArray == right.isArray &&
                               left.elementIsReference == right.elementIsReference &&
                               left.elementSlotCount == right.elementSlotCount && left.fieldSlotCount == right.fieldSlotCount &&
                               left.fieldReferenceBits == right.fieldReferenceBits && left.vtable == right.vtable &&
                               left.constructorFunction == right.constructorFunction;
        if (!sameShape) return false;

        if (left.interfaces.size() != right.interfaces.size()) return false;
        for (size_t k = 0; k < left.interfaces.size(); ++k)
        {
            if (left.interfaces[k].interfaceClass != right.interfaces[k].interfaceClass) return false;
            if (left.interfaces[k].methods != right.interfaces[k].methods) return false;
        }

        if (left.attributes.size() != right.attributes.size()) return false;
        for (size_t k = 0; k < left.attributes.size(); ++k)
        {
            if (left.attributes[k].name != right.attributes[k].name) return false;
            if (left.attributes[k].arguments.size() != right.attributes[k].arguments.size()) return false;

            for (size_t a2 = 0; a2 < left.attributes[k].arguments.size(); ++a2)
            {
                const AttributeArgument& x = left.attributes[k].arguments[a2];
                const AttributeArgument& y = right.attributes[k].arguments[a2];
                const bool sameArgument = x.kind == y.kind && x.intValue == y.intValue && x.floatValue == y.floatValue &&
                                          x.stringValue == y.stringValue && x.classIndex == y.classIndex;
                if (!sameArgument) return false;
            }
        }

        if (left.fields.size() != right.fields.size()) return false;
        for (size_t k = 0; k < left.fields.size(); ++k)
        {
            const FieldInfo& x = left.fields[k];
            const FieldInfo& y = right.fields[k];
            if (x.name != y.name || x.type != y.type || x.typeClass != y.typeClass || x.slot != y.slot) return false;
            if (x.attributes.size() != y.attributes.size()) return false;
        }
    }

    if (a.functions.size() != b.functions.size()) return false;
    for (size_t i = 0; i < a.functions.size(); ++i)
    {
        const FunctionInfo& left = a.functions[i];
        const FunctionInfo& right = b.functions[i];
        const bool same = left.qualifiedName == right.qualifiedName && left.sourceFile == right.sourceFile &&
                          left.returnType == right.returnType && left.parameterTypes == right.parameterTypes &&
                          left.returnSlotCount == right.returnSlotCount && left.parameterSlotCount == right.parameterSlotCount &&
                          left.receiverSlotCount == right.receiverSlotCount && left.receiverIsValue == right.receiverIsValue &&
                          left.localSlotCount == right.localSlotCount && left.codeOffset == right.codeOffset &&
                          left.codeLength == right.codeLength && left.hasBody == right.hasBody &&
                          left.owningClass == right.owningClass && left.vtableSlot == right.vtableSlot &&
                          left.isInstance == right.isInstance && left.localReferenceBits == right.localReferenceBits;
        if (!same) return false;
    }

    if (a.boundCalls.size() != b.boundCalls.size()) return false;
    for (size_t i = 0; i < a.boundCalls.size(); ++i)
    {
        const BoundCallSite& left = a.boundCalls[i];
        const BoundCallSite& right = b.boundCalls[i];
        const bool same = left.typeName == right.typeName && left.methodName == right.methodName &&
                          left.isInstance == right.isInstance && left.parameterTypes == right.parameterTypes &&
                          left.returnType == right.returnType;
        if (!same) return false;
    }

    return true;
}

// True when the reader refused, which is the only acceptable answer to
// every image below. Nothing here may be accepted, and nothing here may
// stop the test run either.
bool Refused(TestContext& ctx, const char* what, const std::vector<u8>& bytes)
{
    DiagnosticList diagnostics;
    BytecodeModule module;
    const bool accepted = ReadModule(bytes.data(), bytes.size(), module, diagnostics);
    if (accepted)
    {
        std::fprintf(stderr, "  FAIL: the reader accepted %s\n", what);
        ++ctx.failures;
        return false;
    }

    // A refusal has to say something: a caller that is told only "no" has
    // nothing to act on and nothing to print.
    if (diagnostics.entries.empty())
    {
        std::fprintf(stderr, "  FAIL: the reader refused %s without saying why\n", what);
        ++ctx.failures;
        return false;
    }
    return true;
}

// --- What a cache file is made of, for the tests that damage one --------

std::filesystem::path MakeCacheDirectory(TestContext& ctx, const char* name)
{
    std::error_code error;
    std::filesystem::path root = std::filesystem::temp_directory_path(error);
    if (error)
    {
        std::fprintf(stderr, "  FAIL: there is nowhere to put a script cache for '%s'\n", name);
        ++ctx.failures;
        return std::filesystem::path();
    }

    std::filesystem::path directory = root / "FluxionScriptCacheTests" / name;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        std::fprintf(stderr, "  FAIL: could not make a script cache directory for '%s'\n", name);
        ++ctx.failures;
        return std::filesystem::path();
    }
    return directory;
}

bool ReadFileBytes(const std::filesystem::path& path, std::vector<u8>& outBytes)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    outBytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !outBytes.empty();
}

bool WriteFileBytes(const std::filesystem::path& path, const std::vector<u8>& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    if (!bytes.empty()) file.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    return (bool)file;
}

// One trip through the cache, with everything a test wants to assert
// gathered in one place.
struct CacheAttempt
{
    bool ok = false;
    bool wasCached = false;
    bool wrote = false;
    bool reportedAnError = false;
    u64 compiledDelta = 0;
    u64 loadedDelta = 0;
    std::string path;
};

CacheAttempt RunCached(const std::string& source, const std::filesystem::path& directory)
{
    const CompileCacheCounters before = GetCompileCacheCounters();

    CompileOptions options;
    options.fileName = "cached.fls";

    CompileCacheOptions cache;
    cache.directory = directory.string();

    DiagnosticList diagnostics;
    CompileCacheReport report;
    auto compiled = CompileCached(source, options, cache, diagnostics, report);

    const CompileCacheCounters after = GetCompileCacheCounters();

    CacheAttempt attempt;
    attempt.ok = compiled.IsOk();
    attempt.wasCached = report.wasCached;
    attempt.wrote = report.wrote;
    attempt.reportedAnError = diagnostics.HasErrors();
    attempt.compiledDelta = after.compiled - before.compiled;
    attempt.loadedDelta = after.loaded - before.loaded;
    attempt.path = report.path;
    return attempt;
}

const char* const kCachedSource =
    "static class Sum\n"
    "{\n"
    "    static int Of(int n)\n"
    "    {\n"
    "        int total = 0;\n"
    "        for (int i = 0; i <= n; i += 1) { total += i; }\n"
    "        return total;\n"
    "    }\n"
    "\n"
    "    static int Answer() { return Sum.Of(10); }\n"
    "}\n";

} // namespace

void Test_Serialization_Run(TestContext& ctx)
{
    {
        // The reader takes bytes from a file, and a file can hold anything
        // at all -- a half-finished write, a truncated copy, another
        // program's output. Every length short of the whole has to be
        // refused, and none of them may reach past what actually arrived.
        // A reader that trusts a count it just read from the very bytes it
        // is checking is the classic way this goes wrong, and it does not
        // announce itself: it reads whatever memory follows.
        CompiledModule original;
        if (CompileRich(ctx, "truncation.fls", original))
        {
            std::vector<u8> bytes;
            if (WriteModule(original, bytes) && bytes.size() > 8)
            {
                size_t accepted = 0;
                for (size_t length = 0; length < bytes.size(); ++length)
                {
                    DiagnosticList diagnostics;
                    BytecodeModule restored;
                    if (ReadModule(bytes.data(), length, restored, diagnostics)) ++accepted;
                }
                TEST_CHECK(ctx, accepted == 0);

                // Nothing was said about the empty case above, so it is
                // said here: no bytes is not a module.
                DiagnosticList empty;
                BytecodeModule nothing;
                TEST_CHECK(ctx, !ReadModule(nullptr, 0, nothing, empty));
            }
        }
    }
    {
        // A single flipped byte anywhere in the image. Some of these are
        // still a valid module -- flipping a bit of a number a script
        // returns changes the answer and nothing else -- so the claim is
        // not that they are all refused. The claim is that the reader
        // survives every one of them: it either refuses or produces a
        // module that holds together, and never walks off the end of what
        // it was handed.
        CompiledModule original;
        if (CompileRich(ctx, "single-byte-damage.fls", original))
        {
            std::vector<u8> bytes;
            if (WriteModule(original, bytes) && !bytes.empty())
            {
                bool consistent = true;
                for (size_t index = 0; index < bytes.size(); ++index)
                {
                    for (const u8 pattern : { (u8)0x00, (u8)0xFF, (u8)0x7F })
                    {
                        std::vector<u8> damaged = bytes;
                        if (damaged[index] == pattern) continue;
                        damaged[index] = pattern;

                        DiagnosticList diagnostics;
                        BytecodeModule restored;
                        if (!ReadModule(damaged.data(), damaged.size(), restored, diagnostics)) continue;

                        // It said yes, so what it produced has to be
                        // usable: every function's code has to lie inside
                        // the code that arrived, and every instruction has
                        // to have a source line beside it.
                        if (restored.sourceLines.size() != restored.code.size()) consistent = false;
                        for (const FunctionInfo& function : restored.functions)
                        {
                            const size_t first = (size_t)function.codeOffset;
                            const size_t last = first + (size_t)function.codeLength;
                            if (last > restored.code.size() || last < first) consistent = false;
                        }
                    }
                }
                TEST_CHECK(ctx, consistent);
            }
        }
    }
    {
        // Bytes that were never a module at all, including ones that open
        // with the right signature and then lie about everything after it.
        CompiledModule original;
        if (CompileRich(ctx, "not-a-module.fls", original))
        {
            std::vector<u8> bytes;
            if (WriteModule(original, bytes) && bytes.size() > 32)
            {
                bool survived = true;
                u32 noise = 0x12345678u;
                for (int attempt = 0; attempt < 400; ++attempt)
                {
                    std::vector<u8> garbage(bytes.begin(), bytes.begin() + 16);
                    const size_t tail = 1 + (size_t)(noise % 512u);
                    for (size_t i = 0; i < tail; ++i)
                    {
                        noise = noise * 1664525u + 1013904223u;
                        garbage.push_back((u8)(noise >> 16));
                    }

                    DiagnosticList diagnostics;
                    BytecodeModule restored;
                    // The only thing being established is that it returns
                    // at all rather than reading past its buffer; either
                    // answer is a fine answer.
                    if (ReadModule(garbage.data(), garbage.size(), restored, diagnostics))
                    {
                        if (restored.sourceLines.size() != restored.code.size()) survived = false;
                    }
                }
                TEST_CHECK(ctx, survived);
            }
        }
    }
    {
        // Everything the compiler produced comes back, and the image that
        // came back runs and answers the same thing the original does.
        CompiledModule original;
        if (CompileRich(ctx, "round-trip.fls", original))
        {
            std::vector<u8> bytes;
            TEST_CHECK(ctx, WriteModule(original, bytes));
            TEST_CHECK(ctx, !bytes.empty());

            DiagnosticList diagnostics;
            BytecodeModule restored;
            const bool read = ReadModule(bytes.data(), bytes.size(), restored, diagnostics);
            TEST_CHECK(ctx, read);
            if (!read) ReportDiagnostics(diagnostics);

            if (read)
            {
                TEST_CHECK(ctx, SameModule(original, restored));

                // Writing what was read produces the same bytes again,
                // which says the format has no state the reader dropped on
                // the floor.
                std::vector<u8> again;
                TEST_CHECK(ctx, WriteModule(restored, again));
                TEST_CHECK(ctx, again == bytes);

                Vm* vm = CreateVm(restored, diagnostics);
                TEST_CHECK(ctx, vm != nullptr);
                if (vm)
                {
                    auto answer = Invoke(vm, "Entry.Total");
                    TEST_CHECK(ctx, answer.IsOk());

                    // 3 * 10 + (2 + 5) = 37, plus 3 * 3 = 9, plus 22, plus
                    // one for the named constant having survived.
                    if (answer.IsOk()) TEST_CHECK(ctx, answer.Value().intValue == 69);
                    DestroyVm(vm);
                }
            }
        }
    }

    {
        // Nothing that is not exactly one whole module of this build's
        // making may be accepted, however plausible it looks.
        CompiledModule original;
        if (CompileRich(ctx, "refusals.fls", original))
        {
            std::vector<u8> good;
            TEST_CHECK(ctx, WriteModule(original, good));

            Refused(ctx, "no bytes at all", std::vector<u8>());

            std::vector<u8> wrongMagic = good;
            wrongMagic[0] = 'X';
            Refused(ctx, "an image whose signature does not match", wrongMagic);

            std::vector<u8> wrongVersion = good;
            wrongVersion[8] = (u8)(good[8] + 1u);
            Refused(ctx, "an image built for a different instruction set", wrongVersion);

            std::vector<u8> wrongEngine = good;
            wrongEngine[12] = (u8)(good[12] + 1u);
            Refused(ctx, "an image built against a different engine interface", wrongEngine);

            // Truncated at every scale, since where an image stops decides
            // which read runs out of bytes.
            for (size_t keep : { (size_t)1, (size_t)4, (size_t)15, good.size() / 4, good.size() / 2, good.size() - 1 })
            {
                if (keep >= good.size()) continue;
                std::vector<u8> truncated(good.begin(), good.begin() + (std::ptrdiff_t)keep);

                char what[64];
                std::snprintf(what, sizeof(what), "an image truncated to %u bytes", (unsigned)keep);
                Refused(ctx, what, truncated);
            }

            std::vector<u8> trailing = good;
            trailing.push_back(0u);
            Refused(ctx, "an image with a byte after the end of it", trailing);

            // A length in the middle of the image blown up to something
            // the file could not possibly hold. The instruction count is
            // the first one after the source name, whose own length is the
            // four bytes at offset 20.
            std::vector<u8> absurdLength = good;
            {
                u32 nameLength = 0;
                for (int i = 0; i < 4; ++i) nameLength |= (u32)absurdLength[20 + (size_t)i] << (i * 8);

                const size_t codeCountAt = 24 + (size_t)nameLength;
                if (codeCountAt + 4 <= absurdLength.size())
                {
                    for (int i = 0; i < 4; ++i) absurdLength[codeCountAt + (size_t)i] = 0xFFu;
                    Refused(ctx, "an image claiming more instructions than could fit in it", absurdLength);
                }
                else
                {
                    std::fprintf(stderr, "  FAIL: the image is not laid out the way this test assumes\n");
                    ++ctx.failures;
                }
            }

            // Arbitrary bytes, in bulk. None of them is a module and none
            // of them may take the process down.
            for (u32 seed = 1; seed <= 64; ++seed)
            {
                std::vector<u8> noise;
                u32 state = seed * 2654435761u + 1u;
                const size_t length = (size_t)(state % 700u) + 1u;
                noise.reserve(length);
                for (size_t i = 0; i < length; ++i)
                {
                    state = state * 1664525u + 1013904223u;
                    noise.push_back((u8)(state >> 16));
                }

                DiagnosticList ignored;
                BytecodeModule module;
                TEST_CHECK(ctx, !ReadModule(noise.data(), noise.size(), module, ignored));
            }

            // The same, but starting with the right signature and versions
            // so the reader gets past the header and has to refuse on the
            // strength of what follows.
            for (u32 seed = 1; seed <= 64; ++seed)
            {
                std::vector<u8> noise(good.begin(), good.begin() + 16);
                u32 state = seed * 40503u + 7u;
                const size_t length = (size_t)(state % 400u) + 1u;
                for (size_t i = 0; i < length; ++i)
                {
                    state = state * 1664525u + 1013904223u;
                    noise.push_back((u8)(state >> 16));
                }

                DiagnosticList ignored;
                BytecodeModule module;
                TEST_CHECK(ctx, !ReadModule(noise.data(), noise.size(), module, ignored));
            }
        }
    }

    {
        // An image that reads through cleanly still has to agree with
        // itself. A class that says it was built on a class the image does
        // not have is a refusal, not something to be discovered halfway
        // through a call.
        CompiledModule tampered;
        if (CompileRich(ctx, "inconsistent.fls", tampered))
        {
            CompiledModule copy = tampered;
            TEST_CHECK(ctx, !copy.classes.empty());
            if (!copy.classes.empty())
            {
                copy.classes[0].baseClass = (u32)copy.classes.size() + 100u;

                std::vector<u8> bytes;
                TEST_CHECK(ctx, WriteModule(copy, bytes));
                Refused(ctx, "an image whose class is built on a class it does not have", bytes);
            }

            copy = tampered;
            TEST_CHECK(ctx, !copy.functions.empty());
            if (!copy.functions.empty())
            {
                for (FunctionInfo& function : copy.functions)
                {
                    if (!function.hasBody) continue;
                    function.codeLength = (u32)copy.code.size() + 1u;
                    break;
                }

                std::vector<u8> bytes;
                TEST_CHECK(ctx, WriteModule(copy, bytes));
                Refused(ctx, "an image whose method runs past the end of its own instructions", bytes);
            }

            copy = tampered;
            copy.sourceLines.pop_back();
            {
                std::vector<u8> bytes;
                TEST_CHECK(ctx, WriteModule(copy, bytes));
                Refused(ctx, "an image with fewer source lines than instructions", bytes);
            }

            copy = tampered;
            TEST_CHECK(ctx, !copy.code.empty());
            if (!copy.code.empty())
            {
                copy.code[0].op = (OpCode)((u16)OpCode::Halt + 1u);

                std::vector<u8> bytes;
                TEST_CHECK(ctx, WriteModule(copy, bytes));
                Refused(ctx, "an image holding an instruction this build does not have", bytes);
            }
        }
    }

    {
        // The same input twice: the second time the compiler does not run
        // at all, which is counted rather than timed.
        const std::filesystem::path directory = MakeCacheDirectory(ctx, "hit");
        if (!directory.empty())
        {
            const CacheAttempt first = RunCached(kCachedSource, directory);
            TEST_CHECK(ctx, first.ok);
            TEST_CHECK(ctx, !first.wasCached);
            TEST_CHECK(ctx, first.wrote);
            TEST_CHECK(ctx, first.compiledDelta == 1);
            TEST_CHECK(ctx, first.loadedDelta == 0);

            const CacheAttempt second = RunCached(kCachedSource, directory);
            TEST_CHECK(ctx, second.ok);
            TEST_CHECK(ctx, second.wasCached);
            TEST_CHECK(ctx, second.compiledDelta == 0);
            TEST_CHECK(ctx, second.loadedDelta == 1);
            TEST_CHECK(ctx, second.path == first.path);

            // And what came out of the file is a module that runs.
            CompileOptions options;
            options.fileName = "cached.fls";

            CompileCacheOptions cache;
            cache.directory = directory.string();

            DiagnosticList diagnostics;
            CompileCacheReport report;
            auto compiled = CompileCached(kCachedSource, options, cache, diagnostics, report);
            TEST_CHECK(ctx, compiled.IsOk() && report.wasCached);
            if (compiled.IsOk())
            {
                Vm* vm = CreateVm(compiled.Value(), diagnostics);
                TEST_CHECK(ctx, vm != nullptr);
                if (vm)
                {
                    auto answer = Invoke(vm, "Sum.Answer");
                    TEST_CHECK(ctx, answer.IsOk());
                    if (answer.IsOk()) TEST_CHECK(ctx, answer.Value().intValue == 55);
                    DestroyVm(vm);
                }
            }

            // Different source, same directory: a different entry, and the
            // compiler runs again.
            const CacheAttempt other = RunCached(std::string(kCachedSource) + "\nstatic class Extra { static int Zero() { return 0; } }\n",
                directory);
            TEST_CHECK(ctx, other.ok);
            TEST_CHECK(ctx, !other.wasCached);
            TEST_CHECK(ctx, other.compiledDelta == 1);
            TEST_CHECK(ctx, other.path != first.path);
        }
    }

    {
        // A cache file that is half of one is a miss and nothing else: no
        // failure, no diagnostic, and the answer is still a module.
        const std::filesystem::path directory = MakeCacheDirectory(ctx, "truncated");
        if (!directory.empty())
        {
            const CacheAttempt first = RunCached(kCachedSource, directory);
            TEST_CHECK(ctx, first.ok && first.wrote);

            std::vector<u8> stored;
            TEST_CHECK(ctx, ReadFileBytes(first.path, stored));
            if (!stored.empty())
            {
                for (size_t keep : { (size_t)0, (size_t)3, (size_t)12, (size_t)20, stored.size() / 2, stored.size() - 1 })
                {
                    if (keep >= stored.size()) continue;

                    std::vector<u8> cut(stored.begin(), stored.begin() + (std::ptrdiff_t)keep);
                    TEST_CHECK(ctx, WriteFileBytes(first.path, cut));

                    const CacheAttempt attempt = RunCached(kCachedSource, directory);
                    TEST_CHECK(ctx, attempt.ok);
                    TEST_CHECK(ctx, !attempt.wasCached);
                    TEST_CHECK(ctx, !attempt.reportedAnError);
                    TEST_CHECK(ctx, attempt.compiledDelta == 1);
                }
            }
        }
    }

    {
        // The same for a file that is not one of these at all.
        const std::filesystem::path directory = MakeCacheDirectory(ctx, "garbage");
        if (!directory.empty())
        {
            const CacheAttempt first = RunCached(kCachedSource, directory);
            TEST_CHECK(ctx, first.ok && first.wrote);

            std::vector<u8> noise;
            u32 state = 12345u;
            for (size_t i = 0; i < 4096; ++i)
            {
                state = state * 1664525u + 1013904223u;
                noise.push_back((u8)(state >> 16));
            }
            TEST_CHECK(ctx, WriteFileBytes(first.path, noise));

            const CacheAttempt attempt = RunCached(kCachedSource, directory);
            TEST_CHECK(ctx, attempt.ok);
            TEST_CHECK(ctx, !attempt.wasCached);
            TEST_CHECK(ctx, !attempt.reportedAnError);
            TEST_CHECK(ctx, attempt.compiledDelta == 1);

            // A file that is the right length and starts the right way but
            // was written by a build that laid it out differently.
            std::vector<u8> stored;
            TEST_CHECK(ctx, ReadFileBytes(first.path, stored));
            if (stored.size() > 32)
            {
                std::vector<u8> otherFormat = stored;
                otherFormat[4] = (u8)(otherFormat[4] + 1u);
                TEST_CHECK(ctx, WriteFileBytes(first.path, otherFormat));

                const CacheAttempt formatMiss = RunCached(kCachedSource, directory);
                TEST_CHECK(ctx, formatMiss.ok && !formatMiss.wasCached && !formatMiss.reportedAnError);
                TEST_CHECK(ctx, formatMiss.compiledDelta == 1);

                // And one holding a module built for a different
                // instruction set: 4 signature bytes, 4 for the layout, 8
                // for the key, then the module's own signature and its
                // language version before the one that matters.
                std::vector<u8> otherBytecode = stored;
                otherBytecode[16 + 8] = (u8)(otherBytecode[16 + 8] + 1u);
                TEST_CHECK(ctx, WriteFileBytes(first.path, otherBytecode));

                const CacheAttempt versionMiss = RunCached(kCachedSource, directory);
                TEST_CHECK(ctx, versionMiss.ok && !versionMiss.wasCached && !versionMiss.reportedAnError);
                TEST_CHECK(ctx, versionMiss.compiledDelta == 1);
            }
        }
    }

    {
        // A directory that was never named turns the whole thing off: every
        // call compiles, nothing is written, and no path is reported.
        CompileOptions options;
        options.fileName = "uncached.fls";

        CompileCacheOptions cache;

        for (int i = 0; i < 2; ++i)
        {
            const CompileCacheCounters before = GetCompileCacheCounters();

            DiagnosticList diagnostics;
            CompileCacheReport report;
            auto compiled = CompileCached(kCachedSource, options, cache, diagnostics, report);

            const CompileCacheCounters after = GetCompileCacheCounters();
            TEST_CHECK(ctx, compiled.IsOk());
            TEST_CHECK(ctx, !report.wasCached && !report.wrote && report.path.empty());
            TEST_CHECK(ctx, after.compiled - before.compiled == 1);
        }
    }

    {
        // Source that does not compile is a failure whether or not there is
        // a cache, and nothing is left behind for the next run to find.
        const std::filesystem::path directory = MakeCacheDirectory(ctx, "broken");
        if (!directory.empty())
        {
            CompileOptions options;
            options.fileName = "broken.fls";

            CompileCacheOptions cache;
            cache.directory = directory.string();

            DiagnosticList diagnostics;
            CompileCacheReport report;
            auto compiled = CompileCached("static class Broken { static int Go() { return }", options, cache, diagnostics, report);

            TEST_CHECK(ctx, !compiled.IsOk());
            TEST_CHECK(ctx, diagnostics.HasErrors());
            TEST_CHECK(ctx, !report.wrote);

            std::error_code error;
            const bool leftBehind = std::filesystem::exists(std::filesystem::path(report.path), error);
            TEST_CHECK(ctx, !leftBehind);
        }
    }
}
