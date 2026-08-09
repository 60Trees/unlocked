#include <sys/types.h>
#include <base/fps_counter.hpp>

#include <chrono>
#include <cmath>

GETTER_IMPL(Base::BaseClass, GetFpsCounter, Base::FpsCounter);

// <AI>
namespace {
    using clock_type = std::chrono::steady_clock;

    clock_type::time_point lastFrame;
    clock_type::time_point lastFpsUpdate;

    uint frameCount = 0;
}  // namespace

void Base::FpsCounter::init() {
    lastFrame = clock_type::now();
    lastFpsUpdate = lastFrame;

    deltaTime = NAN;
    _FPS = NAN;
    frameCount = 0;
}

void Base::FpsCounter::loop() {
    auto now = clock_type::now();

    deltaTime = std::chrono::duration<double>(now - lastFrame).count();
    lastFrame = now;

    frameCount++;

    double elapsed = std::chrono::duration<double>(now - lastFpsUpdate).count();
    if (elapsed >= 1.0) {
        _FPS = frameCount / elapsed;

        frameCount = 0;
        lastFpsUpdate = now;
    }
}

void Base::FpsCounter::quit() {}
// </AI>
