#include "Renderer.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;
using gpur::Renderer;

// ---------------------------------------------------------------------------
// Un nodo por ZLayer, hijo de m_objectLayer. Así el orden de dibujo respecto al
// jugador y a los demás elementos se mantiene. El primero que se dibuja cada
// frame dispara el compute shader; todos dibujan sus runs.
// ---------------------------------------------------------------------------
class GpuLayerNode : public CCNode {
    int m_zLayer = 0;

public:
    static GpuLayerNode* create(int zLayer) {
        auto* ret = new GpuLayerNode();
        if (ret && ret->init()) {
            ret->m_zLayer = zLayer;
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void draw() override {
        auto& r = Renderer::get();
        if (!r.active()) return;

        // en este punto cocos ya aplicó la transformación de la capa de objetos
        kmMat4 proj, mv, mvp;
        kmGLGetMatrix(KM_GL_PROJECTION, &proj);
        kmGLGetMatrix(KM_GL_MODELVIEW, &mv);
        kmMat4Multiply(&mvp, &proj, &mv);

        r.beginFrameOnce(mvp);
        r.drawLayer(m_zLayer, mvp);
    }
};

// usado por Renderer.cpp
CCNode* gpurMakeLayerNode(int zLayer) { return GpuLayerNode::create(zLayer); }

// ---------------------------------------------------------------------------
// Hooks
// NOTA: GJBaseGameLayer::updateVisibility NO se puede enganchar (definición inline en los bindings,
// error de compilación en Android32), así que no se omite: GD sigue haciendo su culling normal.
// VERIFY (Android): PlayLayer::setupHasCompleted y PlayLayer::onQuit deben tener dirección android32 y
// android64 en los bindings de Geode. Si alguna falta, el compilador de Android lo dirá al
// compilar ese hook (no hace falta cambiar nada más).
// ---------------------------------------------------------------------------
class $modify(GpuPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        if (Mod::get()->getSettingValue<bool>("enabled")) Renderer::get().onLevelLoaded(this);
    }

    void onQuit() {
        Renderer::get().onLevelUnloaded();
        PlayLayer::onQuit();
    }
};
