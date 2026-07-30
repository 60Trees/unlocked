#pragma once

#ifdef __EMSCRIPTEN__
#    include <emscripten.h>
#endif

#include <functional>

inline void emswrapper_loop(std::function<bool()> loop) {
#ifdef __EMSCRIPTEN__
    static std::function<bool()> _loop = [] { return false; };
    _loop = loop;

    emscripten_set_main_loop(
        [] {
            if (!_loop()) emscripten_cancel_main_loop();
        },
        0, true);
#else
    while (loop());
#endif

}
