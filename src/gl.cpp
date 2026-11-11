#define GLAD_API_CALL
#include "gl.hpp"

struct CastableFuncPtr {
    void* ptr;

    template <typename T>
    operator T() {
        return (T)ptr;
    }
};

static bool loaded = false;

void* glGetProcAddressRaw(const char* name) {
#ifdef GEODE_IS_WINDOWS
    return (void*)wglGetProcAddress(name);
#elif defined(GEODE_IS_ANDROID)
    return (void*)eglGetProcAddress(name);
#else
    return nullptr;
#endif
}

inline CastableFuncPtr glGetProcAddress(const char* name) {
    return (CastableFuncPtr)glGetProcAddressRaw(name);
}

bool hasLoadedNewerOpenGLFunctions() {
    return loaded;
}

void loadNewerOpenGLFunctions() {
    glMultiDrawElementsIndirect = glGetProcAddress("glMultiDrawElementsIndirect");
    if (!glMultiDrawElementsIndirect)
        glMultiDrawElementsIndirect = glGetProcAddress("glMultiDrawElementsIndirectEXT");

    glMultiDrawArraysIndirect = glGetProcAddress("glMultiDrawArraysIndirect");
    if (!glMultiDrawArraysIndirect)
        glMultiDrawArraysIndirect = glGetProcAddress("glMultiDrawArraysIndirectEXT");

    b_glDrawArraysIndirect = glGetProcAddress("glDrawArraysIndirect");
    glDispatchCompute = glGetProcAddress("glDispatchCompute");
    b_glMemoryBarrier = glGetProcAddress("glMemoryBarrier");
    glSpecializeShader = glGetProcAddress("glSpecializeShader");
    glGetProgramResourceIndex = glGetProcAddress("glGetProgramResourceIndex");
    glShaderStorageBlockBinding = glGetProcAddress("glShaderStorageBlockBinding");

    loaded = true;
}

static GLint storedVAO = 0;

void storeVAO() {
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &storedVAO);
}

void restoreVAO() {
    glBindVertexArray(storedVAO);
}
