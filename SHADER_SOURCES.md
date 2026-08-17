# Shader sources

Where every shader in this engine came from, and on whose terms.

The rule this file exists to keep: **nothing is copied into this engine
from a source that requires attribution or a preserved notice.** Only
Public Domain, The Unlicense, and CC0 sources may be used as code. Any
other source may be *read* to understand an algorithm, and nothing more.

The reference repositories live in `_external_shader_refs/`. They are not
committed, not built, and not modified — they are somebody else's code at
somebody else's terms, kept locally to be read.

---

## 1. The licence findings, before anything else

Every repository's licence was checked by reading its actual licence file
and its actual shader code, not by trusting a README or a stated licence.
Two of the findings change what may be used.

### 1.1 vkmerc — the stated licence does not cover its shaders

`_external_shader_refs/vkmerc/` carries The Unlicense at the top level.
Its shaders are not the author's to dedicate.

**Finding A — the IBL generation shaders are Sascha Willems' MIT code
with the copyright header removed.**

| vkmerc file | upstream | difference |
|---|---|---|
| `examples/res/shaders/pbr_gen/genbrdflut.frag` | `SaschaWillems/Vulkan-glTF-PBR` `data/shaders/genbrdflut.frag` | the 6-line MIT header, and nothing else |
| `examples/res/shaders/pbr_gen/irradiancecube.frag` | same repo, same name | the 6-line MIT header, and nothing else |
| `examples/res/shaders/pbr_gen/prefilterenvmap.frag` | same repo, same name | the header plus one line |
| `examples/res/shaders/pbr_gen/filtercube.vert` | same repo, same name | header plus the vertex input layout |

Verified by diffing against the upstream files. The removed header reads:

```
/* Copyright (c) 2018-2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 */
```

**Finding B — the PBR lighting shader's BRDF block is LearnOpenGL code.**

`examples/res/shaders/pbr/pbr_light.frag` contains `DistributionGGX`,
`GeometrySchlickGGX` and `GeometrySmith` character-for-character as they
appear in `JoeyDeVries/LearnOpenGL`
(`src/6.pbr/1.2.lighting_textured/1.2.pbr.fs`), down to the `nom`/`denom`
local names. LearnOpenGL's `LICENSE.md` says:

> All code samples, unless explicitly stated otherwise, are licensed under
> the terms of the CC BY-NC 4.0 license

CC BY-NC 4.0 requires attribution **and forbids commercial use**. It is
the least usable licence in this whole survey.

**Finding C — smaller borrowed pieces, in the passes that were left.**

- `ssao/ssao.frag` carries a hash function credited in-file to a Shadertoy
  user. Shadertoy code is its author's copyright unless they say
  otherwise, and this one does not.
- `pbr/cascade.glsl` and `misc/cascade_debugger.frag` carry an `sdBox`
  credited to Inigo Quilez.
- `bloom/blur.frag` is a nine-tap gaussian marked `// from mattdesl`.
  mattdesl's `glsl-fast-gaussian-blur` is MIT.

`bloom/highpass.frag` and `bloom/merge.frag` are the only shaders in this
repository with no borrowed marker and no match against the upstreams they
would most plausibly have come from. Two files out of twenty-six is not a
basis for trusting the rest.

**Consequence.** vkmerc was named as the primary mandatory reference for
PBR, BRDF, IBL, CSM, SSAO, bloom and deferred rendering. It cannot be a
**code** donor for any of them. This is exactly the case the project's own
rule names: a repository that claims public domain while carrying code
that demonstrably came from elsewhere under a different licence.

vkmerc stays cloned and stays useful — as an **architecture** reference.
How its render graph is arranged, which passes exist, what a G-buffer
layout looks like, how cascades are packed into one atlas: all of that is
structure to study, and structure is not what a licence covers. No line of
its shader code is to be copied.

### 1.2 fur-demo — no licence file, and a third-party origin

