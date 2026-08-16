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

#include <Fluxion/Foundation/Defines.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FluxionLogLevel
{
    FLUXION_LOG_LEVEL_TRACE = 0,
    FLUXION_LOG_LEVEL_INFO,
    FLUXION_LOG_LEVEL_WARN,
    FLUXION_LOG_LEVEL_ERROR,
    FLUXION_LOG_LEVEL_FATAL,
} FluxionLogLevel;

// Minimal leveled logger writing to stdout/stderr. `file`/`line` identify
// the call site — always pass __FILE__/__LINE__, never call this directly;
// use the FLUXION_LOG_* macros below, which fill those in automatically so
// callers never have to think about it. NOT thread-safe yet.
void Fluxion_Log(FluxionLogLevel level, const char* category, const char* file, int line, const char* format, ...);

#ifdef __cplusplus
}
#endif

#define FLUXION_LOG_TRACE(category, ...) Fluxion_Log(FLUXION_LOG_LEVEL_TRACE, category, __FILE__, __LINE__, __VA_ARGS__)
#define FLUXION_LOG_INFO(category, ...)  Fluxion_Log(FLUXION_LOG_LEVEL_INFO,  category, __FILE__, __LINE__, __VA_ARGS__)
#define FLUXION_LOG_WARN(category, ...)  Fluxion_Log(FLUXION_LOG_LEVEL_WARN,  category, __FILE__, __LINE__, __VA_ARGS__)
#define FLUXION_LOG_ERROR(category, ...) Fluxion_Log(FLUXION_LOG_LEVEL_ERROR, category, __FILE__, __LINE__, __VA_ARGS__)
#define FLUXION_LOG_FATAL(category, ...) Fluxion_Log(FLUXION_LOG_LEVEL_FATAL, category, __FILE__, __LINE__, __VA_ARGS__)
