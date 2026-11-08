#pragma once
#include <Geode/Geode.hpp>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Plataforma: SOLO el cargador de funciones depende del #ifdef. Todo lo demás
// (shaders, ruta de dibujo) se decide en tiempo de ejecución según GL_VERSION
// ("OpenGL ES 3.1+" => ruta ES, "4.3+" => ruta de escritorio).
// ---------------------------------------------------------------------------
#if defined(__ANDROID__)
    #define GPUR_GLES_BUILD 1
    // VERIFY: los headers de cocos de Geode ya suelen incluir GLES2/gl2.h en Android;
    // se incluye aquí por si acaso (los include guards evitan duplicados).
    #include <GLES2/gl2.h>
    #include <EGL/egl.h>
    #include <dlfcn.h>
    #define GPUR_GLCALL
#elif defined(_WIN32)
    #define GPUR_GLES_BUILD 0
    #define GPUR_GLCALL APIENTRY
    // winnt.h define MemoryBarrier como macro y chocaría con el puntero de función
    #ifdef MemoryBarrier
    #undef MemoryBarrier
    #endif
#else
    #error "GPU Object Renderer solo soporta Windows y Android"
#endif

// GD usa un GLEW viejo (Windows) / solo GLES2 (Android): cargamos nosotros lo que necesitamos.
// Los campos NO llevan prefijo "gl", así que no chocan con los macros de GLEW.
//
// Funciones que existen tanto en OpenGL 4.3 como en OpenGL ES 3.1:
#define GPUR_GL_LIST_COMMON(X)                                                               \
    X(DispatchCompute,          void,   (GLuint, GLuint, GLuint))                            \
    X(MemoryBarrier,            void,   (GLbitfield))                                        \
    X(BindBufferBase,           void,   (GLenum, GLuint, GLuint))                            \
    X(GenBuffers,               void,   (GLsizei, GLuint*))                                  \
    X(DeleteBuffers,            void,   (GLsizei, const GLuint*))                            \
    X(BindBuffer,               void,   (GLenum, GLuint))                                    \
    X(BufferData,               void,   (GLenum, ptrdiff_t, const void*, GLenum))            \
    X(BufferSubData,            void,   (GLenum, ptrdiff_t, ptrdiff_t, const void*))         \
    X(CreateShader,             GLuint, (GLenum))                                            \
    X(ShaderSource,             void,   (GLuint, GLsizei, const char* const*, const GLint*)) \
    X(CompileShader,            void,   (GLuint))                                            \
    X(GetShaderiv,              void,   (GLuint, GLenum, GLint*))                            \
    X(GetShaderInfoLog,         void,   (GLuint, GLsizei, GLsizei*, char*))                  \
    X(DeleteShader,             void,   (GLuint))                                            \
    X(CreateProgram,            GLuint, ())                                                  \
    X(DeleteProgram,            void,   (GLuint))                                            \
    X(AttachShader,             void,   (GLuint, GLuint))                                    \
    X(LinkProgram,              void,   (GLuint))                                            \
    X(GetProgramiv,             void,   (GLuint, GLenum, GLint*))                            \
    X(GetProgramInfoLog,        void,   (GLuint, GLsizei, GLsizei*, char*))                  \
    X(UseProgram,               void,   (GLuint))                                            \
    X(GetUniformLocation,       GLint,  (GLuint, const char*))                               \
    X(Uniform1i,                void,   (GLint, GLint))                                      \
    X(Uniform1ui,               void,   (GLint, GLuint))                                     \
    X(Uniform1f,                void,   (GLint, GLfloat))                                    \
    X(Uniform4f,                void,   (GLint, GLfloat, GLfloat, GLfloat, GLfloat))         \
    X(UniformMatrix4fv,         void,   (GLint, GLsizei, GLboolean, const GLfloat*))         \
    X(GenVertexArrays,          void,   (GLsizei, GLuint*))                                  \
    X(DeleteVertexArrays,       void,   (GLsizei, const GLuint*))                            \
    X(BindVertexArray,          void,   (GLuint))                                            \
    X(EnableVertexAttribArray,  void,   (GLuint))                                            \
    X(VertexAttribPointer,      void,   (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(VertexAttribDivisor,      void,   (GLuint, GLuint))                                    \
    X(DrawElementsIndirect,     void,   (GLenum, GLenum, const void*))                       \
    X(BindVertexBuffer,         void,   (GLuint, GLuint, ptrdiff_t, GLsizei))                \
    X(VertexAttribFormat,       void,   (GLuint, GLint, GLenum, GLboolean, GLuint))          \
    X(VertexAttribBinding,      void,   (GLuint, GLuint))                                    \
    X(VertexBindingDivisor,     void,   (GLuint, GLuint))                                    \
    X(GetIntegeri_v,            void,   (GLenum, GLuint, GLint*))                            \
    X(ActiveTexture,            void,   (GLenum))

// Solo en OpenGL de escritorio. OpenGL ES 3.1 NO tiene glMultiDrawElementsIndirect.
#define GPUR_GL_LIST_DESKTOP(X)                                                              \
    X(MultiDrawElementsIndirect, void,  (GLenum, GLenum, const void*, GLsizei, GLsizei))

namespace gpur::gl {

constexpr GLenum kShaderStorageBuffer = 0x90D2, kDrawIndirectBuffer = 0x8F3F;
constexpr GLenum kArrayBuffer = 0x8892, kElementArrayBuffer = 0x8893;
constexpr GLenum kDynamicDraw = 0x88E8, kStaticDraw = 0x88E4;
constexpr GLenum kComputeShader = 0x91B9, kVertexShader = 0x8B31, kFragmentShader = 0x8B30;
constexpr GLenum kCompileStatus = 0x8B81, kLinkStatus = 0x8B82, kTexture0 = 0x84C0;

// glMemoryBarrier
constexpr GLbitfield kVertexAttribArrayBarrier = 0x1, kCommandBarrier = 0x40,
                     kShaderStorageBarrier = 0x2000;
// Escritorio: el compute escribe atributos instanciados + draw commands.
constexpr GLbitfield kPostComputeBarrierDesktop = kVertexAttribArrayBarrier | kCommandBarrier;
// ES (v0.2.1): igual que escritorio. El raw sprite buffer se lee como atributos instanciados
// (muchos drivers ES, p. ej. Mali, dan 0 SSBO en el vertex shader).
constexpr GLbitfield kPostComputeBarrierEs = kVertexAttribArrayBarrier | kCommandBarrier;

// glGetIntegerv / glGetIntegeri_v (mismos valores en GL 4.3 y ES 3.1)
constexpr GLenum kMaxComputeWorkGroupInvocations = 0x90EB, kMaxComputeWorkGroupCount = 0x91BE,
                 kMaxComputeWorkGroupSize = 0x91BF, kMaxComputeSharedMemory = 0x8262,
                 kMaxVertexSsboBlocks = 0x90D6, kMaxComputeSsboBlocks = 0x90DB,
                 kMaxCombinedSsboBlocks = 0x90DC, kMaxSsboBindings = 0x90DD,
                 kMaxSsboBlockSize = 0x90DE;

struct Fns {
#define X(name, ret, args) ret(GPUR_GLCALL* name) args = nullptr;
    GPUR_GL_LIST_COMMON(X)
    GPUR_GL_LIST_DESKTOP(X)
#undef X
};
inline Fns fn;

// Límites del driver, consultados una vez en load().
struct Limits {
    std::string version;
    bool es = false;
    int major = 0, minor = 0;
    int wgInvocations = 0;      // GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS (mínimo garantizado en ES 3.1: 128)
    int wgSizeX = 0;            // GL_MAX_COMPUTE_WORK_GROUP_SIZE[0]     (mínimo en ES 3.1: 128)
    int wgCountX = 0;           // GL_MAX_COMPUTE_WORK_GROUP_COUNT[0]    (mínimo: 65535)
    int sharedMem = 0;          // GL_MAX_COMPUTE_SHARED_MEMORY_SIZE     (mínimo en ES 3.1: 16 KB)
    int computeSsbo = 0;        // GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS  (mínimo en ES 3.1: 4)
    int vertexSsbo = 0;         // GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS   (mínimo en ES 3.1: 0 !)
    int combinedSsbo = 0;       // GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS (mínimo en ES 3.1: 4)
    int ssboBindings = 0;       // GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS (mínimo en ES 3.1: 4)
    uint32_t ssboBlockSize = 0; // GL_MAX_SHADER_STORAGE_BLOCK_SIZE (se lee como GLint y se reinterpreta:
                                // algunos drivers devuelven 4294967295, que llega como -1)
};
inline Limits limits;
inline std::string loadError;

inline void* procAddress(const char* name) {
#if GPUR_GLES_BUILD
    // Android: eglGetProcAddress; si no devuelve nada, se prueba directamente en libGLESv3.
    void* p = reinterpret_cast<void*>(eglGetProcAddress(name));
    if (p) return p;
    static void* lib = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
    return lib ? dlsym(lib, name) : nullptr;
#else
    return reinterpret_cast<void*>(wglGetProcAddress(name));
#endif
}

inline void clearErrors() {
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {}
}

// Para el diagnóstico de los primeros frames de cada nivel (no se llama cada frame:
// glGetError puede forzar una sincronización con el driver).
inline void checkError(const char* where) {
    for (int i = 0; i < 8; ++i) {
        GLenum e = glGetError();
        if (e == GL_NO_ERROR) break;
        geode::log::warn("[gpu-renderer] error GL 0x{:X} tras: {}", e, where);
    }
}

inline void queryLimits() {
    Limits& L = limits;
    GLint v = 0;
    glGetIntegerv(kMaxComputeWorkGroupInvocations, &v);  L.wgInvocations = v;
    fn.GetIntegeri_v(kMaxComputeWorkGroupSize, 0, &v);   L.wgSizeX = v;
    fn.GetIntegeri_v(kMaxComputeWorkGroupCount, 0, &v);  L.wgCountX = v;
    glGetIntegerv(kMaxComputeSharedMemory, &v);          L.sharedMem = v;
    glGetIntegerv(kMaxComputeSsboBlocks, &v);            L.computeSsbo = v;
    glGetIntegerv(kMaxVertexSsboBlocks, &v);             L.vertexSsbo = v;
    glGetIntegerv(kMaxCombinedSsboBlocks, &v);           L.combinedSsbo = v;
    glGetIntegerv(kMaxSsboBindings, &v);                 L.ssboBindings = v;
    glGetIntegerv(kMaxSsboBlockSize, &v);                L.ssboBlockSize = uint32_t(v);
    clearErrors();
}

// true si hay OpenGL >= 4.3 / OpenGL ES >= 3.1 y se pudieron cargar todas las funciones.
// Si devuelve false, loadError dice por qué.
inline bool load() {
    static int state = 0;  // 0 sin probar, 1 ok, -1 fallo definitivo
    if (state) return state > 0;

    auto fail = [&](std::string why) {
        loadError = std::move(why);
        state = -1;
        return false;
    };

    const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (!ver) {  // no hay contexto activo: no se cachea el fallo
        loadError = "glGetString(GL_VERSION) devolvió null (sin contexto GL activo)";
        return false;
    }

    Limits& L = limits;
    L.version = ver;
    // Android: "OpenGL ES 3.2 V@..." ; Windows: "4.6.0 NVIDIA ..."
    L.es = std::strstr(ver, "OpenGL ES") != nullptr;
    const char* p = ver;
    while (*p && (*p < '0' || *p > '9')) ++p;
    if (std::sscanf(p, "%d.%d", &L.major, &L.minor) != 2)
        return fail("no se pudo interpretar GL_VERSION: " + L.version);

    const bool ok = L.es ? (L.major > 3 || (L.major == 3 && L.minor >= 1))
                         : (L.major > 4 || (L.major == 4 && L.minor >= 3));
    if (!ok) {
        return fail(std::string(L.es ? "se necesita OpenGL ES 3.1 o superior" : "se necesita OpenGL 4.3 o superior") +
                    " (este contexto es: " + L.version + ")");
    }

#define X(name, ret, args)                                                                   \
    fn.name = reinterpret_cast<decltype(fn.name)>(procAddress("gl" #name));                  \
    if (!fn.name) return fail(std::string("no se pudo cargar gl") + #name);
    GPUR_GL_LIST_COMMON(X)
    if (!L.es) { GPUR_GL_LIST_DESKTOP(X) }
#undef X

    queryLimits();
    state = 1;
    return true;
}

}  // namespace gpur::gl
