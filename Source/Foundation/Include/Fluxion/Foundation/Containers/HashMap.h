// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

#pragma once

#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef u64  (*FluxionHashMapHashFn)(const void* key, usize keySize);
typedef bool (*FluxionHashMapEqualsFn)(const void* keyA, const void* keyB, usize keySize);

// Open-addressing (linear probing) hash map with generic, fixed-size byte
// keys/values. Grows automatically past a 0.7 load factor.
typedef struct FluxionHashMap
{
    FluxionAllocator* allocator;
    u8* keys;
    u8* values;
    u8* occupied; // 1 byte per slot: 0 = empty, 1 = occupied, 2 = tombstone
    usize keySize;
    usize valueSize;
    usize capacity; // number of slots; always a power of two once > 0
    usize count;    // occupied slots (excluding tombstones)
    FluxionHashMapHashFn hash;
    FluxionHashMapEqualsFn equals;
} FluxionHashMap;

void Fluxion_HashMap_Init(FluxionHashMap* map, FluxionAllocator* allocator, usize keySize, usize valueSize, FluxionHashMapHashFn hash, FluxionHashMapEqualsFn equals);
void Fluxion_HashMap_Destroy(FluxionHashMap* map);

// Inserts or overwrites the value for `key`. Returns false only if a
// backing allocation failed.
bool Fluxion_HashMap_Set(FluxionHashMap* map, const void* key, const void* value);

// Returns a pointer to the stored value, or NULL if `key` is not present.
void* Fluxion_HashMap_Find(FluxionHashMap* map, const void* key);

bool Fluxion_HashMap_Remove(FluxionHashMap* map, const void* key);

#ifdef __cplusplus
}
#endif
