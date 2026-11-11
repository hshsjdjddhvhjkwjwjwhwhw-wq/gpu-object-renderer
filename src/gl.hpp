#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/platform/CCGL.h>

#ifdef GEODE_IS_ANDROID
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>
#endif

/*
    This file adds the required OpenGL 4.3 / OpenGL ES 3.1 functions
    and cross-platform definitions for Windows and Android.
*/

#ifndef GL_BUFFER
#define GL_BUFFER 0x82E0
#endif
#ifndef GL_SHADER
#define GL_SHADER 0x82E1
#endif
#ifndef GL_PROGRAM
#define GL_PROGRAM 0x82E2
#endif
#ifndef GL_SHADER_BINARY_FORMAT_SPIR_V
#define GL_SHADER_BINARY_FORMAT_SPIR_V 0x9551
#endif
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_SHADER_STORAGE_BLOCK
#define GL_SHADER_STORAGE_BLOCK 0x92E6
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_MAX_SHADER_STORAGE_BLOCK_SIZE
#define GL_MAX_SHADER_STORAGE_BLOCK_SIZE 0x90DE
#endif
#ifndef GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS
#define GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS 0x90DD
#endif
#ifndef GL_DRAW_INDIRECT_BUFFER
#define GL_DRAW_INDIRECT_BUFFER 0x8F3F
#endif
#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif
#ifndef GL_COMMAND_BARRIER_BIT
#define GL_COMMAND_BARRIER_BIT 0x00000040
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif
#ifndef GL_READ_WRITE
#define GL_READ_WRITE 0x88BA
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#endif
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_MAP_FLUSH_EXPLICIT_BIT
#define GL_MAP_FLUSH_EXPLICIT_BIT 0x0010
#endif

#ifndef GLAD_API_CALL
#define GLAD_API_CALL extern
#endif

typedef void (*PFNGLMULTIDRAWELEMENTSINDIRECTPROC)(GLenum mode, GLenum type, const void * indirect, GLsizei drawcount, GLsizei stride);
typedef void (*PFNGLMULTIDRAWARRAYSINDIRECTPROC)(GLenum mode, const void * indirect, GLsizei drawcount, GLsizei stride);
typedef void (*PFNGLDRAWARRAYSINDIRECTPROC)(GLenum mode, const void * indirect);
typedef void (*PFNGLDISPATCHCOMPUTEPROC)(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
typedef void (*PFNGLMEMORYBARRIERPROC)(GLbitfield barriers);
typedef void (*PFNGLSPECIALIZESHADERPROC)(GLuint shader, const GLchar *pEntryPoint, GLuint numSpecializationConstants, const GLuint *pConstantIndex, const GLuint *pConstantValue);
typedef GLuint (*PFNGLGETPROGRAMRESOURCEINDEXPROC)(GLuint program, GLenum programInterface, const GLchar * name);
typedef void (*PFNGLSHADERSTORAGEBLOCKBINDINGPROC)(GLuint program, GLuint storageBlockIndex, GLuint storageBlockBinding);

GLAD_API_CALL PFNGLMULTIDRAWELEMENTSINDIRECTPROC glad_glMultiDrawElementsIndirect;
#define glMultiDrawElementsIndirect glad_glMultiDrawElementsIndirect
GLAD_API_CALL PFNGLMULTIDRAWARRAYSINDIRECTPROC glad_glMultiDrawArraysIndirect;
#define glMultiDrawArraysIndirect glad_glMultiDrawArraysIndirect
GLAD_API_CALL PFNGLDRAWARRAYSINDIRECTPROC glad_glDrawArraysIndirect;
#define b_glDrawArraysIndirect glad_glDrawArraysIndirect
GLAD_API_CALL PFNGLDISPATCHCOMPUTEPROC glad_glDispatchCompute;
#define glDispatchCompute glad_glDispatchCompute
GLAD_API_CALL PFNGLMEMORYBARRIERPROC glad_glMemoryBarrier;
#define b_glMemoryBarrier glad_glMemoryBarrier
GLAD_API_CALL PFNGLSPECIALIZESHADERPROC glad_glSpecializeShader;
#define glSpecializeShader glad_glSpecializeShader
GLAD_API_CALL PFNGLGETPROGRAMRESOURCEINDEXPROC glad_glGetProgramResourceIndex;
#define glGetProgramResourceIndex glad_glGetProgramResourceIndex
GLAD_API_CALL PFNGLSHADERSTORAGEBLOCKBINDINGPROC glad_glShaderStorageBlockBinding;
#define glShaderStorageBlockBinding glad_glShaderStorageBlockBinding

void* glGetProcAddressRaw(const char* name);
bool hasLoadedNewerOpenGLFunctions();
void loadNewerOpenGLFunctions();

void storeVAO();
void restoreVAO();
