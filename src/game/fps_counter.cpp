#include <sys/types.h>
#include <base/fps_counter.hpp>
#include <utils.hpp>

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
    static float high = 0;
    static float high_time_ago = 0;
    static float low = 0;
    static float low_time_ago = 0;

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

    const float fps = 1000 / deltaTime;
    if (fps > high || high_time_ago > 3) {
        high = fps;
        high_time_ago = 0;
    } else high_time_ago += deltaTime;

    if (fps < low || low_time_ago > 3) {
        low = fps;
        low_time_ago = 0;
    } else low_time_ago += deltaTime;

#define colourize(fps_in) (fps_in < 60 ? "\\red" : "\\green") << fps_in << "\\normal"
#define sf(in) (in < 60 ? in : round(in))

    debug_screen("FPS", "FPS: " << colourize(sf(fps)) << "\nHigh: " << colourize(sf(high)) << "\nLow: " << colourize(sf(low)));

    seconds_since_start += deltaTime;
    while (seconds_since_start > 1e6) seconds_since_start -= 1e6;
}

void Base::FpsCounter::quit() {}
// </AI>
