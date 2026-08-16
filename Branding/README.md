# Branding

The Fluxion Engine mark, in the two arrangements it exists in.

| File | Use |
|---|---|
| [`Fluxion-Logo.png`](Fluxion-Logo.png) | **The default.** Horizontal: mark and wordmark side by side, 2172×724. What to reach for unless there is a reason not to. |
| [`Fluxion-Logo-Stacked.png`](Fluxion-Logo-Stacked.png) | The variation. Square: mark above wordmark, 1254×1254. For where a wide image does not fit — an avatar, an icon, a narrow column. |

## Light and dark

The artwork is black on a transparent background, so it disappears against a
dark backdrop. Each arrangement therefore has a second file for that case:

| File | Use |
|---|---|
| [`Fluxion-Logo-OnDark.png`](Fluxion-Logo-OnDark.png) | The default, for dark backgrounds |
| [`Fluxion-Logo-Stacked-OnDark.png`](Fluxion-Logo-Stacked-OnDark.png) | The variation, for dark backgrounds |

The `-OnDark` pair was **produced mechanically from the black originals** by
inverting the three colour channels and leaving alpha untouched, so the shape,
the soft edges and the shading on the facets of the F are all exactly as they
were. It is a knockout of the original, not a redraw — if a hand-made light
version ever exists, replacing these two files is the whole of the change.

The root [`README.md`](../README.md) picks between the two with a `<picture>`
element on `prefers-color-scheme`, which is how the logo follows the reader's
theme rather than fighting it.

## Attribution

`Fluxion-Logo.png` is the "Graphic Image" named in Exhibit B of
[`license.md`](../license.md), which is what makes it part of the engine's
attribution information under Section 14 of the CPAL.

## What the engine's license does not give you

The code is under CPAL-1.0. **The mark is not part of that grant.** CPAL
Section 14(d) says so in as many words: trademarks, service marks and trade
names appearing in Attribution Information stay the exclusive property of their
owners and may only be used with permission or where the law otherwise allows.

In practice: reproducing the logo to *give attribution*, to say that a program
was built with Fluxion Engine, or to link back to the project is what these
files are for. Putting it on something of your own so that it reads as
Fluxion's, or as endorsed by it, is not.