`_external_shader_refs/fur-demo/` has **no LICENSE file**. The Unlicense
text appears only in `README.md`. That is an explicit dedication and is
accepted as one — but the same README says the implementation is "adapted
from the XNA/HLSL tutorial given by Catalin Zima", whose terms are not
stated anywhere. The author can dedicate their own adaptation; they cannot
dedicate what it was adapted from.

**Consequence.** Shell texturing is a published technique, and the
technique is free to implement. Use fur-demo to understand the shell
offset and the fur texture generation; write the shader ourselves.

### 1.3 The bgolus gists — none of them are usable as code

All 15 gists at `https://gist.github.com/bgolus` were downloaded to
`_external_shader_refs/bgolus/` and checked one by one.

**Not one carries a licence, a copyright notice, or any permission
statement.** Under the project's own rule — code may be used only with an
explicit notice-free permission — every one of them is **study-only**.

One is worse than merely unlicensed: `02311bb78c` is described by its own
author as "a modification of Kyle Halladay's Pencil Sketch Effect shader",
so it is an unlicensed derivative of a third party's work.

The gists remain worth reading for depth reconstruction, world/normal
reconstruction from a depth texture, the jump-flood outline, and the grid
shaders. Read the idea, then write it.

### 1.4 The clean ones

| repository | licence | where it is stated | verified |
|---|---|---|---|
| `VolumetricFog-URP2022` | The Unlicense | `LICENSE` | full text read |
| `GPU-Fog-Particles` | The Unlicense | `LICENSE.txt` + README | full text read |
| `OpenLit` | CC0 1.0 | `LICENSE` + a notice in every source file | full text read |
| `Shadow-Tutorial` | The Unlicense | `LICENSE` | full text read |

Two notes that do not block use but change what is worth taking:

- **VolumetricFog-URP2022** calls into Unity's URP HLSL library
  (`InputData`, `AmbientOcclusionFactor`, `CalculateLight`). Those are
  Unity's, not the author's, and are not copyable. What is the author's,
  and what is worth having, is the raymarch loop and the scattering
  integration around them.
- **GPU-Fog-Particles** ships shaders generated by Amplify Shader Editor
  and headed "Available at the Unity Asset Store". The repository's own
  licence is the Unlicense and the author is the Asset Store publisher, so
  the dedication holds — but machine-generated Amplify output is a poor
  donor for a hand-written renderer regardless. Take the noise composition
  and the softness/fade maths.
- **OpenLit** is a toon-lighting library for Unity avatars, built on
  Unity's spherical-harmonic globals. Despite being listed as the generic
  lighting reference, it contains no general-purpose punctual-light or PBR
  code. What is genuinely reusable is small: sRGB conversion, luminance,
  and the shape of the SH9 evaluation.
- **Shadow-Tutorial** is a Minecraft shaderpack tutorial and uses that
  pipeline's own uniforms (`shadowModelView`, `gbufferModelViewInverse`).
  The shadow-space transform, the distortion trick and the bias derivation
  carry over as maths; the surrounding code does not.

---

## 2. Index — which file to read for which feature

Only sources that may be used as **code** are listed as approved. Where
the only reference is study-only, the index says so, and the
implementation has to be written from the published algorithm.

