#include "GDAdapter.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>

using namespace geode::prelude;

// NOTA: este archivo NO se ha compilado contra los bindings reales de Geode.
// Los puntos marcados con "VERIFY" son los que más probablemente necesiten ajuste.
//
// Contrastado (solo LEYENDO la documentación pública docs.geode-sdk.org, no compilando) que EXISTEN:
//   GameObject: m_groups (std::array<short,_unk>*), m_groupCount (short), m_isGroupDisabled,
//     m_baseColor / m_detailColor (GJSpriteColor*), m_colorSprite, m_glowSprite, m_zLayer, m_zOrder,
//     m_usesAudioScale.
//   GJSpriteColor: m_colorID, m_defaultColorID.   ColorActionSprite: m_color, m_opacity, m_colorAction.
//   ColorAction: m_blending.   GJEffectManager: getColorSprite(int), opacityModForGroup(int),
//     isGroupEnabled(int), m_pulseEffectMap (unordered_map<int, vector<PulseEffectAction>>).
//   PulseEffectAction: m_disabled, m_color, m_currentValue, m_pulseEffectType, m_mainOnly, m_detailOnly.
//   GJBaseGameLayer: updateVisibility(float), m_effectManager.
// NO se pudo contrastar: m_objects / m_objectLayer de GJBaseGameLayer (la página salió truncada),
//   el enum PulseEffectType, ni que las direcciones android32/android64 existan para cada función.
// Que la documentación liste un miembro NO garantiza que su nombre/tipo coincida en tu versión de bindings.

