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

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FluxionPluginType
{
    FLUXION_PLUGIN_TYPE_RUNTIME = 0,
    FLUXION_PLUGIN_TYPE_RENDERER,
    FLUXION_PLUGIN_TYPE_RHI,
    FLUXION_PLUGIN_TYPE_PHYSICS,
    FLUXION_PLUGIN_TYPE_AUDIO,
    FLUXION_PLUGIN_TYPE_IMPORTER,
    FLUXION_PLUGIN_TYPE_EDITOR,
    FLUXION_PLUGIN_TYPE_DEVELOPER,
    FLUXION_PLUGIN_TYPE_GAME,
} FluxionPluginType;

#define FLUXION_PLUGIN_MAX_NAME_LENGTH  63
#define FLUXION_PLUGIN_MAX_DEPENDENCIES 8

// Fully POD, fixed-size, no owned heap memory — a small, generous cap on
// names/dependencies is simpler and cheaper than managing the lifetime of
// a dynamic string array for something this small.
typedef struct FluxionPluginDescriptor
{
    char name[FLUXION_PLUGIN_MAX_NAME_LENGTH + 1];
    char library[FLUXION_PLUGIN_MAX_NAME_LENGTH + 1]; // base name, no prefix/extension
    u32 version;
    FluxionPluginType type;
    char dependencies[FLUXION_PLUGIN_MAX_DEPENDENCIES][FLUXION_PLUGIN_MAX_NAME_LENGTH + 1];
    u32 dependencyCount;
} FluxionPluginDescriptor;

#ifdef __cplusplus
}
#endif
