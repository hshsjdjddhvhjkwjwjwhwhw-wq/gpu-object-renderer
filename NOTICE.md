# NOTICE — Bismuth Android Port

This project is a modified fork of **Bismuth** by **IliasHDZ** (https://github.com/IliasHDZ/Bismuth), distributed under the **GNU General Public License v3.0 (GPLv3)** with the original author's additional non-generative-AI level generation clause preserved in `LICENSE`.

## Summary of Changes for Android Port (OpenGL ES 3.1+ & Cross-Platform)
1. **Platform Layer (`src/gl.hpp`, `src/gl.cpp`, `src/common.cpp`, `src/cl.cpp`)**:
   - Replaced Windows-exclusive headers (`win32/CCGL.h`, `win32/CCEGLView.h`) with cross-platform Cocos2d/Geode headers and Android `<EGL/egl.h>` + `<GLES3/gl31.h>`.
   - Added `eglGetProcAddress` function loader on Android alongside `wglGetProcAddress` on Windows.
   - Guarded Windows-only `OpenCL.dll` core-count detection (`src/cl.cpp`) with `#ifdef GEODE_IS_WINDOWS` and provided a configurable Android fallback (`gpu_core_count` setting, defaulting to 256).
   - Added runtime OpenGL ES 3.1+ context detection (`isOpenGLES()`, `isSupportedGLContext()`) and automatic GL Probe logging on startup (`GL_VERSION`, `GL_RENDERER`, `GL_SHADING_LANGUAGE_VERSION`, `GL_MAX_SHADER_STORAGE_BLOCK_SIZE`, `glDispatchCompute`).
2. **Buffer & Profiler Layer (`src/renderer/Buffer.cpp`, `src/utils/GPUTimer.hpp`, `src/profiler.cpp`)**:
   - Adapted buffer mapping on Android to use `glMapBufferRange` (core in GLES 3.0+) instead of desktop-only `glMapBuffer`/`glGetBufferSubData`.
   - Guarded desktop-only `GL_TIME_ELAPSED` GPU timer queries on Android.
3. **GLES 3.1 Shader & Indirect Draw Pipeline (`src/renderer/Shader.cpp`, `src/renderer/ObjectBatch.cpp`, `resources/shaders/*`)**:
   - Configured `Shader.cpp` to emit `#version 310 es` with explicit `precision highp` qualifiers when running on OpenGL ES 3.1+, while preserving `#version 460` on desktop OpenGL.
   - Added `glDrawArraysIndirect` loop fallback when `glMultiDrawArraysIndirect` (`GL_EXT_multi_draw_indirect`) is unavailable on mobile GPUs, plus `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT)` after `glDispatchCompute`.
   - Fixed strict GLSL 310 ES unsigned/signed literal and bitwise type rules across `objectDrawing.vert`, `objectCulling.comp`, and shader headers.
4. **Build System & CI (`CMakeLists.txt`, `mod.json`, `.github/workflows/multi-platform.yml`)**:
   - Enabled `Android64`, `Android32`, and `Win64` targets in GitHub Actions and linked `EGL` and `GLESv3` on Android.
