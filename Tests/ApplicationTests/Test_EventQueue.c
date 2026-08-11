#include "TestFramework.h"

#include <Fluxion/Application/Events/EventQueue.h>

static FluxionEvent Fluxion_MakeKeyEvent(i32 keyCode)
{
    FluxionEvent event;
    event.type = FLUXION_EVENT_KEY_DOWN;
    event.window.index = 0;
    event.window.generation = 1;
    event.data.key.keyCode = keyCode;
    event.data.key.repeat = false;
    return event;
}

void Test_EventQueue_Run(TestContext* ctx)
{
    FluxionEventQueue queue;
    Fluxion_EventQueue_Init(&queue, NULL, 4); // already a power of two

    TEST_CHECK(ctx, Fluxion_EventQueue_Count(&queue) == 0);

    FluxionEvent outEvent;
    TEST_CHECK(ctx, Fluxion_EventQueue_Pop(&queue, &outEvent) == false);

    for (i32 i = 0; i < 4; ++i)
    {
        FluxionEvent event = Fluxion_MakeKeyEvent(i);
        TEST_CHECK(ctx, Fluxion_EventQueue_Push(&queue, &event));
    }
    TEST_CHECK(ctx, Fluxion_EventQueue_Count(&queue) == 4);

    FluxionEvent overflow = Fluxion_MakeKeyEvent(99);
    TEST_CHECK(ctx, Fluxion_EventQueue_Push(&queue, &overflow) == false);

    for (i32 i = 0; i < 4; ++i)
    {
        FluxionEvent popped;
        TEST_CHECK(ctx, Fluxion_EventQueue_Pop(&queue, &popped));
        TEST_CHECK(ctx, popped.data.key.keyCode == i);
    }
    TEST_CHECK(ctx, Fluxion_EventQueue_Count(&queue) == 0);
    TEST_CHECK(ctx, Fluxion_EventQueue_Pop(&queue, &outEvent) == false);

    // Wraparound: repeatedly push/pop past the physical end of the buffer.
    for (i32 round = 0; round < 3; ++round)
    {
        for (i32 i = 0; i < 3; ++i)
        {
            FluxionEvent event = Fluxion_MakeKeyEvent(round * 10 + i);
            TEST_CHECK(ctx, Fluxion_EventQueue_Push(&queue, &event));
        }
        for (i32 i = 0; i < 3; ++i)
        {
            FluxionEvent popped;
            TEST_CHECK(ctx, Fluxion_EventQueue_Pop(&queue, &popped));
            TEST_CHECK(ctx, popped.data.key.keyCode == round * 10 + i);
        }
    }

    Fluxion_EventQueue_Destroy(&queue);
}
