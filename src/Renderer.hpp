#pragma once
#include "GLFuncs.hpp"
#include "Types.hpp"
#include <Geode/Geode.hpp>
#include <memory>
#include <string>
#include <unordered_map>

// Definida en main.cpp, en el namespace GLOBAL (por eso se declara fuera de gpur).
cocos2d::CCNode* gpurMakeLayerNode(int zLayer);

namespace gpur {

// Tramo consecutivo del orden de dibujo con el mismo ZLayer, textura y blending.
//  - Escritorio: se dibuja con UN glMultiDrawElementsIndirect (un draw command por work group).
//  - ES 3.1 (Android): no hay multi-draw ni baseInstance; se dibuja con UN glDrawElementsIndirect por
//    work group (moviendo el offset del buffer de atributos con glBindVertexBuffer), pero con
//    textura y blending fijados UNA vez por run.
struct Run {
    int      zLayer;
    uint32_t texture;
    bool     blend;
    uint32_t firstGroup;
    uint32_t groupCount;
};

// Lote ordenado (sección 2.1): depende del estado de blending de los canales.
// Se guarda en caché para no regenerarlo si el blending vuelve a un estado ya visto.
struct SortedBatch {
    GLuint orderBuf = 0;   // slot -> índice de sprite
    GLuint groupBuf = 0;   // GpuWorkGroup[]
    GLuint cmdBuf = 0;     // draw commands (los escribe el compute shader)
    uint32_t groupCount = 0;
    std::vector<Run> runs;
    std::vector<uint32_t> outBase;  // por work group: primer sprite suyo en el raw sprite buffer (ruta ES)
    ~SortedBatch();
};

class Renderer {
public:
    static Renderer& get();

    bool active() const { return m_active; }

    void onLevelLoaded(GJBaseGameLayer* layer);
    void onLevelUnloaded();

    // Una vez por frame (el primer nodo de capa que se dibuja la dispara).
    void beginFrameOnce(const kmMat4& mvp);
    void drawLayer(int zLayer, const kmMat4& mvp);

private:
    bool initGL();
    bool checkLimits();
    bool contextChanged() const;
    void abandonGL();
    void releaseLevel();
    void beginFrame(const kmMat4& mvp);
    void updateCombos();
    SortedBatch* batchFor(uint64_t signature);
    void build(SortedBatch& batch);

    bool m_ready = false;
    bool m_initFailed = false;     // límites/shaders no válidos: no se reintenta en cada nivel
    bool m_active = false;
    bool m_es = false;             // true => ruta OpenGL ES 3.1 (Android); false => OpenGL 4.3 (Windows)
    bool m_contextLost = false;    // el contexto EGL cambió: los ids GL guardados ya no valen
    void* m_eglContext = nullptr;  // solo Android: contexto con el que se crearon los objetos GL
    uint32_t m_tile = kTileSize;   // local_size_x real (potencia de 2, limitado por el driver)
    int m_diagFrames = 0;          // frames de diagnóstico restantes (se comprueba glGetError)
    GJBaseGameLayer* m_layer = nullptr;
    LevelData m_level;

    GLuint m_csProg = 0, m_drawProg = 0;
    GLint m_uView = -1, m_uTime = -1, m_uAudio = -1, m_uMvp = -1, m_uTex = -1;
    GLuint m_quadVbo = 0, m_quadIbo = 0, m_vao = 0;
    GLuint m_spriteBuf = 0, m_comboBuf = 0, m_colorBuf = 0, m_rawBuf = 0;

    std::vector<GpuCombo> m_combos;
    std::vector<float> m_colors;                 // 1101 * rgba
    std::bitset<kColorChannels> m_blend;         // blending actual por canal
    std::vector<uint16_t> m_usedChannels;        // canales que usan sprites base/detalle

    uint64_t m_signature = 0;
    SortedBatch* m_current = nullptr;
    std::unordered_map<uint64_t, std::unique_ptr<SortedBatch>> m_cache;

    int m_frame = -1;
    double m_startTime = 0;
    std::vector<geode::Ref<cocos2d::CCNode>> m_nodes;
};

}  // namespace gpur
