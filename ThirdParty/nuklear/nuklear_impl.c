// Nuklear (https://github.com/Immediate-Mode-UI/Nuklear) — vendored,
// unmodified, dual licensed public domain / MIT (see LICENSE beside this).
//
// THE ONE PLACE ITS IMPLEMENTATION IS COMPILED. The header is both the
// interface and the body; whoever defines NK_IMPLEMENTATION gets the
// body, and defining it twice is a link error rather than a warning.
//
// The switches below are what this engine needs and nothing more:
//   FIXED_TYPES     -- sizes stated rather than guessed from the platform
//   DEFAULT_ALLOCATOR / STANDARD_IO not asked for: the UI module hands it
//                      memory and never touches a file
//   FONT_BAKING + DEFAULT_FONT -- a usable font with no asset to ship,
//                      which is what a developer panel wants
//   VERTEX_BUFFER_OUTPUT -- draw commands as vertices and indices, which
//                      is the only shape this engine's RHI can draw

#define NK_IMPLEMENTATION
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT

#include "nuklear.h"