namespace gpur::gd {
namespace {

constexpr float kDeg2Rad = 3.14159265358979f / 180.f;

// Orden de hojas de sprites (sección 1.0): define el orden dentro de cada ZLayer.
// VERIFY: confirmar el orden real de los batch nodes del juego.
constexpr const char* kSheets[] = {"GJ_GameSheet",  "GJ_GameSheet02", "GJ_GameSheetGlow",
                                   "FireSheet",     "ParticleSheet",  "PixelSheet"};

size_t g_skippedInvisible = 0;  // diagnóstico (C2): sprites descartados por !isVisible() al cargar
bool g_includeInvisible = false;  // depuración: no descartar sprites ocultos
bool g_debugLog = false;          // depuración: registrar muestras
int  g_samples = 0;               // muestras ya registradas en este nivel
std::unordered_map<unsigned, uint8_t> g_rank;  // nombre GL de textura -> rango de hoja

void buildRankMap() {
    g_rank.clear();
    auto* cache = CCTextureCache::get();
    auto* files = CCFileUtils::get();
    for (uint8_t i = 0; i < 6; ++i) {
        for (auto suffix : {"-uhd.png", "-hd.png", ".png"}) {
            std::string name = std::string(kSheets[i]) + suffix;
            // VERIFY (Android): en cocos2d-x 2.x textureForKey() resuelve la ruta completa por dentro,
            // así que se prueba primero el nombre tal cual y luego la ruta resuelta a mano. En Android
            // la clave de la caché puede ser distinta (assets del APK) y, si no coincide ninguna,
            // todas las hojas quedan con el rango 6 y se pierde el orden entre hojas.
            auto* t = cache->textureForKey(name.c_str());
            if (!t) {
                std::string path = files->fullPathForFilename(name.c_str(), false);
                t = cache->textureForKey(path.c_str());
            }
            if (t) g_rank[t->getName()] = i;
        }
    }
}

uint8_t rankOf(CCTexture2D* tex) {
    auto it = g_rank.find(tex->getName());
    return it == g_rank.end() ? uint8_t(6) : it->second;
}

// Transformación acumulada (rotación ANTIHORARIA en rad; cocos usa grados horarios).
struct Xf { float x, y, rot, sx, sy; };

Xf rootXf(CCNode* n) {
    return {n->getPositionX(), n->getPositionY(), -n->getRotation() * kDeg2Rad,
            n->getScaleX(), n->getScaleY()};
}

Xf childXf(const Xf& parent, CCNode* parentNode, CCNode* child) {
    CCSize pcs = parentNode->getContentSize();
    CCPoint pa = parentNode->getAnchorPoint();
    float ox = (child->getPositionX() - pcs.width * pa.x) * parent.sx;
    float oy = (child->getPositionY() - pcs.height * pa.y) * parent.sy;
    float c = std::cos(parent.rot), s = std::sin(parent.rot);
    return {parent.x + c * ox - s * oy, parent.y + s * ox + c * oy,
            parent.rot - child->getRotation() * kDeg2Rad,
            parent.sx * child->getScaleX(), parent.sy * child->getScaleY()};
}

// VERIFY: en 2.2074 m_groups es std::array<short,10>* (nullptr si no hay grupos) + m_groupCount.
template <class F>
void forEachGroup(GameObject* obj, F&& fn) {
    if (!obj->m_groups) return;
    for (int i = 0; i < obj->m_groupCount; ++i) fn(static_cast<int>((*obj->m_groups)[i]));
}

// VERIFY: GJSpriteColor::m_colorID / m_defaultColorID
int channelOf(GJSpriteColor* c) {
    if (!c) return 1010;
    int id = c->m_colorID ? c->m_colorID : c->m_defaultColorID;
    return (id >= 0 && id < int(kColorChannels)) ? id : 1010;
}

// STUB: velocidad de giro continuo (rad/s) de sierras y similares. Con 0 no giran.
// El shader YA lo soporta (Sprite.rotSpeed * uTime); falta SOLO leer el dato del objeto.
// VERIFY: en los bindings de GameObject (GameObject.hpp de geode-sdk/bindings) busca miembros con
// "rotat"/"spin" y comprueba (a) cuál guarda los grados/seg de las sierras y rotadores,
// (b) su signo respecto a getRotation() (cocos: horario; aquí rad antihorarios => negar),
// (c) que sea realmente una velocidad constante. No lo rellené para no inventar un miembro.
float rotationSpeedOf(GameObject*) { return 0.f; }

// Objetos cuyo tamaño sigue el audio (sección 1.2).
// m_usesAudioScale figura en la documentación de GameObject. VERIFY: que sea el flag que activa el
// "rebote" y no solo una opción del editor. Mientras audioScale() devuelva 1 (STUB) no cambia nada visible.
bool usesAudioScale(GameObject* o) { return o->m_usesAudioScale; }

// Valor del enum PulseEffectType que corresponde a un pulso de COLOR (no HSV).
// VERIFY: no se pudo leer el enum. Con -1 los pulse triggers de grupo quedan DESACTIVADOS (se
// ignoran en vez de dibujar un color equivocado). Pon aquí el valor correcto de tu PulseEffectType.
constexpr int kPulseTypeColor = -1;

void emit(LevelData& L, CCSprite* sp, const Xf& xf, SpriteType type, int channel,
          uint32_t combo, float rotSpeed, bool audio, int zLayer, int zOrder) {
    if (!sp) return;
    if (!sp->isVisible()) {
        ++g_skippedInvisible;
        if (!g_includeInvisible) return;
    }
    auto* tex = sp->getTexture();
    if (!tex) return;

    // VERIFY: se asume que el quad está en coordenadas locales (sprite aún fuera de un batch).
    auto q = sp->getQuad();

    // Depuración: comparar posición/escala del objeto con su quad, textureRect y contentSize.
    // Sirve para saber si el quad está en coordenadas locales (como asume este código) o ya
    // transformadas por el batch node de GD.
    if (g_debugLog && g_samples < 8 && type == SpriteType::Base &&
        (g_samples < 3 || std::fabs(xf.sx - 1.f) > 0.01f || std::fabs(xf.sy - 1.f) > 0.01f ||
         std::fabs(xf.rot) > 0.01f)) {
        ++g_samples;
        CCRect tr = sp->getTextureRect();
        CCSize csz = sp->getContentSize();
        log::info("[gpu-renderer] muestra {}: pos=({:.1f},{:.1f}) escala=({:.2f},{:.2f}) rot={:.2f}rad visible={} | "
                  "quad bl=({:.1f},{:.1f}) tr=({:.1f},{:.1f}) | textureRect={:.1f}x{:.1f} contentSize={:.1f}x{:.1f}",
                  g_samples, xf.x, xf.y, xf.sx, xf.sy, xf.rot, sp->isVisible(),
                  q.bl.vertices.x, q.bl.vertices.y, q.tr.vertices.x, q.tr.vertices.y,
                  tr.size.width, tr.size.height, csz.width, csz.height);
    }
    float cx = (q.bl.vertices.x + q.tr.vertices.x) * 0.5f;
    float cy = (q.bl.vertices.y + q.tr.vertices.y) * 0.5f;
    float hx = std::fabs(q.tr.vertices.x - q.bl.vertices.x) * 0.5f;
    float hy = std::fabs(q.tr.vertices.y - q.bl.vertices.y) * 0.5f;

    CCSize cs = sp->getContentSize();
    CCPoint an = sp->getAnchorPoint();
    float lx = (cx - cs.width * an.x) * xf.sx;
    float ly = (cy - cs.height * an.y) * xf.sy;
    float c = std::cos(xf.rot), s = std::sin(xf.rot);

    CpuSprite o;
    o.gpu.startPos[0] = xf.x + c * lx - s * ly;
    o.gpu.startPos[1] = xf.y + s * lx + c * ly;
    o.gpu.halfSize[0] = hx * std::fabs(xf.sx);
    o.gpu.halfSize[1] = hy * std::fabs(xf.sy);
    // El quad ya trae las coordenadas de textura finales (frames rotados en la hoja y flips incluidos).
    o.gpu.uvA[0] = q.bl.texCoords.u;
    o.gpu.uvA[1] = q.bl.texCoords.v;
    o.gpu.uvA[2] = q.br.texCoords.u - q.bl.texCoords.u;
    o.gpu.uvA[3] = q.br.texCoords.v - q.bl.texCoords.v;
    o.gpu.uvEy[0] = q.tl.texCoords.u - q.bl.texCoords.u;
    o.gpu.uvEy[1] = q.tl.texCoords.v - q.bl.texCoords.v;
    o.gpu.startRot = xf.rot;
    o.gpu.groupCombo = combo;
    o.gpu.meta = uint32_t(channel) | (uint32_t(type) << 16) | (audio ? (1u << 18) : 0u);
    o.gpu.opacity = sp->getOpacity() / 255.f;
    o.gpu.rotSpeed = rotSpeed;
    o.zLayer = zLayer;
    o.zOrder = zOrder;
    o.sheetRank = rankOf(tex);
    o.texture = tex->getName();
    o.type = type;
    o.channel = uint16_t(channel);
    L.sprites.push_back(o);
}

}  // namespace

LevelData collect(GJBaseGameLayer* layer, bool includeInvisible, bool debugLog) {
    buildRankMap();
    g_skippedInvisible = 0;
    g_includeInvisible = includeInvisible;
    g_debugLog = debugLog;
    g_samples = 0;

    LevelData L;
    L.combos.emplace_back();  // combo 0: objetos sin grupos (identidad, nunca se mueven)
    std::map<std::vector<int>, uint32_t> comboIds;
    comboIds[{}] = 0;

    for (auto* obj : CCArrayExt<GameObject*>(layer->m_objects)) {
        // --- combinación de grupos (sección 2.3) ---
        std::vector<int> groups;
        forEachGroup(obj, [&](int g) { groups.push_back(g); });
        std::sort(groups.begin(), groups.end());
        groups.erase(std::unique(groups.begin(), groups.end()), groups.end());

        auto [it, inserted] = comboIds.try_emplace(groups, uint32_t(L.combos.size()));
        if (inserted) {
            ComboInfo ci;
            ci.groups = groups;
            L.combos.push_back(std::move(ci));
        }
        uint32_t combo = it->second;
        float rotSpeed = rotationSpeedOf(obj);

        Xf root = rootXf(obj);

        // Objeto de referencia: de él se lee la pose real para derivar la matriz del combo.
        // No sirve uno que gire solo (su rotación incluiría el giro continuo).
        auto& ci = L.combos[combo];
        if (combo != 0 && !ci.refObject && rotSpeed == 0.f) {
            ci.refObject = obj;
            ci.startPos[0] = root.x;
            ci.startPos[1] = root.y;
            ci.startRot = root.rot;
        }

        // --- sprites del objeto ---
        int zl = static_cast<int>(obj->m_zLayer);
        int zo = obj->m_zOrder;
        int baseCh = channelOf(obj->m_baseColor);
        int detCh = channelOf(obj->m_detailColor);
        bool audio = usesAudioScale(obj);

        emit(L, obj, root, SpriteType::Base, baseCh, combo, rotSpeed, audio, zl, zo);

        if (obj->m_colorSprite)
            emit(L, obj->m_colorSprite, childXf(root, obj, obj->m_colorSprite),
                 SpriteType::Detail, detCh, combo, rotSpeed, audio, zl, zo);

        // VERIFY: el glow usa el canal base del objeto
        if (obj->m_glowSprite)
            emit(L, obj->m_glowSprite, childXf(root, obj, obj->m_glowSprite),
                 SpriteType::Glow, baseCh, combo, rotSpeed, audio, zl, zo);

        // Hijos extra. CAMBIO v0.2: la versión anterior usaba obj->m_childSprites, que NO aparece en la
        // documentación de GameObject. Se recorren los hijos reales del nodo (API estándar de cocos,
        // CCNode::getChildren) y se saltan los que ya se emitieron arriba.
        // VERIFY: que los hijos CCSprite directos del objeto sean los sprites extra que GD dibuja
        // (si GD los mete en otro nodo, aquí no aparecerán) y que la heurística de negro sea correcta.
        if (auto* kids = obj->getChildren()) {
            for (auto* node : CCArrayExt<CCNode*>(kids)) {
                auto* sp = typeinfo_cast<CCSprite*>(node);
                if (!sp || sp == obj->m_colorSprite || sp == obj->m_glowSprite) continue;
                auto col = sp->getColor();
                bool black = col.r == 0 && col.g == 0 && col.b == 0;
                emit(L, sp, childXf(root, obj, sp),
                     black ? SpriteType::Black : SpriteType::Detail, detCh, combo,
                     rotSpeed, audio, zl, zo);
            }
        }
    }
    log::info("[gpu-renderer] objetos de GD: {}; recolectados {} sprites; sprites invisibles al cargar: {} ({})",
              layer->m_objects ? layer->m_objects->count() : 0u, L.sprites.size(), g_skippedInvisible,
              includeInvisible ? "se recogieron igualmente" : "descartados");
    return L;
}

void fetchChannels(GJBaseGameLayer* layer, float* rgba, std::bitset<kColorChannels>& blending) {
    auto* fx = layer->m_effectManager;
    for (uint32_t i = 0; i < kColorChannels; ++i) {
        float* p = rgba + i * 4;
        p[0] = p[1] = p[2] = p[3] = 1.f;
        blending[i] = false;
        // getColorSprite(int) y m_color / m_opacity figuran en la documentación.
        // CAMBIO v0.2: ColorActionSprite NO tiene m_blending; el flag está en ColorAction::m_blending
        // (ColorActionSprite::m_colorAction). VERIFY: que m_colorAction no sea nulo para canales sin acción.
        auto* c = fx->getColorSprite(int(i));
        if (!c) continue;
        p[0] = c->m_color.r / 255.f;
        p[1] = c->m_color.g / 255.f;
        p[2] = c->m_color.b / 255.f;
        p[3] = c->m_opacity;
        blending[i] = c->m_colorAction ? c->m_colorAction->m_blending : false;
    }
}

bool channelHasSprite(GJBaseGameLayer* layer, int channel) {
    return layer->m_effectManager->getColorSprite(channel) != nullptr;
}

float groupAlpha(GJBaseGameLayer* layer, int group) {
    return layer->m_effectManager->opacityModForGroup(group);  // VERIFY
}

// Pulse triggers sobre un GRUPO: se lee GJEffectManager::m_pulseEffectMap[group] (lista de
// PulseEffectAction). Cada acción trae su color (m_color) y su intensidad actual (m_currentValue).
// El Renderer compone varias en una sola mezcla. Limitaciones conocidas:
//  - solo pulsos de COLOR (ver kPulseTypeColor: con -1 está todo desactivado);
//  - m_mainOnly / m_detailOnly se ignoran (el combo afecta a base y detalle por igual);
//  - se asume m_currentValue en 0..1 = cantidad de mezcla (VERIFY: confirmar en pantalla con un pulse trigger).
bool groupPulse(GJBaseGameLayer* layer, int group, float rgb[3], float& amount) {
    rgb[0] = rgb[1] = rgb[2] = 0.f;
    amount = 0.f;
    if (kPulseTypeColor < 0) return false;

    auto* fx = layer->m_effectManager;
    auto it = fx->m_pulseEffectMap.find(group);
    if (it == fx->m_pulseEffectMap.end()) return false;

    float r = 0.f, g = 0.f, b = 0.f, a = 0.f;
    for (auto& act : it->second) {
        if (act.m_disabled || int(act.m_pulseEffectType) != kPulseTypeColor) continue;
        float v = std::clamp(act.m_currentValue, 0.f, 1.f);
        if (v <= 0.f) continue;
        float na = 1.f - (1.f - a) * (1.f - v);
        r = (r * a * (1.f - v) + (act.m_color.r / 255.f) * v) / na;
        g = (g * a * (1.f - v) + (act.m_color.g / 255.f) * v) / na;
        b = (b * a * (1.f - v) + (act.m_color.b / 255.f) * v) / na;
        a = na;
    }
    rgb[0] = r;  rgb[1] = g;  rgb[2] = b;
    amount = a;
    return a > 0.f;
}

bool groupEnabled(void* refObject) {
    // VERIFY: nombre del flag que ponen los toggle triggers
    return !static_cast<GameObject*>(refObject)->m_isGroupDisabled;
}

bool objectPose(void* refObject, float& x, float& y, float& rotRad) {
    // VERIFY: que la posición/rotación del objeto se actualice aunque no esté visible
    auto* o = static_cast<GameObject*>(refObject);
    x = o->getPositionX();
    y = o->getPositionY();
    rotRad = -o->getRotation() * kDeg2Rad;
    return true;
}

// STUB: escala actual de los objetos que "rebotan" con el audio (1 = sin efecto).
// VERIFY: buscar en FMODAudioEngine (y en GJBaseGameLayer) el valor de medición/pulso del audio
// que GD usa para escalar esos objetos, y comprobar su rango (el shader multiplica el tamaño).
float audioScale() { return 1.f; }

int batchZOrder(GJBaseGameLayer*, int zLayer) {
    return zLayer;  // VERIFY: usar el zOrder del batch node vanilla de ese ZLayer
}

}  // namespace gpur::gd