| feature | approved code source | study-only reference | files to read |
|---|---|---|---|
| Cook-Torrance BRDF, GGX, Smith, Schlick | *none* | vkmerc (finding B) | `vkmerc/examples/res/shaders/pbr/pbr_light.frag` |
| BRDF integration LUT | *none* | vkmerc (finding A) | `vkmerc/examples/res/shaders/pbr_gen/genbrdflut.frag` |
| Irradiance cubemap | *none* | vkmerc (finding A) | `vkmerc/examples/res/shaders/pbr_gen/irradiancecube.frag` |
| Prefiltered specular cubemap | *none* | vkmerc (finding A) | `vkmerc/examples/res/shaders/pbr_gen/prefilterenvmap.frag` |
| G-buffer layout, deferred merge | *none* (structure only) | vkmerc | `pbr/pbr_gbuf.frag`, `pbr/pbr_merge.frag` |
| Cascaded shadow maps | *none* (structure only) | vkmerc | `pbr/cascade.glsl`, `misc/cascade_debugger.frag` |
| Basic shadow mapping, bias, distortion | Shadow-Tutorial (Unlicense) | — | `Shadow-Tutorial/shaders/distort.glsl`, `shadow.vsh`, `shadow.fsh`, `composite.fsh` |
| SSAO | *none* (structure only) | vkmerc (finding C) | `ssao/ssao.frag`, `ssao/ssao_blur.frag` |
| Bloom | *none* — `blur.frag` is mattdesl's MIT gaussian (§1.1 finding C) | vkmerc | `bloom/highpass.frag`, `bloom/blur.frag`, `bloom/merge.frag` |
| Volumetric fog, raymarch, scattering | VolumetricFog-URP2022 (Unlicense) | — | `Shaders/Resources/VolumetricFog.shader`, `VolumetricFogUtils.hlsl`, `FogVolumes.hlsl` |
| Atmospheric fog VFX, noise modulation | GPU-Fog-Particles (Unlicense) | — | `Assets/Mirza Beig/GPU Fog Particles/Shaders/GPU Fog (URP).shader` |
| sRGB / luminance / SH9 utilities | OpenLit (CC0) | — | `Assets/OpenLit/core.hlsl` |
| Fur / shell rendering | *none* (technique only, §1.2) | fur-demo | `fur-demo/default.vert`, `fur-demo/default.frag` |
| Depth / world-position / normal reconstruction | *none* | bgolus (§1.3) | `bgolus/1933a1b0b4_PostDepthToWorldPos.shader`, `bgolus/a07ed65602_WorldNormalFromDepthTexture.shader` |
| Jump flood, outlines, grids, matcap | *none* | bgolus (§1.3) | `bgolus/a18c1a3fc9_*`, `bgolus/d49651f52b_PristineGrid.shader`, `bgolus/02e37cd765_MatCapTechniques.shader` |
| Volumetric clouds | *none* — from published papers only | — | — |

**No open questions remain in this table.** Every vkmerc shader named
above was either matched to an upstream, found to carry an in-file credit
to a third party, or — for exactly two of them — neither. Nothing in that
repository is copied from.

### What this means for PBR, plainly

The single most important thing in this index is a gap: **for the core PBR
and IBL shaders there is no approved code donor at all.** That is not a
problem, because those are the best-documented shaders in real-time
graphics — the GGX distribution, the Smith height-correlated geometry
term, the Schlick Fresnel approximation, the split-sum approximation and
its BRDF LUT are all published equations in papers and course notes, and
an equation carries no licence. They will be implemented here from those
publications, which is both legally clean and the reason to understand
them properly rather than paste them.

Primary published sources to implement from (papers and course notes, not
code):

- Walter et al., *Microfacet Models for Refraction through Rough
  Surfaces* (2007) — the GGX/Trowbridge-Reitz distribution.
- Karis, *Real Shading in Unreal Engine 4*, SIGGRAPH 2013 course notes —
  the split-sum approximation, the BRDF LUT, the `k` remapping.
- Heitz, *Understanding the Masking-Shadowing Function in Microfacet-Based
  BRDFs* (2014) — the height-correlated Smith term.
- Lagarde & de Rousiers, *Moving Frostbite to PBR* (2014) — the whole
  pipeline, and the energy-conservation details most implementations get
  wrong.
- Fdez-Agüera, *A Multiple-Scattering Microfacet Model for Real-Time
  Image-based Lighting* (JCGT 8(1), 2019) — the multiscatter energy
  compensation over the split-sum table.
- Hillaire, *Physically Based and Unified Volumetric Rendering in
  Frostbite* (2015) — the scattering integration the volumetric fog
  reference also points at.

---

## 3. Per-feature record

One entry per shader we write, added as it is written.

The shape of an entry:

