#include <Fluxion/Foundation/UUID.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool s_seeded = false;

static void Fluxion_UUID_EnsureSeeded(void)
{
    if (!s_seeded)
    {
        srand((unsigned int)time(NULL));
        s_seeded = true;
    }
}

FluxionUUID Fluxion_UUID_Generate(void)
{
    Fluxion_UUID_EnsureSeeded();

    FluxionUUID id;
    for (int i = 0; i < 16; ++i)
    {
        id.bytes[i] = (u8)(rand() & 0xFF);
    }

    // RFC 4122 version 4 (random) and variant bits.
    id.bytes[6] = (u8)((id.bytes[6] & 0x0Fu) | 0x40u);
    id.bytes[8] = (u8)((id.bytes[8] & 0x3Fu) | 0x80u);

    return id;
}

bool Fluxion_UUID_Equals(FluxionUUID a, FluxionUUID b)
{
    return memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

bool Fluxion_UUID_IsNil(FluxionUUID id)
{
    for (int i = 0; i < 16; ++i)
    {
        if (id.bytes[i] != 0)
        {
            return false;
        }
    }
    return true;
}

void Fluxion_UUID_ToString(FluxionUUID id, char outBuffer[37])
{
    snprintf(outBuffer, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        id.bytes[0], id.bytes[1], id.bytes[2], id.bytes[3],
        id.bytes[4], id.bytes[5],
        id.bytes[6], id.bytes[7],
        id.bytes[8], id.bytes[9],
        id.bytes[10], id.bytes[11], id.bytes[12], id.bytes[13], id.bytes[14], id.bytes[15]);
}
