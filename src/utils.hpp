#pragma once

// <AI>
#include <cstdint>
#include <memory>
#include <map>
#include <stdexcept>
template <typename Derived, typename Base>
std::unique_ptr<Derived> static_unique_ptr_cast(std::unique_ptr<Base>&& ptr) {
    return std::unique_ptr<Derived>(static_cast<Derived*>(ptr.release()));
}
// </AI>

#include <gcem.hpp>
namespace mth {
    using namespace gcem;
}

constexpr inline uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept {
    // bit format: uint8_t * 4 = uint32_t
    // rgba, #RRGGBBAA
    return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) | (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

#define _disabled if constexpr (false)

#define __concat(x, y) x##y
#define _concat(x, y) __concat(x, y)

namespace hidden {
    struct Registerer {
        using Factory = void* (*)();
        using Registrar = void (*)(Factory);
        inline Registerer(Factory f, Registrar r) { r(f); }
    };
}  // namespace hidden

#define _REGISTERABLE(className)                                                       \
    public:                                                                            \
    inline static void register_new(const std::string& name, void* (*constructor)()) { \
        get_registries()[name] = reinterpret_cast<className* (*)()>(constructor);      \
    }                                                                                  \
    [[nodiscard]] inline static className* make_new(const std::string& name) {         \
        if (!get_registries().contains(name)) return nullptr;                          \
        return get_registries()[name]();                                               \
    }                                                                                  \
                                                                                       \
    private:                                                                           \
    static std::map<std::string, className* (*)()>& get_registries() {                 \
        static std::map<std::string, className* (*)()> registry{};                     \
        return registry;                                                               \
    }

#define _REGISTERABLE_SINGLETON(className)                                             \
    public:                                                                            \
    inline static className& get(const std::string& name) {                            \
        static std::map<std::string, std::unique_ptr<className>> instances{};          \
        if (instances.contains(name)) return *instances.at(name);                      \
        instances[name] = std::unique_ptr<className>{className::make_new(name)};       \
        return *instances.at(name);                                                    \
    }                                                                                  \
    inline static void register_new(const std::string& name, void* (*constructor)()) { \
        get_registries()[name] = reinterpret_cast<className* (*)()>(constructor);      \
    }                                                                                  \
                                                                                       \
    private:                                                                           \
    [[nodiscard]] inline static className* make_new(const std::string& name) {         \
        if (!get_registries().contains(name)) return nullptr;                          \
        return get_registries()[name]();                                               \
    }                                                                                  \
    static std::map<std::string, className* (*)()>& get_registries() {                 \
        static std::map<std::string, className* (*)()> registry{};                     \
        return registry;                                                               \
    }

#define _REGISTER_FOR(value, name, registerable_classname)           \
    static ::hidden ::Registerer _concat(__register_for, __COUNT__){ \
        [] -> void* { return new Game::Player(); }, [](void* (*factory)()) { Game ::Entity ::register_new("player", factory); }};

#define unimplemented_code throw std::logic_error("Unimplemented function reached")

