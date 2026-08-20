# Licenses

Full text of every license anything in this repository is under — the engine's
own and the third-party code's alike — one file per license, named by its
[SPDX identifier](https://spdx.org/licenses/) where it has one, and
`LicenseRef-…` where it does not.

**Why the texts live here and not only next to the code.** Some third-party
files name their license by identifier and do not carry its text. The Khronos
OpenGL headers are exactly that case: `glcorearb.h`, `glxext.h` and `wglext.h`
each say `SPDX-License-Identifier: MIT` and nothing more — so before this
directory existed, this repository shipped files under MIT without shipping the
MIT terms anywhere. That is what these files fix.

Each component also keeps its own notice where it always was, and those are
still the authoritative copies for the components that have them.

The engine's own licenses are here too. It is **dual licensed**: CPAL-1.0 for
everyone, in [`CPAL-1.0.txt`](CPAL-1.0.txt), and the
[Fluxion Engine Commercial License](LicenseRef-Fluxion-Commercial-1.0.md) for
whoever separately buys one — the latter carries a `LicenseRef-` name because it
is this project's own document and has no SPDX identifier. The CPAL text is here
for the same reason the third-party ones are: the
text has to be somewhere in the tree, and one file per SPDX identifier is where
it belongs. What is *not* duplicated here is the part that is specific to this
project: the two Exhibits, which name the Initial Developer and the attribution
information, live in [`license.md`](../license.md) in the root and nowhere else.

That file was fetched from <https://spdx.org/licenses/CPAL-1.0.txt>; its
sha256 is `0bd4ac43b6a522528b05a7fb5f642c945f21b7c6631cd01516c91a632057aa89`, so
that a copy can be checked against the source it came from rather than trusted.

## What is under what

| Component | Where it is | License | Copyright |
|---|---|---|---|
| **Fluxion Engine itself** | everything outside `ThirdParty/` | [CPAL-1.0](CPAL-1.0.txt), or [the commercial terms](LicenseRef-Fluxion-Commercial-1.0.md) where separately granted | Copyright (c) 2026 Kiss Tibor Péter — Exhibits in [`license.md`](../license.md) |
| [pdjson](https://github.com/skeeto/pdjson) | `ThirdParty/pdjson/` | [Unlicense](Unlicense.txt) | Public domain — dedicated by Christopher Wellons; no notice required |
| [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | `ThirdParty/vma/` | [MIT](MIT.txt) | Copyright (c) 2017-2026 Advanced Micro Devices, Inc. |
| [D3D12MemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator) | `ThirdParty/d3d12ma/` | [MIT](MIT.txt) | Copyright (c) 2019-2026 Advanced Micro Devices, Inc. |
| Khronos `glcorearb.h`, `glxext.h`, `wglext.h` | `ThirdParty/glheaders/GL/` | [MIT](MIT.txt) | Copyright 2013-2026 The Khronos Group Inc. |
| Khronos `khrplatform.h` | `ThirdParty/glheaders/KHR/` | [LicenseRef-Khronos-MIT-Materials](LicenseRef-Khronos-MIT-Materials.txt) | Copyright (c) 2008-2018 The Khronos Group Inc. |
| [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) | `ThirdParty/nuklear/` | [MIT](MIT.txt) — chosen from its two alternatives | Copyright (c) 2017 Micha Mettke |
| `stb_rect_pack` and `stb_truetype`, embedded inside `nuklear.h` | `ThirdParty/nuklear/nuklear.h` | [MIT](MIT.txt) — chosen from the same two alternatives | Copyright (c) 2017 Sean Barrett |

Every copyright line above was read out of the file it belongs to, and the
license texts were copied from files in this repository rather than fetched or
typed from memory — `MIT.txt` is the body of `ThirdParty/vma/LICENSE.txt` with
the copyright line replaced by the placeholder the SPDX text uses, and the two
AMD copies were checked against each other and are byte-identical below that
line.

## Nuklear offers two licenses and this repository picks one

`nuklear.h` ends with "This software is available under 2 licenses -- choose
whichever you prefer": MIT, or a public-domain dedication. **MIT is the one
taken here**, for a reason that is about texts rather than preference: the MIT
alternative is `MIT.txt` word for word, so nothing new has to be shipped for
it, while the public-domain alternative is the Unlicense text **minus its
closing line** pointing at <http://unlicense.org/> — the same offer in
substance, but not the same file as `Unlicense.txt`, and this directory's rule
is one file per text rather than one per intent.

**The same header carries two more notices that are not Nuklear's.** It embeds
Sean Barrett's `stb_rect_pack` and `stb_truetype` — the code that bakes the
panels' font, so it is compiled and not merely carried along. Each has the same
two alternatives under Barrett's own copyright, and MIT is taken there too. All
three notices are reproduced in `ThirdParty/nuklear/LICENSE`, which is the
authoritative copy; the table above lists Nuklear and the stb pair separately
because they are separate copyright holders.

## The two Khronos entries are not the same license

Worth stating plainly, because "Khronos header" is not a license and the four
files here do not agree with each other:

- `glcorearb.h`, `glxext.h` and `wglext.h` are generated from the OpenGL XML
  registry and carry `SPDX-License-Identifier: MIT`. **Plain MIT.** They are
  *not* under the Khronos Free Use License, whatever older documentation about
  Khronos headers may say.
- `khrplatform.h` in this repository is an older file — copyright 2008-2018,
  no SPDX tag — that spells its permission notice out in full. That notice is
  MIT in substance, word for word, **except that it says "Materials" wherever
  MIT says "Software"**. Different wording is a different text, so it gets a
  file of its own rather than being folded into `MIT.txt`.

**The file in front of you is what counts, not the project it came from.**
Khronos re-licensed and re-tagged these headers over time, so a refreshed copy
may say something different from the copy it replaced. Whenever one of these is
updated, re-read the top of the file and check this table against it.

### Version of the copies here

So that "which version was this table written against" has an answer:

| File | Marker |
|---|---|
| `glcorearb.h` | generated from the `gl` API registry, all versions emitted; copyright range 2013-2026 |
| `glxext.h` | `GLX_GLXEXT_VERSION 20260126` |
| `wglext.h` | `WGL_WGLEXT_VERSION 20260126` |
| `khrplatform.h` | EGL-Registry commit `67a3e0864c2d75ea5287b9f3d2eb74a745936692` |

`khrplatform.h` is the one lagging behind: upstream has since moved to SPDX
tagging, so a refresh is likely to change which row of the table above it
belongs in.

## Found on the machine, not carried here

These are not in this repository and are not distributed with it — they are
located on whatever machine builds the engine, and come under their own terms
from wherever they were installed: the Vulkan SDK, the system OpenGL library,
X11, and the DirectX Shader Compiler.

## Shader sources

No third-party shader code is vendored. What was read while writing the
engine's own shaders, and under what terms, is recorded in
[`SHADER_SOURCES.md`](../SHADER_SOURCES.md).

## Adding one

A new third-party component means three things, and the third is the one that
gets forgotten:

1. its own license file kept beside the code, as it came;
2. a row in the table above, with the copyright line read out of the file;
3. a row in the **Third party** table in the root [`README.md`](../README.md).

If its license is not already in this directory, add the full text as
`<SPDX-identifier>.txt`. If the wording differs from a standard text even
slightly — as `khrplatform.h` does — it is a different license and gets its own
`LicenseRef-*.txt` rather than being filed under the one it resembles.
