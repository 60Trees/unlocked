#pragma once

#include "base.hpp"
#include <utils.hpp>

extern "C" Base::BaseClass* GetFpsCounter();
namespace Base {
    struct FpsCounter : BaseClass {
        void init() override;
        /// Updates delta time
        void loop() override;
        void quit() override;

        double deltaTime = 0;
        inline const float fps() const noexcept { return _FPS; }

        protected:
        float _FPS;

        friend int ::main();

        public:
        inline static std::unique_ptr<FpsCounter> get() {
            std::unique_ptr<FpsCounter> retval = nullptr;
            retval.reset(dynamic_cast<FpsCounter*>(GetFpsCounter()));
            return retval;
        };
    };
}  // namespace Base