```
Feature:            <what it does>
Started from:       <repository, or "published algorithm only">
Repository URL:     <url>
Licence:            <licence, and where it is stated>
Reference files:    <exact paths read>
Our implementation: <path in this repository>
What changed:       <what was rewritten, and why>
Licence doubt:      <none, or exactly what is uncertain>
```

<!-- entries go below this line -->

### Reflectance model — the microfacet BRDF

Every function below is in `Source/RenderCore/Shaders/Fluxion/BRDF.jsl`.
They are recorded one at a time rather than as a single "PBR" entry,
because each comes from a different paper and each can be read against
its own source without reading the rest.

```
Feature:            Microfacet distribution (GGX / Trowbridge-Reitz)
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    Walter, Marschner, Li, Torrance, "Microfacet Models
                    for Refraction through Rough Surfaces" (EGSR 2007),
                    equation 33
Our implementation: Source/RenderCore/Shaders/Fluxion/BRDF.jsl, D_GGX
What changed:       Written from the equation as printed. The guard on
                    the denominator is ours: the published form divides
                    by a quantity that reaches zero for a mirror seen
                    head-on, which is a real configuration rather than a
                    degenerate one.
Licence doubt:      none
```

```
Feature:            Masking-shadowing, height-correlated Smith
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    Heitz, "Understanding the Masking-Shadowing Function
                    in Microfacet-Based BRDFs" (JCGT 3(2), 2014),
                    section 6 (the height-correlated form)
Our implementation: Source/RenderCore/Shaders/Fluxion/BRDF.jsl,
                    V_SmithGGXCorrelated
What changed:       Written as a VISIBILITY term -- the paper's G divided
                    through by the 4*NoL*NoV that the microfacet model
                    would otherwise carry in its denominator. That is an
                    algebraic rearrangement of the published function, not
                    a different one.
Licence doubt:      none
```

```
Feature:            Fresnel
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    Schlick, "An Inexpensive BRDF Model for Physically-
                    based Rendering" (Computer Graphics Forum 13(3),
                    1994), section 3
Our implementation: Source/RenderCore/Shaders/Fluxion/BRDF.jsl, F_Schlick
What changed:       Nothing but the spelling. The fifth power is written
                    as multiplications rather than a pow call, which is
                    the same number.
Licence doubt:      none
```

```
Feature:            Diffuse
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    Lambert's cosine law; the 1/pi normalisation is the
                    standard consequence of integrating it over the
                    hemisphere.
Our implementation: Source/RenderCore/Shaders/Fluxion/BRDF.jsl, Fd_Lambert
What changed:       nothing
Licence doubt:      none
```

```
Feature:            Reflectance at normal incidence, from a material's
                    own values
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    Karis, "Real Shading in Unreal Engine 4" (SIGGRAPH
                    2013 course notes) for the metallic parameterisation;
                    the 0.16 * reflectance^2 mapping is the standard
                    remapping that puts the four percent of ordinary
                    dielectrics at a reflectance of one half.
Our implementation: Source/RenderCore/Shaders/Fluxion/BRDF.jsl,
                    ComputeF0 and ComputeDiffuseColor
What changed:       Written from the description. No code was read.
Licence doubt:      none
```

### Exposure and tone mapping

```
Feature:            Tone mapping (extended Reinhard)
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    Reinhard, Stark, Shirley, Ferwerda, "Photographic
                    Tone Reproduction for Digital Images" (SIGGRAPH
                    2002), equation 4
Our implementation: Source/RenderCore/Shaders/Fluxion/Tonemap.jsl,
                    TonemapReinhard
What changed:       Applied per channel rather than to the luminance, so
                    that a very bright colour drifts towards white as film
                    and eyes both do. A white point of zero or less is our
                    own addition and means do not tone map at all.
Licence doubt:      none
```

```
Feature:            Exposure from camera settings
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    The photographic exposure relation, which is
                    standard: EV100 = log2(N^2 / t * 100 / S). The 1.2
                    calibration between an incident reading and middle
                    grey is ISO 2720.
Our implementation: Source/RenderCore/Private/Renderer/Exposure.c
What changed:       nothing
Licence doubt:      none
```

