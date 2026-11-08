#include "Renderer.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
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
// VERIFY (Android): las tres funciones enganchadas abajo (PlayLayer::setupHasCompleted,
// PlayLayer::onQuit y GJBaseGameLayer::updateVisibility) deben tener dirección android32 y
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

class $modify(GpuBaseLayer, GJBaseGameLayer) {
    // Con el renderer activo se omiten los visibility checks de GD (la parte más lenta):
    // la GPU hace el culling. VERIFY: confirmar que esta función no hace nada más necesario
    // para el gameplay (activación de objetos, etc.).
    void updateVisibility(float dt) {
        if (Renderer::get().active()) return;
        GJBaseGameLayer::updateVisibility(dt);
    }
};
