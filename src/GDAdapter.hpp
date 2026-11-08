#pragma once
#include "Types.hpp"
#include <Geode/Geode.hpp>

// ÚNICO punto que conoce los internos de Geometry Dash. Si un binding cambia
// entre versiones, solo hay que arreglar GDAdapter.cpp (busca "VERIFY").
namespace gpur::gd {

// Recorre los objetos del nivel y extrae sprites, combinaciones de grupos, etc.
LevelData collect(GJBaseGameLayer* layer);

// Color (rgba) y flag de blending de los 1101 canales, tal como están AHORA.
void fetchChannels(GJBaseGameLayer* layer, float* rgba, std::bitset<kColorChannels>& blending);

// Opacidad actual de un grupo (producto de los alpha triggers).
float groupAlpha(GJBaseGameLayer* layer, int group);

// Pulse triggers que apuntan a un GRUPO: true si el grupo está pulsando ahora; rgb = color del
// pulso (0..1) y amount = cuánto se mezcla (0..1). (Los pulse triggers sobre canales de color
// ya llegan por fetchChannels.) STUB: de momento siempre false.
bool groupPulse(GJBaseGameLayer* layer, int group, float rgb[3], float& amount);

// Estado del objeto de referencia de una combinación.
bool groupEnabled(void* refObject);                          // toggle triggers
bool objectPose(void* refObject, float& x, float& y, float& rotRad);

// Escala actual de los objetos que "rebotan" con el audio.
float audioScale();

// z-order de la capa de objetos al que corresponde un ZLayer.
int batchZOrder(GJBaseGameLayer* layer, int zLayer);

}  // namespace gpur::gd
