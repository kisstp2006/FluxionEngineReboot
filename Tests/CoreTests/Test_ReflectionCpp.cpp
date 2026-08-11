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

        TEST_CHECK(ctx, Fluxion_Reflection_RegisterType(&typeInfo));

        const FluxionTypeInfo* found = Fluxion::Core::GetTypeInfo<TestReflectedVec2>();
        TEST_CHECK(ctx, found == &typeInfo);
        TEST_CHECK(ctx, found != nullptr && found->members.count == 2);
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
}
