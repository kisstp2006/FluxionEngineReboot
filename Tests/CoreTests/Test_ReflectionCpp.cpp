#include "TestFramework.h"

#include <Fluxion/Core/Reflection/Reflection.hpp>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Defines.h>

namespace
{

// (a) A plain struct reflected the existing C way (FLUXION_REFLECT_PROPERTY,
// offset-mode) -- proves Fluxion::Core::GetTypeInfo<T>() finds it via T::Name.
struct TestReflectedVec2
{
    static constexpr auto Name = "TestReflectedVec2";

    f32 x;
    f32 y;
};

// (b) A C++ class reflected through an accessor property -- the setter
// clamps, so a successful round-trip proves SetPropertyValue/
// GetPropertyValue actually call the getter/setter, not a raw memcpy.
class TestReflectedClamped
{
public:
    static constexpr auto Name = "TestReflectedClamped";

    f32 GetValue() const { return m_value; }
    void SetValue(f32 value) { m_value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

private:
    f32 m_value = 0.0f;
};

// (c) A type whose methods are reflected through the trampoline
// generator: a const one, a non-const one taking several arguments, one
// returning nothing, and a free function standing in for a static.
class TestReflectedCounter
{
public:
    static constexpr auto Name = "TestReflectedCounter";

    i32 Total() const { return m_total; }
    void Reset() { m_total = 0; }
    i32 Add(i32 a, i32 b) { m_total += a + b; return m_total; }
    f32 Blend(f32 weight, i32 count, bool doubled) const
    {
        const f32 blended = weight * (f32)count;
        return doubled ? blended * 2.0f : blended;
    }

private:
    i32 m_total = 0;
};

i32 TestReflectedAddFour(i32 a, i32 b, i32 c, i32 d)
{
    return a + b + c + d;
}

void TestReflectedNothing()
{
}

} // namespace

// main.c (plain C) calls this, so it needs unmangled C linkage.
extern "C" void Test_ReflectionCpp_Run(TestContext* ctx)
{
    Fluxion_Reflection_Init();
    {
        FluxionPropertyInfo properties[] =
        {
            FLUXION_REFLECT_PROPERTY(TestReflectedVec2, x, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
            FLUXION_REFLECT_PROPERTY(TestReflectedVec2, y, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
        };

        FluxionTypeInfo typeInfo;
        typeInfo.name = Fluxion_StringView_FromCStr(TestReflectedVec2::Name);
        typeInfo.id = FLUXION_TYPE_ID_OF(TestReflectedVec2);
        typeInfo.kind = FLUXION_TYPE_KIND_STRUCT;
        typeInfo.size = sizeof(TestReflectedVec2);
        typeInfo.version = 1;
        typeInfo.members = Fluxion_Span_Make(properties, FLUXION_ARRAY_COUNT(properties), sizeof(FluxionPropertyInfo));
        typeInfo.methods = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionMethodInfo));

        TEST_CHECK(ctx, Fluxion_Reflection_RegisterType(&typeInfo));

        const FluxionTypeInfo* found = Fluxion::Core::GetTypeInfo<TestReflectedVec2>();
        TEST_CHECK(ctx, found == &typeInfo);
        TEST_CHECK(ctx, found != nullptr && found->members.count == 2);
        TEST_CHECK(ctx, found != nullptr && found->methods.count == 0);
    }
    Fluxion_Reflection_Shutdown();

    {
        FluxionPropertyInfo clampedProperty = Fluxion::Core::ReflectAccessor<&TestReflectedClamped::GetValue, &TestReflectedClamped::SetValue>(
            "value", FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE);
        TEST_CHECK(ctx, clampedProperty.accessKind == FLUXION_PROPERTY_ACCESS_ACCESSOR);

        TestReflectedClamped instance;

        f32 withinRange = 0.5f;
        Fluxion::Core::SetPropertyValue(instance, clampedProperty, withinRange);
        f32 readBack = 0.0f;
        Fluxion::Core::GetPropertyValue(instance, clampedProperty, readBack);
        TEST_CHECK(ctx, readBack == 0.5f);

        f32 outOfRange = 5.0f;
        Fluxion::Core::SetPropertyValue(instance, clampedProperty, outOfRange);
        Fluxion::Core::GetPropertyValue(instance, clampedProperty, readBack);
        TEST_CHECK(ctx, readBack == 1.0f); // proves the setter's clamp ran, not a raw memory copy
        TEST_CHECK(ctx, instance.GetValue() == 1.0f);
    }

    // Methods built from plain function and member function pointers, and
    // then registered and called back through the descriptor -- no
    // `void**` unpacking written anywhere in this test.
    Fluxion_Reflection_Init();
    {
        const FluxionTypeId addParameters[] = { FLUXION_TYPE_ID_OF(i32), FLUXION_TYPE_ID_OF(i32) };
        const FluxionTypeId blendParameters[] = { FLUXION_TYPE_ID_OF(f32), FLUXION_TYPE_ID_OF(i32), FLUXION_TYPE_ID_OF(bool) };
        const FluxionTypeId addFourParameters[] = {
            FLUXION_TYPE_ID_OF(i32), FLUXION_TYPE_ID_OF(i32), FLUXION_TYPE_ID_OF(i32), FLUXION_TYPE_ID_OF(i32)
        };

        FluxionMethodInfo methods[] = {
            Fluxion::Core::ReflectMethod<&TestReflectedCounter::Total>("Total", FLUXION_TYPE_ID_OF(i32)),
            Fluxion::Core::ReflectMethod<&TestReflectedCounter::Reset>("Reset", FLUXION_TYPE_ID_INVALID),
            Fluxion::Core::ReflectMethod<&TestReflectedCounter::Add>("Add", FLUXION_TYPE_ID_OF(i32), addParameters),
            Fluxion::Core::ReflectMethod<&TestReflectedCounter::Blend>("Blend", FLUXION_TYPE_ID_OF(f32), blendParameters),
            Fluxion::Core::ReflectMethod<&TestReflectedAddFour>("AddFour", FLUXION_TYPE_ID_OF(i32), addFourParameters),
            Fluxion::Core::ReflectMethod<&TestReflectedNothing>("Nothing", FLUXION_TYPE_ID_INVALID),
        };

        FluxionTypeInfo typeInfo;
        typeInfo.name = Fluxion_StringView_FromCStr(TestReflectedCounter::Name);
        typeInfo.id = FLUXION_TYPE_ID_OF(TestReflectedCounter);
        typeInfo.kind = FLUXION_TYPE_KIND_STRUCT;
        typeInfo.size = sizeof(TestReflectedCounter);
        typeInfo.version = 1;
        typeInfo.members = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionPropertyInfo));
        typeInfo.methods = Fluxion_Span_Make(methods, FLUXION_ARRAY_COUNT(methods), sizeof(FluxionMethodInfo));

