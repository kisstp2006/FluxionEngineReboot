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

// The flat list a frame is drawn from.

#include <Fluxion/RenderCore/Scene/RenderWorld.h>

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Memory/Allocator.h>

#include <string.h>

#define FLUXION_RENDER_WORLD_LOG_CATEGORY "RenderWorld"

// What a world starts with room for. It doubles from here and never
// shrinks, for the same reason the light list does not: a count that
// wobbles by one would otherwise rebuild the list twice a second.
#define FLUXION_RENDER_WORLD_INITIAL_OBJECTS 256

bool Fluxion_RenderWorld_Init(FluxionRenderWorld* world)
{
    if (world == NULL) return false;

    memset(world, 0, sizeof(*world));

    world->objects = (FluxionRenderObject*)Fluxion_Allocator_Alloc(
        Fluxion_DefaultAllocator(), (usize)FLUXION_RENDER_WORLD_INITIAL_OBJECTS * sizeof(FluxionRenderObject), FLUXION_DEFAULT_ALIGNMENT);
    if (world->objects == NULL) return false;

    world->objectCapacity = FLUXION_RENDER_WORLD_INITIAL_OBJECTS;
    return true;
}

void Fluxion_RenderWorld_Shutdown(FluxionRenderWorld* world)
{
    if (world == NULL || world->objects == NULL) return;

    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), world->objects, (usize)world->objectCapacity * sizeof(FluxionRenderObject));
    memset(world, 0, sizeof(*world));
}

void Fluxion_RenderWorld_Clear(FluxionRenderWorld* world)
{
    if (world == NULL) return;

    world->objectCount = 0;
    world->lightCount = 0;
    world->lightsInWorld = 0;
    world->camera.valid = false;
}

bool Fluxion_RenderWorld_AddObject(FluxionRenderWorld* world, const FluxionRenderObject* object)
{
    if (world == NULL || object == NULL) return false;

    if (world->objectCount >= world->objectCapacity)
    {
        const u32 capacity = world->objectCapacity != 0 ? world->objectCapacity * 2 : FLUXION_RENDER_WORLD_INITIAL_OBJECTS;
        FluxionRenderObject* grown = (FluxionRenderObject*)Fluxion_Allocator_Alloc(
            Fluxion_DefaultAllocator(), (usize)capacity * sizeof(FluxionRenderObject), FLUXION_DEFAULT_ALIGNMENT);
        if (grown == NULL)
        {
            FLUXION_LOG_ERROR(FLUXION_RENDER_WORLD_LOG_CATEGORY, "there was no room for another object this frame; it will not be drawn");
            return false;
        }

        memcpy(grown, world->objects, (usize)world->objectCount * sizeof(FluxionRenderObject));
        Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), world->objects, (usize)world->objectCapacity * sizeof(FluxionRenderObject));

        world->objects = grown;
        world->objectCapacity = capacity;
    }

    world->objects[world->objectCount++] = *object;
    return true;
}
