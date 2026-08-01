#pragma once

#include <utils.hpp>
#include "base.hpp"
#include "renderer.hpp"
#include "fps_counter.hpp"

extern "C" Base::BaseClass* GetApplication();
namespace Base {
    struct Application : BaseClass {
        protected:
        /**
         * @brief The renderer pointer
         * @detail Used for rendering.
         */
        std::unique_ptr<Renderer> renderer = Renderer::get();
        /**
         * @brief The fps_counter pointer
         * @detail Used for things like delta time (fps_counter->loop() called every frame)
         */
        std::unique_ptr<FpsCounter> fps_counter = FpsCounter::get();

        friend int ::main();

        public:
        inline static std::unique_ptr<Application> get() {
            std::unique_ptr<Application> retval = nullptr;
            retval.reset(dynamic_cast<Application*>(GetApplication()));
            return retval;
        };
    };
}  // namespace Base