### Punctual lights

```
Feature:            Distance attenuation with a range window
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    The inverse-square law, plus the windowing function
                    described in the physically based shading course
                    notes and in Frostbite's published lighting unit
                    write-up: a squared (1 - (d/r)^4)-style falloff that
                    arrives at exactly zero at the range.
Our implementation: Source/RenderCore/Shaders/Fluxion/Lighting.jsl,
                    DistanceAttenuation
What changed:       Written in terms of the SQUARED distance throughout,
                    so no square root is taken to compute a falloff that
                    then squares it again. A range of zero or less means
                    no window at all, which is our own addition.
Licence doubt:      none
```

```
Feature:            Spot cone attenuation
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    The standard inner/outer cone treatment: the cosine
                    of the angle to the axis, mapped across the two cone
                    cosines and smoothed.
Our implementation: Source/RenderCore/Shaders/Fluxion/Lighting.jsl,
                    SpotAttenuation
What changed:       The smoothing is written out as t*t*(3-2t) rather
                    than called through a builtin, so the same shape
                    reaches both target languages regardless of what each
                    one calls it. The cosines are taken on the CPU side.
Licence doubt:      none
```

```
Feature:            Diffuse irradiance as spherical harmonics
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    Ramamoorthi & Hanrahan, "An Efficient Representation
                    for Irradiance Environment Maps" (SIGGRAPH 2001):
                    the first three SH bands, their normalisation
                    constants, and the per-band cosine-lobe convolution
                    factors (pi, 2pi/3, pi/4). The per-texel solid angle
                    of a cube map is the standard closed form obtained by
                    integrating the differential solid angle over a cell
                    and evaluating at its four corners.
Our implementation: Source/RenderCore/Shaders/Fluxion/SphericalHarmonics.jsl
                    (evaluation, per surface),
                    Source/RenderCore/Shaders/Fluxion/Pass/IrradianceProject.jsl
                    (projection, compute, once per environment),
                    Source/RenderCore/Private/Renderer/Irradiance.cpp
                    (the dispatch around it)
What changed:       The nine terms are written out one by one rather than
                    looped over an array -- the shading language has no
                    arrays outside storage buffers. The projection gives
                    each coefficient to one thread, each walking the whole
                    sky over a fixed 64x64 grid per face, so there is no
                    reduction step; the paper does not prescribe a
                    parallel decomposition at all. The cosine convolution
                    is folded into the stored coefficients at projection
                    time, so evaluation is a bare dot product. The solid
                    angle's corner term uses the one-argument arctangent,
                    valid because its denominator sqrt(x^2+y^2+1) is
                    always positive.
Licence doubt:      none
```

```
Feature:            GGX importance sampling and the Hammersley sequence
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    Karis, "Real Shading in Unreal Engine 4" (SIGGRAPH
                    2013 course notes), the ImportanceSampleGGX
                    pseudocode's underlying inversion; the Hammersley
                    point set and the base-2 van der Corput radical
                    inverse are standard quasi-Monte-Carlo constructions
                    (Niederreiter, "Random Number Generation and
                    Quasi-Monte Carlo Methods", 1992).
Our implementation: Source/RenderCore/Shaders/Fluxion/ImportanceSampling.jsl
What changed:       The radical inverse is arithmetic -- a loop peeling
                    the lowest bit -- because the shading language has no
                    bit operations; the published forms use bitfield
                    reversal. The half-vector sampling takes the alpha
                    (squared) roughness directly, matching the
                    convention every distribution function in BRDF.jsl
                    already uses, where the course notes square inside.
Licence doubt:      none
```

