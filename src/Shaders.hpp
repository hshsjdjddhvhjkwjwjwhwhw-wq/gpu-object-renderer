#pragma once
#include <cstdint>
#include <string>

namespace gpur::shaders {

// ---------------------------------------------------------------------------
// Un SOLO código fuente GLSL para las dos plataformas. build() le antepone:
//   - Windows (OpenGL 4.3):  "#version 430 core"
//   - Android (OpenGL ES 3.1): "#version 310 es" + "precision highp float/int"
//                              + "#define GPUR_ES 1"
//   - "#define GPUR_TILE <n>": hilos por work group (lo decide el host según los
//     límites del driver; en ES 3.1 el mínimo garantizado es 128, no 256).
// Los números de línea de los errores del driver incluyen estas líneas de prefijo.
//
// Bindings SSBO (deben coincidir con Renderer.cpp):
//   0 sprites | 1 order | 2 combos | 3 colores | 4 work groups | 5 raw sprite buffer | 6 draw commands
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Compute shader (secciones 2.2 y 2.3 de la explicación)
// Un work group = un rango contiguo del lote ordenado.
// Por sprite: aplica la matriz de su combinación de grupos, comprueba si está
// en pantalla, resuelve el color desde el color channel buffer, aplica pulso,
// rotación y opacidad, y lo compacta (en orden) en el raw sprite buffer.
// Al final cada work group escribe su draw command.
// ---------------------------------------------------------------------------
inline constexpr const char* kCull = R"GLSL(
layout(local_size_x = GPUR_TILE) in;

// En ES 3.1 barrier() solo sincroniza la ejecución: la visibilidad de las
// escrituras a memoria compartida hay que pedirla con memoryBarrierShared().
#define GPUR_SYNC() memoryBarrierShared(); barrier()

struct Sprite {
    vec2 startPos; vec2 halfSize;
    vec4 uvA;
    vec2 uvEy; float startRot; uint groupCombo;
    uint meta; float opacity; float rotSpeed; float pad;
};
struct Raw {
    vec2 pos; vec2 halfSize;
    vec4 uvA;
    vec4 color;
    vec2 uvEy; float rot; float pad;
};
struct Combo { vec4 m; vec2 t; float alpha; float pad; vec4 pulse; };
struct WG    { uint first; uint count; uint outBase; uint pad; };

layout(std430, binding = 0) readonly buffer Sprites { Sprite sprites[]; };
layout(std430, binding = 1) readonly buffer Order   { uint   order[];   };
layout(std430, binding = 2) readonly buffer Combos  { Combo  combos[];  };
layout(std430, binding = 3) readonly buffer Colors  { vec4   colors[];  };   // 1101 canales
layout(std430, binding = 4) readonly buffer Groups  { WG     wgs[];     };
layout(std430, binding = 5) writeonly buffer RawOut { Raw    raw[];     };
layout(std430, binding = 6) buffer Cmds             { uint   cmds[];    };

uniform vec4  uView;   // minX, minY, maxX, maxY en espacio de la capa de objetos
uniform float uTime;
uniform float uAudio;

shared uint sScan[GPUR_TILE];
shared uint sRunning;

void main() {
    const uint TILE = uint(GPUR_TILE);
    uint lid = gl_LocalInvocationID.x;
    WG g = wgs[gl_WorkGroupID.x];

    if (lid == 0u) sRunning = 0u;
    GPUR_SYNC();

    for (uint base = 0u; base < g.count; base += TILE) {
        uint i = base + lid;
        bool vis = false;
        Raw r;
        r.pos = vec2(0.0); r.halfSize = vec2(0.0); r.uvA = vec4(0.0);
        r.color = vec4(0.0); r.uvEy = vec2(0.0); r.rot = 0.0; r.pad = 0.0;

        if (i < g.count) {
            Sprite s = sprites[order[g.first + i]];
            Combo  c = combos[s.groupCombo];

            // posición actual = matriz de la combinación * posición inicial
            vec2 p = vec2(c.m.x * s.startPos.x + c.m.z * s.startPos.y,
                          c.m.y * s.startPos.x + c.m.w * s.startPos.y) + c.t;
            vec2  sc  = vec2(length(c.m.xy), length(c.m.zw));
            float rot = s.startRot + atan(c.m.y, c.m.x) + s.rotSpeed * uTime;
            vec2  hs  = s.halfSize * sc;
            if (((s.meta >> 18) & 1u) != 0u) hs *= uAudio;   // objeto de audio que "rebota"

            // color: tipo + canal
            uint type = (s.meta >> 16) & 3u;
            vec4 col  = colors[min(s.meta & 0xFFFFu, 1100u)];
            vec3 rgb;
            float a = c.alpha * s.opacity;
            if (type == 3u) {                       // sprite negro: siempre negro
                rgb = vec3(0.0);
            } else {
                rgb = mix(col.rgb, c.pulse.rgb, c.pulse.a);   // pulse group triggers
                a  *= col.a;
            }

            // ¿está en pantalla? (si no, se salta)
            float rad = length(hs);
            vis = a > 0.0
               && p.x + rad >= uView.x && p.x - rad <= uView.z
               && p.y + rad >= uView.y && p.y - rad <= uView.w;

            r.pos = p; r.halfSize = hs; r.uvA = s.uvA; r.uvEy = s.uvEy;
            r.color = vec4(rgb, a); r.rot = rot;
        }

        // compactación ORDENADA (scan inclusivo) para conservar el orden de dibujo
        sScan[lid] = vis ? 1u : 0u;
        GPUR_SYNC();
        for (uint off = 1u; off < TILE; off <<= 1u) {
            uint v = (lid >= off) ? sScan[lid - off] : 0u;
            GPUR_SYNC();
            sScan[lid] += v;
            GPUR_SYNC();
        }
        uint incl      = sScan[lid];
        uint tileTotal = sScan[TILE - 1u];
        if (vis) raw[g.outBase + sRunning + incl - 1u] = r;
        GPUR_SYNC();
        if (lid == 0u) sRunning += tileTotal;
        GPUR_SYNC();
    }

    // draw command de este work group: 6 índices, N instancias visibles
    if (lid == 0u) {
        uint b = gl_WorkGroupID.x * 5u;
        cmds[b + 0u] = 6u;          // count
        cmds[b + 1u] = sRunning;    // instanceCount
        cmds[b + 2u] = 0u;          // firstIndex
        cmds[b + 3u] = 0u;          // baseVertex
#ifdef GPUR_ES
        // ES 3.1: el último campo de DrawElementsIndirectCommand es "reservedMustBeZero"
        // (no existe baseInstance). El vertex shader suma el offset del grupo con un uniform.
        cmds[b + 4u] = 0u;
#else
        cmds[b + 4u] = g.outBase;   // baseInstance: offset en los atributos instanciados
#endif
    }
}
)GLSL";

