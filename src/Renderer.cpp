#include "Renderer.hpp"
#include "GDAdapter.hpp"
#include "Shaders.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <numeric>
#include <set>
#include <tuple>

using namespace geode::prelude;

namespace gpur {
namespace {

// Blend funcs de los batches vanilla. VERIFY con CCSprite::getBlendFunc() de un sprite normal y uno aditivo.
constexpr GLenum kNormalSrc = GL_ONE,       kNormalDst = GL_ONE_MINUS_SRC_ALPHA;
constexpr GLenum kBlendSrc  = GL_SRC_ALPHA, kBlendDst  = GL_ONE;

// Binding SSBO del raw sprite buffer en el vertex shader ES: debe coincidir con "binding = 5" de Shaders.hpp.
constexpr GLuint kRawBinding = 5;
// SSBOs que usa el compute shader a la vez (bindings 0..6).
constexpr int kNeededSsbos = 7;

GLuint makeBuffer(size_t bytes, const void* data, GLenum usage = gl::kDynamicDraw) {
    auto& f = gl::fn;
    GLuint id = 0;
    f.GenBuffers(1, &id);
    f.BindBuffer(gl::kShaderStorageBuffer, id);
    f.BufferData(gl::kShaderStorageBuffer, ptrdiff_t(bytes), data, usage);
    f.BindBuffer(gl::kShaderStorageBuffer, 0);
    return id;
}

void updateBuffer(GLuint id, size_t bytes, const void* data) {
    auto& f = gl::fn;
    f.BindBuffer(gl::kShaderStorageBuffer, id);
    f.BufferSubData(gl::kShaderStorageBuffer, 0, ptrdiff_t(bytes), data);
    f.BindBuffer(gl::kShaderStorageBuffer, 0);
}

void deleteBuffer(GLuint& id) {
    if (id) gl::fn.DeleteBuffers(1, &id);
    id = 0;
}

GLuint compile(GLenum type, const std::string& src) {
    auto& f = gl::fn;
    GLuint s = f.CreateShader(type);
    const char* p = src.c_str();
    f.ShaderSource(s, 1, &p, nullptr);
    f.CompileShader(s);
    GLint ok = 0;
    f.GetShaderiv(s, gl::kCompileStatus, &ok);
    if (!ok) {
        char buf[2048];
        f.GetShaderInfoLog(s, sizeof(buf), nullptr, buf);
        log::error("[gpu-renderer] error de shader: {}", buf);
        f.DeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(std::initializer_list<GLuint> shaders) {
    auto& f = gl::fn;
    bool bad = false;
    for (auto s : shaders) if (!s) bad = true;
    if (bad) {
        for (auto s : shaders) if (s) f.DeleteShader(s);
        return 0;
    }
    GLuint p = f.CreateProgram();
    for (auto s : shaders) f.AttachShader(p, s);
    f.LinkProgram(p);
    GLint ok = 0;
    f.GetProgramiv(p, gl::kLinkStatus, &ok);
    for (auto s : shaders) f.DeleteShader(s);
    if (!ok) {
        char buf[2048];
        f.GetProgramInfoLog(p, sizeof(buf), nullptr, buf);
        log::error("[gpu-renderer] error al enlazar: {}", buf);
        f.DeleteProgram(p);
        return 0;
    }
    return p;
}

uint64_t fnv(uint64_t h, uint64_t v) {
    h ^= v;
    return h * 1099511628211ull;
}

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Deja el estado GL/cocos coherente tras dibujar nosotros.
// VERIFY: en cocos2d-x 2.2.x, ccGLInvalidateStateCache() parece llamar también a kmGLFreeAll()
// (vacía las pilas de matrices). Si tras el primer nodo de capa los objetos/HUD aparecen mal
// colocados o desaparecen, esta es la primera sospechosa: habría que sustituirla por llamadas
// individuales (ccGLUseProgram, ccGLBindTexture2D, ccGLBlendFunc) que mantienen la caché coherente.
void restoreCocosState() {
    ccGLInvalidateStateCache();  // cocos cachea programa/textura/blend: hay que invalidarlo
}

}  // namespace

SortedBatch::~SortedBatch() {
    deleteBuffer(orderBuf);
    deleteBuffer(groupBuf);
    deleteBuffer(cmdBuf);
}

Renderer& Renderer::get() {
    static Renderer r;
    return r;
}

// ---------------------------------------------------------------------------
// Límites del driver (ES 3.1 garantiza muy poco: ver comentarios en GLFuncs.hpp)
// Devuelve false (y lo dice en el log) si el mod no puede funcionar en este dispositivo.
// ---------------------------------------------------------------------------
bool Renderer::checkLimits() {
    const auto& L = gl::limits;
    log::info("[gpu-renderer] {} | compute: {} invocaciones, size.x {}, count.x {}, shared {} B | "
              "SSBO: compute {}, vertex {}, combined {}, bindings {}, bloque max {} MB",
              L.version, L.wgInvocations, L.wgSizeX, L.wgCountX, L.sharedMem, L.computeSsbo,
              L.vertexSsbo, L.combinedSsbo, L.ssboBindings, L.ssboBlockSize / (1024u * 1024u));

    // local_size_x: potencia de 2 (el scan del compute shader lo necesita) <= límites del driver
    const uint32_t cap = uint32_t(std::max(0, std::min(L.wgInvocations, L.wgSizeX)));
    uint32_t tile = 1;
    while (tile * 2 <= std::min(cap, kTileSize)) tile *= 2;
    if (tile < 64) {
        log::warn("[gpu-renderer] el driver solo permite {} hilos por work group: se usa el render normal de GD", cap);
        return false;
    }
    if (tile < kTileSize)
        log::warn("[gpu-renderer] local_size reducido a {} (el driver permite {})", tile, cap);

    if (L.sharedMem < int(tile * 4 + 16)) {
        log::warn("[gpu-renderer] memoria compartida insuficiente ({} B): se usa el render normal de GD", L.sharedMem);
        return false;
    }
    if (L.computeSsbo < kNeededSsbos || L.combinedSsbo < kNeededSsbos || L.ssboBindings < kNeededSsbos) {
        log::warn("[gpu-renderer] se necesitan {} SSBO a la vez y el driver da compute={}, combined={}, bindings={}: "
                  "se usa el render normal de GD", kNeededSsbos, L.computeSsbo, L.combinedSsbo, L.ssboBindings);
        return false;
    }
    if (L.es && L.vertexSsbo < 1) {
        // En ES 3.1 los vertex shaders NO tienen que soportar SSBO (el mínimo de la especificación es 0).
        log::warn("[gpu-renderer] este driver no permite SSBO en vertex shaders "
                  "(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS = 0): se usa el render normal de GD");
        return false;
    }
    m_tile = tile;
    return true;
}

// ---------------------------------------------------------------------------
// Inicialización de GL: shaders y quad unitario
// ---------------------------------------------------------------------------
bool Renderer::initGL() {
    if (m_ready) return true;
    if (m_initFailed) return false;
    if (!gl::load()) {
        log::warn("[gpu-renderer] {}: se usa el render normal de GD", gl::loadError);
        return false;
    }
    m_es = gl::limits.es;
    if (!checkLimits()) {
        m_initFailed = true;
        return false;
    }
    auto& f = gl::fn;
    gl::clearErrors();

    // un mismo código fuente, prefijo distinto por plataforma (Shaders.hpp)
    const std::string cs = shaders::build(shaders::kCull, m_es, m_tile);
    const std::string vs = shaders::build(shaders::kVert, m_es, m_tile);
    const std::string fs = shaders::build(shaders::kFrag, m_es, m_tile);
    m_csProg = link({compile(gl::kComputeShader, cs)});
    m_drawProg = link({compile(gl::kVertexShader, vs), compile(gl::kFragmentShader, fs)});
    if (!m_csProg || !m_drawProg) {
        if (m_csProg) f.DeleteProgram(m_csProg);
        if (m_drawProg) f.DeleteProgram(m_drawProg);
        m_csProg = m_drawProg = 0;
        m_initFailed = true;
        return false;
    }

    m_uView = f.GetUniformLocation(m_csProg, "uView");
    m_uTime = f.GetUniformLocation(m_csProg, "uTime");
    m_uAudio = f.GetUniformLocation(m_csProg, "uAudio");
    m_uMvp = f.GetUniformLocation(m_drawProg, "uMvp");
    m_uTex = f.GetUniformLocation(m_drawProg, "uTex");
    m_uBase = f.GetUniformLocation(m_drawProg, "uBase");  // solo existe en la ruta ES

    static const float corners[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
    f.GenBuffers(1, &m_quadVbo);
    f.BindBuffer(gl::kArrayBuffer, m_quadVbo);
    f.BufferData(gl::kArrayBuffer, sizeof(corners), corners, gl::kStaticDraw);
    f.GenBuffers(1, &m_quadIbo);
    f.BindBuffer(gl::kElementArrayBuffer, m_quadIbo);
    f.BufferData(gl::kElementArrayBuffer, sizeof(indices), indices, gl::kStaticDraw);
    f.BindBuffer(gl::kArrayBuffer, 0);
    f.BindBuffer(gl::kElementArrayBuffer, 0);

#if GPUR_GLES_BUILD
    m_eglContext = eglGetCurrentContext();
#endif
    gl::checkError("inicialización de GL");
    m_ready = true;
    log::info("[gpu-renderer] listo: ruta {}, {} hilos por work group",
              m_es ? "OpenGL ES 3.1 (SSBO en el vertex shader + glDrawElementsIndirect por work group)"
                   : "OpenGL 4.3 (atributos instanciados + glMultiDrawElementsIndirect)",
              m_tile);
    return true;
}

// Android: si la app pasó a segundo plano y el sistema destruyó el contexto EGL, los objetos GL
// que guardamos ya no existen (y sus ids podrían coincidir con objetos nuevos de GD).
bool Renderer::contextChanged() const {
#if GPUR_GLES_BUILD
    return m_eglContext && eglGetCurrentContext() != m_eglContext;
#else
    return false;
#endif
}

// Olvida TODOS los ids GL sin llamar a glDelete* (el contexto que los poseía ya no existe).
// No toca los nodos: puede haberse pedido en mitad de un draw().
void Renderer::abandonGL() {
    for (auto& [sig, b] : m_cache) b->orderBuf = b->groupBuf = b->cmdBuf = 0;
    m_cache.clear();
    m_current = nullptr;
    m_spriteBuf = m_comboBuf = m_colorBuf = m_rawBuf = 0;
    m_vao = m_quadVbo = m_quadIbo = 0;
    m_csProg = m_drawProg = 0;
    m_eglContext = nullptr;
    m_ready = false;
    m_initFailed = false;  // en el contexto nuevo se vuelve a comprobar todo
    m_contextLost = false;
}

// ---------------------------------------------------------------------------
// Carga de nivel (sección 2.0): se suben a la GPU todos los datos estáticos
// ---------------------------------------------------------------------------
void Renderer::onLevelLoaded(GJBaseGameLayer* layer) {
    if (m_ready && contextChanged()) m_contextLost = true;
    releaseLevel();
    if (!initGL()) return;

    m_level = gd::collect(layer);
    if (m_level.sprites.empty()) return;

    // ¿cabe el nivel en un bloque SSBO? (ES 3.1 garantiza 128 MB; los drivers reales dan mucho más)
    const size_t bytes = m_level.sprites.size() * std::max(sizeof(GpuSprite), kRawSpriteSize);
    if (bytes > gl::limits.ssboBlockSize) {
        log::warn("[gpu-renderer] el nivel necesita un SSBO de {} MB y el driver permite {} MB: "
                  "se usa el render normal de GD", bytes / (1024 * 1024),
                  gl::limits.ssboBlockSize / (1024u * 1024u));
        m_level = {};
        return;
    }

    m_layer = layer;
    auto& f = gl::fn;

    std::vector<GpuSprite> sprites;
    sprites.reserve(m_level.sprites.size());
    for (auto& s : m_level.sprites) sprites.push_back(s.gpu);
    m_spriteBuf = makeBuffer(sprites.size() * sizeof(GpuSprite), sprites.data(), gl::kStaticDraw);

    // raw sprite buffer: lo escribe el compute. Escritorio: lo lee el render como atributos
    // instanciados. ES: lo lee el vertex shader como SSBO.
    m_rawBuf = makeBuffer(sprites.size() * kRawSpriteSize, nullptr);

    GpuCombo identity{};
    identity.m[0] = 1.f;
    identity.m[3] = 1.f;
    identity.alpha = 1.f;
    m_combos.assign(m_level.combos.size(), identity);
    m_comboBuf = makeBuffer(m_combos.size() * sizeof(GpuCombo), m_combos.data());

    m_colors.assign(size_t(kColorChannels) * 4, 1.f);
    m_colorBuf = makeBuffer(m_colors.size() * sizeof(float), m_colors.data());

    // canales con los que un cambio de blending puede reordenar sprites
    std::bitset<kColorChannels> used;
    for (auto& s : m_level.sprites)
        if (s.type == SpriteType::Base || s.type == SpriteType::Detail) used[s.channel] = true;
    m_usedChannels.clear();
    for (uint32_t i = 0; i < kColorChannels; ++i)
        if (used[i]) m_usedChannels.push_back(uint16_t(i));

    // VAO: quad (loc 0). Solo en escritorio, además, atributos instanciados leídos del raw
    // sprite buffer (loc 1..6); en ES esos datos los lee el vertex shader como SSBO.
    f.GenVertexArrays(1, &m_vao);
    f.BindVertexArray(m_vao);
    f.BindBuffer(gl::kArrayBuffer, m_quadVbo);
    f.EnableVertexAttribArray(0);
    f.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    f.BindBuffer(gl::kElementArrayBuffer, m_quadIbo);
    if (!m_es) {
        f.BindBuffer(gl::kArrayBuffer, m_rawBuf);
        auto attr = [&](GLuint loc, GLint n, size_t off) {
            f.EnableVertexAttribArray(loc);
            f.VertexAttribPointer(loc, n, GL_FLOAT, GL_FALSE, GLsizei(kRawSpriteSize),
                                  reinterpret_cast<const void*>(off));
            f.VertexAttribDivisor(loc, 1);
        };
        attr(1, 2, 0);   // pos
        attr(2, 2, 8);   // halfSize
        attr(3, 4, 16);  // uvA
        attr(4, 4, 32);  // color
        attr(5, 2, 48);  // uvEy
        attr(6, 1, 56);  // rot
    }
    f.BindVertexArray(0);
    f.BindBuffer(gl::kArrayBuffer, 0);

    // un nodo por ZLayer, ubicado donde iría su batch node vanilla
    std::set<int> zs;
    for (auto& s : m_level.sprites) zs.insert(s.zLayer);
    extern CCNode* gpurMakeLayerNode(int zLayer);  // definido en main.cpp
    for (int z : zs) {
        auto* node = gpurMakeLayerNode(z);
        layer->m_objectLayer->addChild(node, gd::batchZOrder(layer, z));
        m_nodes.push_back(node);
    }

    m_signature = 0;
    m_current = nullptr;
    m_frame = -1;
    m_startTime = nowSeconds();
    m_diagFrames = 3;
    gl::checkError("carga del nivel");
    m_active = true;
    log::info("[gpu-renderer] activo: {} sprites, {} combinaciones de grupos",
              m_level.sprites.size(), m_level.combos.size());
}

void Renderer::onLevelUnloaded() { releaseLevel(); }

void Renderer::releaseLevel() {
    m_active = false;
    if (m_contextLost) abandonGL();  // antes de tocar nada de GL
    for (auto& n : m_nodes) n->removeFromParentAndCleanup(true);
    m_nodes.clear();
    m_cache.clear();
    m_current = nullptr;
    if (m_ready) {
        deleteBuffer(m_spriteBuf);
        deleteBuffer(m_comboBuf);
        deleteBuffer(m_colorBuf);
        deleteBuffer(m_rawBuf);
        if (m_vao) gl::fn.DeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_level = {};
    m_layer = nullptr;
}

// ---------------------------------------------------------------------------
// Estado por combinación de grupos (sección 2.3)
//  - alpha: producto de la opacidad de todos sus grupos
//  - matriz: derivada de la pose real de un objeto de referencia del combo
//    (rotación + traslación respecto al inicio del nivel)
//  - pulso: pulse triggers sobre sus grupos, compuestos en orden
// ---------------------------------------------------------------------------
void Renderer::updateCombos() {
    for (size_t i = 1; i < m_level.combos.size(); ++i) {
        auto& ci = m_level.combos[i];
        auto& c = m_combos[i];

        float a = 1.f;
        for (int g : ci.groups) a *= gd::groupAlpha(m_layer, g);

        if (ci.refObject) {
            if (!gd::groupEnabled(ci.refObject)) a = 0.f;

            float x, y, r;
            if (gd::objectPose(ci.refObject, x, y, r)) {
                float dr = r - ci.startRot;
                float cs = std::cos(dr), sn = std::sin(dr);
                c.m[0] = cs;  c.m[1] = sn;  c.m[2] = -sn;  c.m[3] = cs;
                c.t[0] = x - (cs * ci.startPos[0] - sn * ci.startPos[1]);
                c.t[1] = y - (sn * ci.startPos[0] + cs * ci.startPos[1]);
            }
        }
        c.alpha = a;

        // Pulse triggers sobre grupos. Cada uno es mix(color, pulso, cantidad); aplicar varios en
        // orden equivale a UN solo mix con cantidad A = 1-(1-A1)(1-A2)... y color ponderado.
        // El shader aplica: rgb = mix(canal, pulse.rgb, pulse.a).
        float pr = 0.f, pg = 0.f, pb = 0.f, pa = 0.f;
        for (int g : ci.groups) {
            float rgb[3] = {0.f, 0.f, 0.f};
            float amt = 0.f;
            if (!gd::groupPulse(m_layer, g, rgb, amt) || amt <= 0.f) continue;  // STUB en GDAdapter.cpp
            amt = std::min(amt, 1.f);
            float na = 1.f - (1.f - pa) * (1.f - amt);
            pr = (pr * pa * (1.f - amt) + rgb[0] * amt) / na;
            pg = (pg * pa * (1.f - amt) + rgb[1] * amt) / na;
            pb = (pb * pa * (1.f - amt) + rgb[2] * amt) / na;
            pa = na;
        }
        c.pulse[0] = pr;  c.pulse[1] = pg;  c.pulse[2] = pb;  c.pulse[3] = pa;
    }
    updateBuffer(m_comboBuf, m_combos.size() * sizeof(GpuCombo), m_combos.data());
}

// ---------------------------------------------------------------------------
// Lote ordenado + work groups + draw commands (secciones 2.1 y 2.2)
// ---------------------------------------------------------------------------
SortedBatch* Renderer::batchFor(uint64_t signature) {
    auto it = m_cache.find(signature);
    if (it != m_cache.end()) return it->second.get();  // estado de blending ya visto

    if (m_cache.size() >= 16) m_cache.clear();
    auto batch = std::make_unique<SortedBatch>();
    build(*batch);
    auto* raw = batch.get();
    m_cache[signature] = std::move(batch);
    return raw;
}

void Renderer::build(SortedBatch& b) {
    const auto& S = m_level.sprites;
    const uint32_t total = uint32_t(S.size());

    // ¿el sprite va en un batch con blending? (glow siempre; negro nunca; el resto según su canal)
    std::vector<uint8_t> blend(total);
    for (uint32_t i = 0; i < total; ++i) {
        if (S[i].type == SpriteType::Glow) blend[i] = 1;
        else if (S[i].type == SpriteType::Black) blend[i] = 0;
        else blend[i] = m_blend[S[i].channel] ? 1 : 0;
    }

    // orden de dibujo: ZLayer, hoja de sprites, blending (y dentro, el zOrder del objeto)
    std::vector<uint32_t> idx(total);
    std::iota(idx.begin(), idx.end(), 0u);
    std::stable_sort(idx.begin(), idx.end(), [&](uint32_t a, uint32_t c) {
        return std::tuple(S[a].zLayer, S[a].sheetRank, S[a].texture, blend[a], S[a].zOrder) <
               std::tuple(S[c].zLayer, S[c].sheetRank, S[c].texture, blend[c], S[c].zOrder);
    });

    // tamaño de chunk para acercarse al número de work groups pedido.
    // En ES hay UN draw call de CPU por work group (no hay multi-draw), así que su ajuste es aparte
    // y con un valor por defecto mucho menor.
    const char* key = m_es ? "work-groups-mobile" : "work-groups";
    uint32_t target = uint32_t(std::max<int64_t>(1, Mod::get()->getSettingValue<int64_t>(key)));
    uint32_t chunk = (total + target - 1) / target;
    chunk = std::max(m_tile, (chunk + m_tile - 1) / m_tile * m_tile);

    // runs (mismo ZLayer + textura + blending); cada work group queda dentro de un solo run
    std::vector<GpuWorkGroup> groups;
    for (uint32_t i = 0; i < total;) {
        const auto& first = S[idx[i]];
        const uint8_t bl = blend[idx[i]];
        uint32_t j = i;
        while (j < total && S[idx[j]].zLayer == first.zLayer &&
               S[idx[j]].texture == first.texture && blend[idx[j]] == bl)
            ++j;

        Run run{first.zLayer, first.texture, bl != 0, uint32_t(groups.size()), 0};
        for (uint32_t k = i; k < j; k += chunk) {
            groups.push_back({k, std::min(chunk, j - k), k, 0});
            b.outBase.push_back(k);
        }
        run.groupCount = uint32_t(groups.size()) - run.firstGroup;
        b.runs.push_back(run);
        i = j;
    }
    b.groupCount = uint32_t(groups.size());
    if (int64_t(b.groupCount) > int64_t(gl::limits.wgCountX))
        log::error("[gpu-renderer] {} work groups superan el máximo del driver ({}): baja el ajuste de work groups",
                   b.groupCount, gl::limits.wgCountX);

    b.orderBuf = makeBuffer(idx.size() * sizeof(uint32_t), idx.data(), gl::kStaticDraw);
    b.groupBuf = makeBuffer(groups.size() * sizeof(GpuWorkGroup), groups.data(), gl::kStaticDraw);
    std::vector<uint32_t> zeros(size_t(b.groupCount) * 5, 0u);
    b.cmdBuf = makeBuffer(zeros.size() * sizeof(uint32_t), zeros.data());
}

// ---------------------------------------------------------------------------
// Frame: subir colores/estados, elegir lote por blending y correr el compute shader
// ---------------------------------------------------------------------------
void Renderer::beginFrameOnce(const kmMat4& mvp) {
    if (contextChanged()) {
        // No se limpia nada aquí (estamos dentro de un draw()): releaseLevel() lo hará al salir del nivel.
        log::warn("[gpu-renderer] el contexto EGL cambió (¿la app volvió del segundo plano?): "
                  "el renderer se desactiva hasta el próximo nivel y GD dibuja normal");
        m_contextLost = true;
        m_active = false;
        return;
    }
    int frame = CCDirector::get()->getTotalFrames();
    if (frame == m_frame) return;
    m_frame = frame;
    beginFrame(mvp);
}

void Renderer::beginFrame(const kmMat4& mvp) {
    auto& f = gl::fn;

    // 1) colores de los 1101 canales + estado de grupos -> GPU
    gd::fetchChannels(m_layer, m_colors.data(), m_blend);
    updateBuffer(m_colorBuf, m_colors.size() * sizeof(float), m_colors.data());
    updateCombos();

    // 2) si cambió el blending, otro lote ordenado (reutilizado desde caché si ya existía)
    uint64_t sig = 1469598103934665603ull;
    for (auto ch : m_usedChannels) sig = fnv(sig, m_blend[ch] ? 2 : 1);
    if (!m_current || sig != m_signature) {
        m_signature = sig;
        m_current = batchFor(sig);
    }

    // 3) rectángulo visible en espacio de la capa de objetos (invirtiendo la MVP)
    kmMat4 inv;
    kmMat4Inverse(&inv, &mvp);
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (int i = 0; i < 4; ++i) {
        kmVec3 in = {(i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f, 0.f}, out;
        kmVec3TransformCoord(&out, &in, &inv);
        minX = std::min(minX, out.x);  maxX = std::max(maxX, out.x);
        minY = std::min(minY, out.y);  maxY = std::max(maxY, out.y);
    }

    // 4) compute shader
    f.UseProgram(m_csProg);
    f.Uniform4f(m_uView, minX, minY, maxX, maxY);
    f.Uniform1f(m_uTime, float(nowSeconds() - m_startTime));
    f.Uniform1f(m_uAudio, gd::audioScale());
    f.BindBufferBase(gl::kShaderStorageBuffer, 0, m_spriteBuf);
    f.BindBufferBase(gl::kShaderStorageBuffer, 1, m_current->orderBuf);
    f.BindBufferBase(gl::kShaderStorageBuffer, 2, m_comboBuf);
    f.BindBufferBase(gl::kShaderStorageBuffer, 3, m_colorBuf);
    f.BindBufferBase(gl::kShaderStorageBuffer, 4, m_current->groupBuf);
    f.BindBufferBase(gl::kShaderStorageBuffer, kRawBinding, m_rawBuf);
    f.BindBufferBase(gl::kShaderStorageBuffer, 6, m_current->cmdBuf);
    f.DispatchCompute(m_current->groupCount, 1, 1);
    f.MemoryBarrier(m_es ? gl::kPostComputeBarrierEs : gl::kPostComputeBarrierDesktop);
    f.UseProgram(0);

    if (m_diagFrames > 0) {
        gl::checkError("compute shader");
        --m_diagFrames;
    }
}

// ---------------------------------------------------------------------------
// Etapa de render (sección 2.4)
// ---------------------------------------------------------------------------
void Renderer::drawLayer(int zLayer, const kmMat4& mvp) {
    if (!m_active || !m_current) return;
    auto& f = gl::fn;

    bool any = false;
    for (auto& r : m_current->runs) if (r.zLayer == zLayer) { any = true; break; }
    if (!any) return;

    f.UseProgram(m_drawProg);
    f.UniformMatrix4fv(m_uMvp, 1, GL_FALSE, mvp.mat);
    f.Uniform1i(m_uTex, 0);
    f.BindVertexArray(m_vao);
    f.BindBuffer(gl::kDrawIndirectBuffer, m_current->cmdBuf);
    // ES: el vertex shader lee el raw sprite buffer como SSBO (se vuelve a enlazar por si otro código tocó el binding)
    if (m_es) f.BindBufferBase(gl::kShaderStorageBuffer, kRawBinding, m_rawBuf);
    glEnable(GL_BLEND);
    f.ActiveTexture(gl::kTexture0);

    for (auto& r : m_current->runs) {
        if (r.zLayer != zLayer) continue;
        if (r.blend) glBlendFunc(kBlendSrc, kBlendDst);
        else glBlendFunc(kNormalSrc, kNormalDst);
        glBindTexture(GL_TEXTURE_2D, r.texture);

        if (m_es) {
            // ES 3.1: sin multi-draw y sin baseInstance. Un draw indirecto por work group;
            // uBase le dice al vertex shader dónde empieza su porción del raw sprite buffer.
            for (uint32_t g = r.firstGroup; g < r.firstGroup + r.groupCount; ++g) {
                f.Uniform1ui(m_uBase, m_current->outBase[g]);
                f.DrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_SHORT,
                                       reinterpret_cast<const void*>(uintptr_t(g) * kDrawCmdSize));
            }
        } else {
            f.MultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_SHORT,
                                        reinterpret_cast<const void*>(uintptr_t(r.firstGroup) * kDrawCmdSize),
                                        GLsizei(r.groupCount), GLsizei(kDrawCmdSize));
        }
    }

    f.BindVertexArray(0);
    f.BindBuffer(gl::kDrawIndirectBuffer, 0);
    f.UseProgram(0);
    restoreCocosState();

    if (m_diagFrames > 0) gl::checkError("dibujado de la capa");
}

}  // namespace gpur
