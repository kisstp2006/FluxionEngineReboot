# Fluxion Engine

Open source version of the Fluxion Engine.

Written in C and C++ (C23 / C++23).

## Why open source

The original, closed-source Fluxion had grown too large to keep patching,
refactoring, or restructuring in place. Rather than fight that codebase, it
is being rewritten from scratch, carrying over what still makes sense from
the original (everything written by me), and released under a new,
permissive license that puts as few restrictions as possible on who can use
the engine and how.

Unlike the original, which was written mostly in C#, this version is
written in C and C++ — to natively support more platforms with as few
wrappers as possible, to support at least the scenes of the original
Fluxion, and to be faster than the current C#/C++ version. Its architecture
and extensibility model draw ideas from established AAA engines.