        TEST_CHECK(ctx, Fluxion_Reflection_RegisterType(&typeInfo));

        const FluxionTypeInfo* found = Fluxion::Core::GetTypeInfo<TestReflectedCounter>();
        TEST_CHECK(ctx, found != nullptr && found->methods.count == 6);

        // Whether a method needs an instance is read off the pointer, not
        // asserted by hand: the free function is the only static one here.
        TEST_CHECK(ctx, (methods[0].flags & FLUXION_METHOD_FLAG_STATIC) == 0);
        TEST_CHECK(ctx, (methods[4].flags & FLUXION_METHOD_FLAG_STATIC) != 0);
        TEST_CHECK(ctx, methods[2].parameterCount == 2);
        TEST_CHECK(ctx, methods[3].parameterCount == 3);
        TEST_CHECK(ctx, methods[4].parameterCount == 4);

        TestReflectedCounter counter;

        // Two arguments, a real return value, and state that has to have
        // changed on the instance itself.
        i32 a = 4;
        i32 b = 6;
        void* addArgs[2] = { &a, &b };
        i32 addResult = 0;
        methods[2].invoke(&counter, addArgs, &addResult);
        TEST_CHECK(ctx, addResult == 10);
        TEST_CHECK(ctx, counter.Total() == 10);

        // A const method taking three arguments of three different types:
        // if any of them were unpacked in the wrong order or read as the
        // wrong type, this number would not come out.
        f32 weight = 1.5f;
        i32 count = 4;
        bool doubled = true;
        void* blendArgs[3] = { &weight, &count, &doubled };
        f32 blendResult = 0.0f;
        methods[3].invoke(&counter, blendArgs, &blendResult);
        TEST_CHECK(ctx, blendResult == 12.0f);

        // No arguments, a value back.
        i32 total = 0;
        methods[0].invoke(&counter, nullptr, &total);
        TEST_CHECK(ctx, total == 10);

        // No arguments, nothing back, and a visible effect.
        methods[1].invoke(&counter, nullptr, nullptr);
        TEST_CHECK(ctx, counter.Total() == 0);

        // A free function, called with no instance at all.
        i32 one = 1;
        i32 two = 2;
        i32 three = 3;
        i32 four = 4;
        void* fourArgs[4] = { &one, &two, &three, &four };
        i32 fourResult = 0;
        methods[4].invoke(nullptr, fourArgs, &fourResult);
        TEST_CHECK(ctx, fourResult == 10);

        methods[5].invoke(nullptr, nullptr, nullptr);
    }
    Fluxion_Reflection_Shutdown();
}