```
Feature:            Prefiltered specular environment chain
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    Karis, "Real Shading in Unreal Engine 4" (SIGGRAPH
                    2013 course notes), the PrefilterEnvMap pseudocode:
                    the N = V = R approximation, cosine-weighted
                    accumulation, one roughness per mip. The vkmerc
                    prefilterenvmap.frag named in the index above is
                    study-only and was not read for this.
Our implementation: Source/RenderCore/Shaders/Fluxion/Pass/SpecularPrefilter.jsl
                    (the filter, compute, once per environment per mip),
                    Source/RenderCore/Private/Renderer/Prefilter.cpp
                    (the dispatches and the copies into the cube's mips)
What changed:       A compute pass writing a storage buffer that is then
                    copied into the cube map's mip faces, because compute
                    here writes buffers -- the published form is a
                    fragment shader rendering into each face. The output
                    rows and faces carry the copy-alignment padding in
                    their strides, handed to the shader as parameters.
Licence doubt:      none
```

```
Feature:            The split-sum DFG table
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    Karis, "Real Shading in Unreal Engine 4" (SIGGRAPH
                    2013 course notes), the IntegrateBRDF pseudocode: the
                    scale/bias split of Schlick's Fresnel, the
                    Schlick-GGX geometry term with the k = alpha/2
                    remapping for environment lighting. The vkmerc
                    genbrdflut.frag named in the index above is
                    study-only and was not read for this.
Our implementation: Source/RenderCore/Shaders/Fluxion/Pass/DfgIntegrate.jsl
                    (the integration, compute, once per view),
                    Source/RenderCore/Private/Renderer/Prefilter.cpp
                    (the dispatch and the copy into the table texture)
What changed:       Same buffer-then-copy shape as the prefilter above,
                    and the same arithmetic Hammersley. The table is
                    sampled half a texel in, so its edges hold averages
                    rather than the integrand's degenerate corners.
Licence doubt:      none
```

```
Feature:            Multiple-scattering energy compensation
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    Fdez-Aguera, "A Multiple-Scattering Microfacet Model
                    for Real-Time Image-based Lighting" (JCGT 8(1),
                    2019): the FssEss radiance term, the Ems = 1 - Ess
                    missed energy, the 1/21 hemisphere-averaged Schlick
                    Fresnel, and the coupled diffuse share. Kulla &
                    Conty, "Revisiting Physically Based Shading in
                    Imageworks" (SIGGRAPH 2017 course) for the direct
                    lighting compensation factor 1 + f0 * (1/Ess - 1).
Our implementation: Source/RenderCore/Shaders/Fluxion/Lighting.jsl,
                    EvaluateEnvironment (the environment side) and
                    EvaluateLighting (the direct factor);
                    Source/RenderCore/Shaders/Fluxion/BRDF.jsl,
                    BRDF_Direct (where the factor lands)
What changed:       Written from the papers' equations against the DFG
                    table above; the compensation reaches BRDF_Direct as
                    a parameter, so the reflectance model itself stays a
                    function of published terms and its caller owns the
                    table read.
Licence doubt:      none
```

### Shadows

```
Feature:            Light-space matrices for shadow maps
Started from:       published algorithm only
Repository URL:     none
Licence:            n/a -- no code was read or copied
Reference files:    Zhang, Sun, Xu, Lun, "Parallel-Split Shadow Maps for
                    Large-scale Virtual Environments" (VRCIA 2006) for
                    the blend of uniform and logarithmic cascade splits.
                    Valient, "Stable Rendering of Cascaded Shadow Maps"
                    (ShaderX6, 2008) for fitting a cascade to a bounding
                    SPHERE and snapping its centre to whole shadow-map
                    texels -- the two halves of why a shadow edge stops
                    crawling as the camera moves. The orthographic and
                    perspective forms themselves are the standard ones.
                    The vkmerc cascade.glsl/cascade_debugger.frag named
                    in the index above are study-only and were not read.
Our implementation: Source/RenderCore/Private/Renderer/ShadowMatrices.c
What changed:       This engine's own conventions throughout: depth to
                    -1..1, matrices row-major with v' = Mv, and a light
                    direction meaning THE WAY THE LIGHT TRAVELS -- so a
                    light's view axis is its negation. The texel snapping
                    is applied to the view matrix's translation rather
                    than by moving the eye in world space, which is the
                    same shift without building the frame twice.
Licence doubt:      none
```

