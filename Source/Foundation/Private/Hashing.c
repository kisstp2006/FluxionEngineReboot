#include <Fluxion/Foundation/Hashing.h>

#include <string.h>

u32 Fluxion_HashBytes32(const void* data, usize length)
{
    const u8* bytes = (const u8*)data;
    u32 hash = 2166136261u; // FNV-1a 32-bit offset basis
    for (usize i = 0; i < length; ++i)
    {
        hash ^= bytes[i];
        hash *= 16777619u; // FNV-1a 32-bit prime
    }
    return hash;
}

u64 Fluxion_HashBytes64(const void* data, usize length)
{
    const u8* bytes = (const u8*)data;
    u64 hash = 14695981039346656037ull; // FNV-1a 64-bit offset basis
    for (usize i = 0; i < length; ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull; // FNV-1a 64-bit prime
    }
    return hash;
}

u32 Fluxion_HashString32(const char* str)
{
    return Fluxion_HashBytes32(str, strlen(str));
}

u64 Fluxion_HashString64(const char* str)
{
    return Fluxion_HashBytes64(str, strlen(str));
}

bool Fluxion_BytesEqual(const void* a, const void* b, usize size)
{
    return memcmp(a, b, size) == 0;
}
