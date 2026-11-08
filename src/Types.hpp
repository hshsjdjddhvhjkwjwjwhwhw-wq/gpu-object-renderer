#pragma once
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gpur {

constexpr uint32_t kColorChannels = 1101;  // canales de color 0..1100 (como en la explicación)
// Hilos por work group PREFERIDOS. El valor real (local_size_x) lo fija Renderer::checkLimits()
// con los límites del driver: en OpenGL ES 3.1 el mínimo garantizado es 128, no 256.
constexpr uint32_t kTileSize      = 256;

// Tipos de sprite (sección 1.0): base, detalle, glow y negro.
enum class SpriteType : uint32_t { Base = 0, Detail = 1, Glow = 2, Black = 3 };

// ---------------------------------------------------------------------------
// Espejos EXACTOS de los structs std430 del compute shader (ver Shaders.hpp).
// ---------------------------------------------------------------------------

// Datos estáticos de un sprite: se suben UNA vez al cargar el nivel.
struct alignas(16) GpuSprite {
    float    startPos[2];  //  0  posición al inicio del nivel
    float    halfSize[2];  //  8  medio tamaño (ya incluye la escala del objeto)
    float    uvA[4];       // 16  bl.uv , (br-bl).uv
    float    uvEy[2];      // 32  (tl-bl).uv
    float    startRot;     // 40  rotación inicial (rad, antihoraria)
    uint32_t groupCombo;   // 44  id de combinación de grupos (sección 2.3)
    uint32_t meta;         // 48  canal[0..15] | tipo[16..17] | audioScale[18]
    float    opacity;      // 52
    float    rotSpeed;      // 56  rad/s (sierras y objetos que giran)
    float    pad;          // 60
};
static_assert(sizeof(GpuSprite) == 64);

// Estado por combinación de grupos: matriz afín + alpha + pulso.
struct alignas(16) GpuCombo {
    float m[4];      // (a,b,c,d):  x' = a*x + c*y + tx ;  y' = b*x + d*y + ty
    float t[2];      // (tx,ty)
    float alpha;     // producto de la opacidad de todos sus grupos
    float pad;
    float pulse[4];  // rgb + cantidad de mezcla (pulse triggers)
};
static_assert(sizeof(GpuCombo) == 48);

// Un work group procesa un rango contiguo del lote ordenado y escribe su draw command.
struct alignas(16) GpuWorkGroup {
    uint32_t first;    // primer slot (en el orden de dibujo)
    uint32_t count;    // cuántos sprites
    uint32_t outBase;  // dónde escribe en el raw sprite buffer
    uint32_t pad;
};
static_assert(sizeof(GpuWorkGroup) == 16);

// Offsets std430 que el compute shader espera (ver structs Sprite/Combo/WG en Shaders.hpp).
// Si alguien reordena un campo y esto deja de cumplirse, falla al compilar en vez de dibujar basura.
static_assert(offsetof(GpuSprite, startPos) == 0 && offsetof(GpuSprite, halfSize) == 8 &&
              offsetof(GpuSprite, uvA) == 16 && offsetof(GpuSprite, uvEy) == 32 &&
              offsetof(GpuSprite, startRot) == 40 && offsetof(GpuSprite, groupCombo) == 44 &&
              offsetof(GpuSprite, meta) == 48 && offsetof(GpuSprite, opacity) == 52 &&
              offsetof(GpuSprite, rotSpeed) == 56);
static_assert(offsetof(GpuCombo, m) == 0 && offsetof(GpuCombo, t) == 16 &&
              offsetof(GpuCombo, alpha) == 24 && offsetof(GpuCombo, pulse) == 32);
static_assert(offsetof(GpuWorkGroup, first) == 0 && offsetof(GpuWorkGroup, count) == 4 &&
              offsetof(GpuWorkGroup, outBase) == 8);

constexpr size_t kRawSpriteSize  = 64;  // sizeof(Raw) en el shader
constexpr size_t kDrawCmdSize    = 20;  // DrawElementsIndirectCommand = 5 uints

// ---------------------------------------------------------------------------
// Datos de nivel en CPU
// ---------------------------------------------------------------------------
struct CpuSprite {
    GpuSprite  gpu{};
    int        zLayer = 0;
    int        zOrder = 0;
    uint8_t    sheetRank = 0;   // orden de hoja de sprites (GJ_GameSheet, 02, Glow, Fire, Particle, Pixel)
    uint32_t   texture = 0;     // nombre GL de la textura
    SpriteType type = SpriteType::Base;
    uint16_t   channel = 0;
};

struct ComboInfo {
    std::vector<int> groups;       // ids de grupo de la combinación
    void*  refObject = nullptr;    // GameObject* de referencia (lee su pose real cada frame)
    float  startPos[2]{0, 0};
    float  startRot = 0;
};

struct LevelData {
    std::vector<CpuSprite> sprites;
    std::vector<ComboInfo> combos;  // combos[0] = sin grupos (identidad)
};

}  // namespace gpur
