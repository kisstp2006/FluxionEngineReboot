#pragma once

// X-macro table of every GL 1.2+ entry point this backend loads at
// runtime via wglGetProcAddress/glXGetProcAddress -- GL 1.0/1.1 entry
// points (glEnable, glViewport, glClear, glGetIntegerv, glGetString, ...)
// are called directly since they're statically exported by
// opengl32.lib/libGL and don't need a proc-address lookup. Mirrors the
// idiom VulkanLoader-style tables use elsewhere in this codebase: one
// X(PFNTYPE, name) entry per function, expanded once for extern
// declarations (below) and once more for the load loop (OpenGLLoader.cpp).
#define FLUXION_GL_FUNCTIONS(X) \
    /* GL 1.0/1.1 core entry points (glEnable, glViewport, glClear,
       glGetIntegerv, glGetString, glDeleteTextures, ...) are deliberately
       NOT in this table -- they are real, statically-linkable exports of
       opengl32.lib/libGL (unlike everything below, which only exists
       behind a proc-address lookup), so declaring a same-named function
       POINTER for them would either shadow nothing usefully (Windows) or
       collide outright with the real declaration GL/gl.h provides
       transitively via GL/glx.h on Linux (this crashed the Linux build
       until the collision was found and these were pulled out of the
       load table). See OpenGLCommon.h for their explicit, minimal
       Windows-only forward declarations -- Linux already gets them for
       free via glx.h's own #include <GL/gl.h>. */ \
    /* Buffers (DSA, plus the one classic bind-based entry point this
       backend still needs for glDrawArraysIndirect's implicit
       GL_DRAW_INDIRECT_BUFFER binding -- glBindBuffer itself is GL 1.5,
       not statically exported by opengl32.dll, so it must be loaded like
       every other post-1.1 entry point here) */ \
    X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLCREATEBUFFERSPROC, glCreateBuffers) \
    X(PFNGLNAMEDBUFFERSTORAGEPROC, glNamedBufferStorage) \
    X(PFNGLNAMEDBUFFERSUBDATAPROC, glNamedBufferSubData) \
    X(PFNGLMAPNAMEDBUFFERRANGEPROC, glMapNamedBufferRange) \
    X(PFNGLUNMAPNAMEDBUFFERPROC, glUnmapNamedBuffer) \
    X(PFNGLCOPYNAMEDBUFFERSUBDATAPROC, glCopyNamedBufferSubData) \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    /* Textures (DSA) */ \
    X(PFNGLCREATETEXTURESPROC, glCreateTextures) \
    X(PFNGLTEXTURESTORAGE2DPROC, glTextureStorage2D) \
    X(PFNGLTEXTURESTORAGE3DPROC, glTextureStorage3D) \
    X(PFNGLTEXTURESUBIMAGE2DPROC, glTextureSubImage2D) \
    X(PFNGLTEXTURESUBIMAGE3DPROC, glTextureSubImage3D) \
    X(PFNGLTEXTUREVIEWPROC, glTextureView) \
    X(PFNGLBINDTEXTUREUNITPROC, glBindTextureUnit) \
    X(PFNGLGENERATETEXTUREMIPMAPPROC, glGenerateTextureMipmap) \
    X(PFNGLCOPYIMAGESUBDATAPROC, glCopyImageSubData) \
    /* Vertex array objects (DSA) */ \
    X(PFNGLCREATEVERTEXARRAYSPROC, glCreateVertexArrays) \
    X(PFNGLVERTEXARRAYATTRIBFORMATPROC, glVertexArrayAttribFormat) \
    X(PFNGLVERTEXARRAYATTRIBBINDINGPROC, glVertexArrayAttribBinding) \
    X(PFNGLENABLEVERTEXARRAYATTRIBPROC, glEnableVertexArrayAttrib) \
    X(PFNGLVERTEXARRAYVERTEXBUFFERPROC, glVertexArrayVertexBuffer) \
    X(PFNGLVERTEXARRAYELEMENTBUFFERPROC, glVertexArrayElementBuffer) \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) \
    /* Samplers (DSA) */ \
    X(PFNGLCREATESAMPLERSPROC, glCreateSamplers) \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri) \
    X(PFNGLSAMPLERPARAMETERFPROC, glSamplerParameterf) \
    X(PFNGLBINDSAMPLERPROC, glBindSampler) \
    X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers) \
    /* Framebuffers (DSA) */ \
    X(PFNGLCREATEFRAMEBUFFERSPROC, glCreateFramebuffers) \
    X(PFNGLNAMEDFRAMEBUFFERTEXTUREPROC, glNamedFramebufferTexture) \
    X(PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC, glNamedFramebufferDrawBuffers) \
    X(PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC, glCheckNamedFramebufferStatus) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLCLEARNAMEDFRAMEBUFFERFVPROC, glClearNamedFramebufferfv) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    /* Shaders / programs */ \
    X(PFNGLCREATESHADERPROC, glCreateShader) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLDETACHSHADERPROC, glDetachShader) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLGETPROGRAMBINARYPROC, glGetProgramBinary) \
    X(PFNGLPROGRAMBINARYPROC, glProgramBinary) \
    X(PFNGLPROGRAMPARAMETERIPROC, glProgramParameteri) \
    /* Binding points */ \
    X(PFNGLBINDBUFFERBASEPROC, glBindBufferBase) \
    X(PFNGLBINDBUFFERRANGEPROC, glBindBufferRange) \
    /* Draw / dispatch */ \
    X(PFNGLDRAWARRAYSINSTANCEDBASEINSTANCEPROC, glDrawArraysInstancedBaseInstance) \
    X(PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXBASEINSTANCEPROC, glDrawElementsInstancedBaseVertexBaseInstance) \
    X(PFNGLDRAWARRAYSINDIRECTPROC, glDrawArraysIndirect) \
    X(PFNGLDRAWELEMENTSINDIRECTPROC, glDrawElementsIndirect) \
    X(PFNGLDISPATCHCOMPUTEPROC, glDispatchCompute) \
    X(PFNGLMEMORYBARRIERPROC, glMemoryBarrier) \
    /* Queries */ \
    X(PFNGLGENQUERIESPROC, glGenQueries) \
    X(PFNGLDELETEQUERIESPROC, glDeleteQueries) \
    /* Sync */ \
    X(PFNGLFENCESYNCPROC, glFenceSync) \
    X(PFNGLCLIENTWAITSYNCPROC, glClientWaitSync) \
    X(PFNGLDELETESYNCPROC, glDeleteSync) \
    /* Debug */ \
    X(PFNGLDEBUGMESSAGECALLBACKPROC, glDebugMessageCallback) \
    /* Misc / extension query (core-profile glGetString(GL_EXTENSIONS) is invalid; use glGetStringi) */ \
    X(PFNGLGETSTRINGIPROC, glGetStringi) \
    X(PFNGLGETINTEGERI_VPROC, glGetIntegeri_v)

#define FLUXION_GL_DECLARE(type, name) extern type name;
FLUXION_GL_FUNCTIONS(FLUXION_GL_DECLARE)
#undef FLUXION_GL_DECLARE
