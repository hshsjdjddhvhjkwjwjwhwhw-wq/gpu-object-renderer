#pragma once

#include <common.hpp>
#include <profiler.hpp>

class GPUTimer {
public:
    inline GPUTimer(const char* name)
        : name(name), time(0), queryObject(0)
    {
#ifndef GEODE_IS_ANDROID
        glGenQueries(1, &queryObject);
#endif
        profiler::addTimer(this);
    }

    inline ~GPUTimer() {
#ifndef GEODE_IS_ANDROID
        if (queryObject)
            glDeleteQueries(1, &queryObject);
#endif
        profiler::removeTimer(this);
    }

    inline void start() {
#ifndef GEODE_IS_ANDROID
        if (profiler::isTakingGPUTime)
            glBeginQuery(GL_TIME_ELAPSED, queryObject);
#endif
    }

    inline void stop() {
#ifndef GEODE_IS_ANDROID
        if (profiler::isTakingGPUTime) {
            glEndQuery(GL_TIME_ELAPSED);
            glGetQueryObjecti64v(queryObject, GL_QUERY_RESULT, &time);
        }
#endif
    }

    inline const char* getName() const { return name; }
    inline i64 getTime() const { return time; }

private:
    i64 time;
    const char* name;
    u32 queryObject;
};
