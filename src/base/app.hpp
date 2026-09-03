#pragma once

#include <concepts>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

#include <utils.hpp>
#include "base.hpp"
#include "renderer.hpp"
#include "fps_counter.hpp"

GETTER_DEFINITION(Base::BaseClass, GetApplication);

namespace Base {
    struct Application;

    struct AppModule : BaseClass {
        Application* parent = nullptr;
    };

    struct Application : BaseClass {
        protected:
        std::unique_ptr<Renderer> renderer = Renderer::get();
        std::unique_ptr<FpsCounter> fps_counter = FpsCounter::get();

        std::vector<std::unique_ptr<BaseClass>> classes;

        public:
        template <typename T>
            requires std::derived_from<T, BaseClass>
        [[nodiscard]] T& get() {
            if constexpr (std::is_same_v<T, FpsCounter>) {
                return *fps_counter;
            } else if constexpr (std::is_same_v<T, Renderer>) {
                return *renderer;
            } else {
                for (auto& uptr : classes) {
                    if (auto* c = dynamic_cast<T*>(uptr.get())) return *c;
                }
            }

            throw std::runtime_error("Application does not have " + std::string(typeid(T).name()) + ".");
        }

        template <typename T>
            requires std::derived_from<T, BaseClass>
        [[nodiscard]] const T& get() const {
            if constexpr (std::is_same_v<T, FpsCounter>) {
                return *fps_counter;
            } else if constexpr (std::is_same_v<T, Renderer>) {
                return *renderer;
            } else {
                for (const auto& uptr : classes) {
                    if (auto* c = dynamic_cast<const T*>(uptr.get())) return *c;
                }
            }

            throw std::runtime_error("Application does not have " + std::string(typeid(T).name()) + ".");
        }

        template <typename T>
            requires std::derived_from<T, BaseClass>
        void ensure_class_added(T* (*factory)()) {
            if (has_class<T>()) return;

            AppModule* instance = factory();
            if (!instance) throw std::runtime_error("Factory returned nullptr.");

            instance->parent = this;

            classes.emplace_back(instance);
        }

        template <typename T>
            requires std::derived_from<T, BaseClass>
        [[nodiscard]] bool has_class() const {
            if constexpr (std::is_same_v<T, FpsCounter> || std::is_same_v<T, Renderer>) {
                return true;
            }

            for (const auto& uptr : classes) {
                if (dynamic_cast<const T*>(uptr.get())) return true;
            }

            return false;
        }

        friend int ::main();

        inline static Application* get() { return dynamic_cast<Application*>(GetApplication()); }
    };
}  // namespace Base
