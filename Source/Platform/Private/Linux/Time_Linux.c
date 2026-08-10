#include <Fluxion/Platform/Time.h>

#include <time.h>

u64 Fluxion_Platform_GetHighResolutionTicks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

u64 Fluxion_Platform_GetHighResolutionFrequency(void)
{
    return 1000000000ull; // ticks are nanoseconds
}

void Fluxion_Platform_SleepMilliseconds(u32 milliseconds)
{
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
