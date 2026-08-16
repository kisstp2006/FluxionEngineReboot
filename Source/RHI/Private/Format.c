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

// What every format costs, in the one place that knows.
//
// Both directions of the backends' own mapping tables and every size
// calculation above them read from here. The alternative -- each of them
// carrying its own table -- worked for as long as every format was some
// whole number of bytes a texel, and stops working at the first block
// format, where the two answers differ by a factor of eight and neither
// one reports the disagreement.

#include <Fluxion/RHI/Format.h>

#include <string.h>

FluxionRHIFormatInfo Fluxion_RHI_GetFormatInfo(FluxionRHIFormat format)
{
    FluxionRHIFormatInfo info;
    memset(&info, 0, sizeof(info));

    // The uncompressed formats all cover one texel per block, so the
    // block arithmetic below is a no-op for them rather than a special
    // case beside them.
    info.blockWidth = 1;
    info.blockHeight = 1;

    switch (format)
    {
        case FLUXION_RHI_FORMAT_R8G8B8A8_SRGB:
        case FLUXION_RHI_FORMAT_B8G8R8A8_SRGB:
            info.blockBytes = 4;
            info.srgb = true;
            return info;

        case FLUXION_RHI_FORMAT_R8G8B8A8_UNORM:
        case FLUXION_RHI_FORMAT_B8G8R8A8_UNORM:
        case FLUXION_RHI_FORMAT_R32_FLOAT:
            info.blockBytes = 4;
            return info;

        case FLUXION_RHI_FORMAT_R32G32_FLOAT:
        case FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT:
            info.blockBytes = 8;
            return info;

        case FLUXION_RHI_FORMAT_R32G32B32_FLOAT:
            info.blockBytes = 12;
            return info;

        case FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT:
            info.blockBytes = 16;
            return info;

        case FLUXION_RHI_FORMAT_D32_FLOAT:
            info.blockBytes = 4;
            info.depth = true;
            return info;

        case FLUXION_RHI_FORMAT_D24_UNORM_S8_UINT:
            info.blockBytes = 4;
            info.depth = true;
            return info;

        // --- Block-compressed --------------------------------------------
        //
        // Every BC block covers four texels by four. BC4 and BC5 carry one
        // and two channels in eight bytes each; the rest carry a full
        // colour in sixteen.
        case FLUXION_RHI_FORMAT_BC4_UNORM:
            info.blockWidth = 4;
            info.blockHeight = 4;
            info.blockBytes = 8;
            info.compressed = true;
            return info;

        case FLUXION_RHI_FORMAT_BC5_UNORM:
            info.blockWidth = 4;
            info.blockHeight = 4;
            info.blockBytes = 16;
            info.compressed = true;
            return info;

        case FLUXION_RHI_FORMAT_BC6H_UFLOAT:
        case FLUXION_RHI_FORMAT_BC7_UNORM:
            info.blockWidth = 4;
            info.blockHeight = 4;
            info.blockBytes = 16;
            info.compressed = true;
            return info;

        case FLUXION_RHI_FORMAT_BC7_SRGB:
            info.blockWidth = 4;
            info.blockHeight = 4;
            info.blockBytes = 16;
            info.compressed = true;
            info.srgb = true;
            return info;

        // Every ASTC block is sixteen bytes whatever it covers, so the
        // block size alone settles the bit rate.
        case FLUXION_RHI_FORMAT_ASTC_4X4_SRGB:
            info.blockWidth = 4;
            info.blockHeight = 4;
            info.blockBytes = 16;
            info.compressed = true;
            info.srgb = true;
            return info;

        case FLUXION_RHI_FORMAT_ASTC_4X4_UNORM:
        case FLUXION_RHI_FORMAT_ASTC_4X4_FLOAT:
            info.blockWidth = 4;
            info.blockHeight = 4;
            info.blockBytes = 16;
            info.compressed = true;
            return info;

        case FLUXION_RHI_FORMAT_ASTC_6X6_SRGB:
            info.blockWidth = 6;
            info.blockHeight = 6;
            info.blockBytes = 16;
            info.compressed = true;
            info.srgb = true;
            return info;

        case FLUXION_RHI_FORMAT_ASTC_6X6_UNORM:
        case FLUXION_RHI_FORMAT_ASTC_6X6_FLOAT:
            info.blockWidth = 6;
            info.blockHeight = 6;
            info.blockBytes = 16;
            info.compressed = true;
            return info;

        case FLUXION_RHI_FORMAT_ASTC_8X8_SRGB:
            info.blockWidth = 8;
            info.blockHeight = 8;
            info.blockBytes = 16;
            info.compressed = true;
            info.srgb = true;
            return info;

        case FLUXION_RHI_FORMAT_ASTC_8X8_UNORM:
        case FLUXION_RHI_FORMAT_ASTC_8X8_FLOAT:
            info.blockWidth = 8;
            info.blockHeight = 8;
            info.blockBytes = 16;
            info.compressed = true;
            return info;

        // Zero bytes rather than a plausible guess. A size worked out from
        // a format nothing here understands would be confidently wrong,
        // and an allocation made from it silently too small.
        default:
            info.blockBytes = 0;
            return info;
    }
}

static u32 Fluxion_RHI_DivideRoundingUp(u32 value, u32 divisor)
{
    if (divisor == 0) return 0;
    return (value + divisor - 1u) / divisor;
}

u32 Fluxion_RHI_GetFormatBlockColumns(FluxionRHIFormat format, u32 widthInTexels)
{
    const FluxionRHIFormatInfo info = Fluxion_RHI_GetFormatInfo(format);
    if (info.blockBytes == 0) return 0;
    return Fluxion_RHI_DivideRoundingUp(widthInTexels, info.blockWidth);
}

u32 Fluxion_RHI_GetFormatBlockRows(FluxionRHIFormat format, u32 heightInTexels)
{
    const FluxionRHIFormatInfo info = Fluxion_RHI_GetFormatInfo(format);
    if (info.blockBytes == 0) return 0;
    return Fluxion_RHI_DivideRoundingUp(heightInTexels, info.blockHeight);
}

usize Fluxion_RHI_GetFormatRowBytes(FluxionRHIFormat format, u32 widthInTexels)
{
    const FluxionRHIFormatInfo info = Fluxion_RHI_GetFormatInfo(format);
    if (info.blockBytes == 0 || widthInTexels == 0) return 0;

    return (usize)Fluxion_RHI_DivideRoundingUp(widthInTexels, info.blockWidth) * (usize)info.blockBytes;
}

usize Fluxion_RHI_GetFormatLevelBytes(FluxionRHIFormat format, u32 widthInTexels, u32 heightInTexels)
{
    if (heightInTexels == 0) return 0;

    const usize rowBytes = Fluxion_RHI_GetFormatRowBytes(format, widthInTexels);
    if (rowBytes == 0) return 0;

    return rowBytes * (usize)Fluxion_RHI_GetFormatBlockRows(format, heightInTexels);
}

bool Fluxion_RHI_IsFormatCompressed(FluxionRHIFormat format)
{
    return Fluxion_RHI_GetFormatInfo(format).compressed;
}

bool Fluxion_RHI_IsFormatSRGB(FluxionRHIFormat format)
{
    return Fluxion_RHI_GetFormatInfo(format).srgb;
}
