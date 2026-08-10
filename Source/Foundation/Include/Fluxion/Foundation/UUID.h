#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionUUID
{
    u8 bytes[16];
} FluxionUUID;

// UUIDv4-shaped (RFC 4122 version/variant bits set), but NOT
// cryptographically secure — sufficient for engine object/asset IDs. A
// higher-quality entropy source arrives with the Platform layer.
FluxionUUID Fluxion_UUID_Generate(void);

bool Fluxion_UUID_Equals(FluxionUUID a, FluxionUUID b);
bool Fluxion_UUID_IsNil(FluxionUUID id);

// Writes a 36-character lowercase "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
// string plus null terminator (37 bytes total) into outBuffer.
void Fluxion_UUID_ToString(FluxionUUID id, char outBuffer[37]);

#ifdef __cplusplus
}
#endif