// ---------------------------------------------------------------------------
// Etapa de render (sección 2.4): un quad instanciado por sprite.
//  - Escritorio: transformación y color llegan como atributos instanciados
//    (baseInstance del draw command apunta a la porción del raw sprite buffer).
//  - ES 3.1: el vertex shader lee el raw sprite buffer como SSBO con
//    uBase + gl_InstanceID (uBase = primer sprite del work group, un uniform
//    que se fija antes de cada glDrawElementsIndirect).
// ---------------------------------------------------------------------------
inline constexpr const char* kVert = R"GLSL(
layout(location = 0) in vec2  aCorner;   // (-1,-1) bl, (1,-1) br, (-1,1) tl, (1,1) tr

#ifdef GPUR_ES
struct Raw {
    vec2 pos; vec2 halfSize;
    vec4 uvA;
    vec4 color;
    vec2 uvEy; float rot; float pad;
};
layout(std430, binding = 5) readonly buffer RawIn { Raw raw[]; };
uniform uint uBase;
#else
layout(location = 1) in vec2  iPos;
layout(location = 2) in vec2  iHalf;
layout(location = 3) in vec4  iUvA;
layout(location = 4) in vec4  iColor;
layout(location = 5) in vec2  iUvEy;
layout(location = 6) in float iRot;
#endif

uniform mat4 uMvp;
out vec2 vUv;
out vec4 vColor;

void main() {
#ifdef GPUR_ES
    Raw r = raw[uBase + uint(gl_InstanceID)];
    vec2  iPos  = r.pos;
    vec2  iHalf = r.halfSize;
    vec4  iUvA  = r.uvA;
    vec4  iColor = r.color;
    vec2  iUvEy = r.uvEy;
    float iRot  = r.rot;
#endif
    float c = cos(iRot), s = sin(iRot);
    vec2 l = aCorner * iHalf;
    vec2 w = iPos + vec2(c * l.x - s * l.y, s * l.x + c * l.y);
    gl_Position = uMvp * vec4(w, 0.0, 1.0);
    vec2 t = aCorner * 0.5 + 0.5;
    vUv = iUvA.xy + t.x * iUvA.zw + t.y * iUvEy;   // sirve también para frames rotados/flip
    vColor = iColor;
}
)GLSL";

inline constexpr const char* kFrag = R"GLSL(
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 oColor;

void main() {
    // mismo resultado que el shader de cocos2d: color * opacidad (premultiplicado) * textura
    oColor = texture(uTex, vUv) * vec4(vColor.rgb * vColor.a, vColor.a);
}
)GLSL";

// Arma el código final de un shader para la plataforma actual.
inline std::string build(const char* body, bool es, uint32_t tile) {
    std::string s;
    if (es) {
        s += "#version 310 es\n";
        s += "precision highp float;\n";
        s += "precision highp int;\n";
        s += "#define GPUR_ES 1\n";
    } else {
        s += "#version 430 core\n";
    }
    s += "#define GPUR_TILE " + std::to_string(tile) + "\n";
    s += body;
    return s;
}

}  // namespace gpur::shaders
