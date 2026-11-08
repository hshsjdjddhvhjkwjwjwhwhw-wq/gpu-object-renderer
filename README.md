# GPU Object Renderer (mod de Geode para Geometry Dash 2.2074)

Mod de optimización basado en la explicación técnica de Bismuth (iliasHDZ):
https://github.com/iliasHDZ/Bismuth/blob/master/TechnicalExplanation.md

Reemplaza SOLO el render de objetos: visibilidad, color y transformaciones de grupo se
resuelven en la GPU con un compute shader. Los efectos de shader y lo demás del juego no se tocan.

**Versión: v0.2.3 (Windows + Android 32/64). v0.2.3 = v0.2.2 + ajustes y logs de DEPURACIÓN (no corrige aún los gráficos). Compilada en Android por GitHub Actions desde v0.2.2; v0.2.3 sin compilar todavía.**
Lee la sección [Estado real: qué está probado y qué no](#estado-real-qué-está-probado-y-qué-no) antes de confiar en nada.

## Plataformas

| Plataforma | API gráfica requerida | Ruta de dibujo |
|---|---|---|
| Windows (`win`) | OpenGL 4.3 | atributos instanciados + `glMultiDrawElementsIndirect` (como antes) |
| Android (`android32`, `android64`) | OpenGL ES 3.1 con compute y SSBO en vertex shader | SSBO en el vertex shader + un `glDrawElementsIndirect` por work group |

Si el dispositivo no cumple los requisitos, el mod se desactiva solo, lo dice en el log de Geode
y GD dibuja normal. La arquitectura NO cambió.

## Cambios de v0.2 por archivo

### `GLFuncs.hpp` (cargador de funciones GL)
- `#ifdef __ANDROID__`: usa `eglGetProcAddress` (con `dlsym` sobre `libGLESv3.so` como respaldo). `#ifdef _WIN32`: `wglGetProcAddress`, como antes.
- La comprobación de versión lee `GL_VERSION` en tiempo de ejecución: `OpenGL ES >= 3.1` o `OpenGL >= 4.3`.
- `glMultiDrawElementsIndirect` solo se carga en escritorio (ES 3.1 no la tiene). Se añadieron `glDrawElementsIndirect`, `glUniform1ui`, `glGetIntegeri_v` y `glDeleteProgram`.
- Se consultan con `glGetIntegerv` los límites del driver (invocaciones y tamaño de work group, memoria compartida, SSBO por etapa, bloque SSBO máximo, nº de work groups) y se escriben en el log.

### `Shaders.hpp` (un solo código fuente por plataforma)
- Un único texto GLSL; `shaders::build()` antepone `#version 430 core` (Windows) o `#version 310 es` + `precision highp float; precision highp int;` + `#define GPUR_ES` (Android).
- `local_size_x` ya no es 256 fijo: se inyecta `GPUR_TILE` según el límite del driver (ES 3.1 solo garantiza 128).
- Se añadió `memoryBarrierShared()` antes de cada `barrier()` (en ES `barrier()` solo sincroniza la ejecución).
- Draw command: en ES el último campo es `reservedMustBeZero` (no hay `baseInstance`), así que se escribe 0.
- Vertex shader en ES: lee el raw sprite buffer como SSBO (`binding = 5`) con `uBase + gl_InstanceID`. En escritorio sigue usando atributos instanciados.

### `Renderer.cpp / Renderer.hpp`
- `checkLimits()`: elige `local_size` (potencia de 2 ≤ límite del driver; si es < 64 se desactiva), comprueba memoria compartida, que haya ≥ 7 SSBO a la vez y, en ES, que el vertex shader admita SSBO (`GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS ≥ 1`; en ES 3.1 el mínimo de la especificación es **0**). Si algo falla, el mod se desactiva con un mensaje en el log.
- Se comprueba que el nivel quepa en un bloque SSBO (`GL_MAX_SHADER_STORAGE_BLOCK_SIZE`).
- Dibujo en ES: por cada run se fijan textura y blending UNA vez y se hace un `glDrawElementsIndirect` por work group, con el uniform `uBase` = primer sprite del grupo. En escritorio no cambia (un multi-draw por run).
- Work groups por defecto en móvil: ver abajo.
- Contexto EGL: en Android el sistema puede destruir el contexto al pasar la app a segundo plano. Se guarda `eglGetCurrentContext()`; si cambia, el renderer se desactiva (GD dibuja normal), no se llama a `glDelete*` con ids de un contexto muerto y se reinicializa al cargar el siguiente nivel.
- Tras dibujar se sigue llamando a `ccGLInvalidateStateCache()` (ver riesgo en la lista de pendientes).
- Diagnóstico: `glGetError` solo en los primeros 3 frames de cada nivel (no cada frame).
- Pulse triggers de grupo: `updateCombos()` compone los pulsos en `GpuCombo.pulse` (el shader ya lo mezclaba).

### `GDAdapter.cpp` (único archivo que toca internos de GD)
Cambios hechos tras contrastar los nombres con la documentación pública de Geode (https://docs.geode-sdk.org):
- **Corrección**: `ColorActionSprite` no tiene `m_blending`; ahora se lee `ColorActionSprite::m_colorAction->m_blending`.
- **Corrección**: `GameObject::m_childSprites` no aparece en la documentación; los sprites extra se obtienen recorriendo `getChildren()` del objeto.
- `usesAudioScale()` ahora devuelve `GameObject::m_usesAudioScale`.
- `groupPulse()` implementado con `GJEffectManager::m_pulseEffectMap` (desactivado hasta fijar `kPulseTypeColor`, ver pendientes).
- `buildRankMap()` prueba la clave de caché de textura con el nombre y con la ruta resuelta (en Android puede diferir).

### `mod.json`
- `gd`: `win`, `android32` y `android64`, todos `2.2074`.
- Nuevo ajuste `work-groups-mobile` (por defecto **64**, rango 16..4096), usado solo en Android. El ajuste `work-groups` (1024) pasa a ser solo de Windows.

**Por qué 64 y no 1024 en móvil:** en ES 3.1 no hay multi-draw, así que cada work group cuesta una llamada de dibujo desde la CPU en cada frame (y un `glUniform1ui`). 1024 llamadas por capa y frame es mucho para la CPU de un teléfono; 64 es un punto de partida razonable pero es una **estimación, no una medición**: pruébalo con 32, 64, 128 y 256 en tu dispositivo. Un work group más grande no es más lento por sí mismo (el compute procesa su rango por bloques de `local_size`), pero reduce el paralelismo si hay pocos.

### `CMakeLists.txt`
- Android: `target_link_libraries(... EGL GLESv3)`; Windows: `opengl32`.

### `.github/workflows/build.yml` (nuevo)
- Compila Windows, Android 32 y Android 64 en GitHub Actions y los combina en un único `.geode`.

### Sin cambios
`Types.hpp` (solo se añadieron `static_assert` de offsets; las estructuras siguen siendo GpuSprite 64 B, GpuCombo 48 B, GpuWorkGroup 16 B, alineadas a 16) y `main.cpp` (solo un comentario). La compactación ordenada con scan dentro del work group se conserva.

## Cómo compilar

Antes de nada, en `mod.json` cambia `tunombre` por tu nombre de desarrollador (el id debe ser `dev.nombre`).

### Opción recomendada: GitHub Actions (no hace falta compilar en el celular)
1. Crea un repositorio en GitHub y sube el contenido de esta carpeta (incluida `.github/`).
2. Ve a la pestaña **Actions** y ejecuta **Build Binaries** (o haz un push).
3. Cuando termine, descarga el artefacto **Build Output**: contiene el `.geode` con Windows + Android 32 + Android 64.
4. En el celular, copia el `.geode` a la carpeta de mods de Geode (en la app de Geode para Android, la carpeta `geode/mods`; la ruta exacta depende de tu instalación) y reinicia GD.

La plantilla del workflow está escrita de memoria a partir de la plantilla oficial de mods de Geode. Si falla por una acción o parámetro desactualizado, compárala con `.github/workflows/multi-platform.yml` de https://github.com/geode-sdk/example-mod y copia de ahí.

### Opción local (PC con el SDK instalado)
```
geode build                     # Windows (o la plataforma del PC)
geode build --platform android64
```
Para Android hace falta el NDK de Android y su variable de entorno; las opciones exactas de la CLI cambian entre versiones: consulta `geode build --help` y https://docs.geode-sdk.org. (No se probó.)

## Cómo comprobar que funciona (cuando lo compiles)
1. Abre un nivel y mira el log de Geode. Debe aparecer algo como `[gpu-renderer] listo: ruta OpenGL ES 3.1 ...` seguido de `activo: N sprites...`.
2. Si aparece `se usa el render normal de GD`, el mensaje dice el motivo exacto (versión GL, límite del driver, shader que no compila...). Envíame ese log.
3. Si ves `error de shader`, copia el texto completo: incluye el número de línea (cuenta las líneas de prefijo que antepone `shaders::build()`).
4. Si el juego se ve raro, desactiva "Activar render por GPU" en los ajustes del mod.

## Estado real: qué está probado y qué no

**Comprobado en este entorno (sin Geode ni GD):**
- `Renderer.cpp` (con `GLFuncs.hpp`, `Types.hpp`, `Shaders.hpp`) pasa `-fsyntax-only` con g++ 13 y clang++ en las dos ramas (`-D__ANDROID__` y `-D_WIN32`, estas contra cabeceras falsas de Geode/GL/EGL escritas a mano, no las reales).
- Los `static_assert` de tamaño y offset de las tres estructuras std430 se cumplen (y los tamaños también con un target de 32 bits, comprobado por separado con clang `i386`).
- Coherencia leída a mano entre shaders y host: bindings 0..6, nombres de uniforms y tamaño del draw command (5 uint = 20 B).

**NO comprobado:**
- Que algo compile contra el SDK y los bindings reales de Geode.
- `GDAdapter.cpp`, `main.cpp`: ni siquiera pasaron una comprobación de sintaxis (dependen de tipos reales de GD).
- Que los shaders compilen en ningún driver. No había validador GLSL; los escribí a mano.
- Cualquier ejecución: no se probó en Windows, ni en ningún Android, ni se midió rendimiento.

## Lo que sigue sin verificar (lista final)

Todo está marcado con `VERIFY` o `STUB` en el código.

**Riesgo alto (pueden impedir compilar o dibujar bien):**
1. `GJBaseGameLayer::m_objects` y `m_objectLayer`: la página de documentación salió truncada y no pude confirmarlos.
2. Direcciones `android32`/`android64` en los bindings para `PlayLayer::setupHasCompleted`, `PlayLayer::onQuit` y `GJBaseGameLayer::updateVisibility`. Si falta alguna, no compilará ese hook.
3. `typeinfo_cast<CCSprite*>` en `GDAdapter.cpp` (función de Geode que escribí de memoria) y `ColorActionSprite::m_colorAction` nulo/no nulo para canales sin acción.
4. Que los sprites extra de un objeto sean realmente hijos directos `CCSprite` de él (el cambio que sustituye a `m_childSprites`). Si no lo son, faltarán sprites.
5. `ccGLInvalidateStateCache()` en cocos2d-x 2.x parece vaciar también las pilas de matrices (`kmGLFreeAll`). Si tras dibujar la primera capa el HUD o los objetos se colocan mal, esta es la primera sospechosa (ver `restoreCocosState()` en `Renderer.cpp`).
6. Que el quad de `CCSprite::getQuad()` esté en coordenadas locales y contenga las UV finales (rotaciones y flips en la hoja), y las blend funcs (`kNormalSrc/Dst`, `kBlendSrc/Dst`).
7. En Android: que `GL_VERSION` empiece con "OpenGL ES" en el driver real, que `eglGetProcAddress` devuelva las funciones de ES 3.1 (el respaldo con `dlsym` ayuda), que GD use un contexto con ES 3.1 (si GD pide ES 2.0 el contexto no tendrá compute y el mod se desactivará solo), y que `ccGLInvalidateStateCache` sea suficiente con el estado que dejan los drivers móviles.
8. Rendimiento en móvil: el valor por defecto 64 de `work-groups-mobile` es una estimación. Omitir `updateVisibility` puede afectar a algo de gameplay.

**Riesgo medio (funciones incompletas, no rompen la compilación):**
9. `kPulseTypeColor` vale -1: los pulse triggers de grupo están desactivados porque no pude leer el enum `PulseEffectType`. Debes poner el valor del pulso de color. Además, `m_currentValue` se asume en 0..1 como cantidad de mezcla, y `m_mainOnly`/`m_detailOnly` se ignoran.
10. `audioScale()` devuelve 1 (STUB): los objetos de audio no rebotan aunque `usesAudioScale` ya funcione. Existen `m_minAudioScale`/`m_maxAudioScale` en `GameObject`, pero no sé de dónde sale el valor del audio.
11. `rotationSpeedOf()` devuelve 0 (STUB): las sierras no giran en la GPU. En `GameObject` no hay un miembro de velocidad de giro; probablemente esté en una subclase de rotación.
12. `GJSpriteColor::m_colorID`/`m_defaultColorID`, `groupAlpha` (`opacityModForGroup`) y `m_isGroupDisabled`: los nombres figuran en la documentación, pero no se comprobó su semántica.
13. Orden de las hojas de sprites (`kSheets`) y `batchZOrder()` (devuelve el zLayer a secas).

**No hecho (pedido como opcional):**
14. Interceptar triggers de movimiento/rotación para aplicarlos directo a `GpuCombo`. No lo toqué: requiere hooks sobre funciones cuyos nombres y firmas no pude verificar, y no quise inventarlos. Hoy la matriz de cada combinación se sigue derivando de la pose real de un objeto de referencia (la CPU de GD sigue moviendo los objetos).
15. Sprites animados, HSV por objeto, skew y flips manuales de hijos (heredado de v0.1).
16. Solo hay un nodo de render por ZLayer; no se probó el orden respecto al jugador.


## Depuración (v0.2.3)
Ajustes `debug-*` en Geode (todos apagados salvo el log). Sirven para separar los problemas visuales:
- `debug-log`: escribe en el log `[gpu-renderer]` muestras de sprites, hijos de `m_objectLayer` y el color de los canales más usados (frames 2 y 300).
- `debug-hide-vanilla`: oculta los `CCSpriteBatchNode` de `m_objectLayer` (se restauran al salir del nivel).
- `debug-flat-color`: todo lo que dibuja el mod sale magenta.
- `debug-collect-hidden`: recoge también los sprites que GD tenía invisibles al cargar.
Después de cambiar un ajuste hay que reiniciar GD y volver a entrar al nivel.
