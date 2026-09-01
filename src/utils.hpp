#pragma once

constexpr bool X_AXIS = 0, Y_AXIS = 1;
constexpr bool LEFT = 0, RIGHT = 1;
using Direction = bool;

// <AI>
#include <cstdint>
#include <memory>
#include <map>
#include <stdexcept>
#include <print>
#include <ostream>
#include <sstream>
#include <functional>
#include "base/base.hpp"
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

#define _REGISTERABLE(className)                                                                                   \
    public:                                                                                                        \
    inline static void register_new(const std::string& name, void* (*constructor)()) {                             \
        get_registries()[name] = reinterpret_cast<className* (*)()>(constructor);                                  \
    }                                                                                                              \
    [[nodiscard]] inline static className* make_new(const std::string& name) {                                     \
        if (!get_registries().contains(name)) return nullptr;                                                      \
        return get_registries()[name]();                                                                           \
    }                                                                                                              \
    [[nodiscard]] inline static bool contains(const std::string& name) { return get_registries().contains(name); } \
                                                                                                                   \
    private:                                                                                                       \
    static std::map<std::string, className* (*)()>& get_registries() {                                             \
        static std::map<std::string, className* (*)()> registry{};                                                 \
        return registry;                                                                                           \
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

#define _REGISTER_FOR(value, name, registerable_classname)             \
    ;                                                                  \
    static ::hidden ::Registerer _concat(__register_for, __COUNTER__){ \
        [] -> void* { return value; }, [](void* (*factory)()) { registerable_classname ::register_new(name, factory); }};

#define unimplemented_code throw std::logic_error("Unimplemented function reached")

// <AI>
#ifndef NDEBUG
#    if defined(__x86_64__) || defined(__i386__)
#        define DEBUG_BREAK() asm volatile("int3")
#    elif defined(__aarch64__)
#        define DEBUG_BREAK() asm volatile("brk #0")
#    else
#        include <csignal>
#        define DEBUG_BREAK() raise(SIGTRAP)
#    endif
#else
#    define DEBUG_BREAK()                                                                                                        \
        do {                                                                                                                     \
            std::print("Debug break! But *gasp* there is no debugger! Too bad. L\n{}:{}\n", __builtin_LINE(), __builtin_FILE()); \
        } while (0)
#endif

#define CASSERT(x)         \
    do {                   \
        if (!(x)) {        \
            DEBUG_BREAK(); \
        } else {           \
        }                  \
    } while (0)

#ifdef NDEBUG
#    define ASSUME(x) [[assume(x)]]
#    define DBGASSERT(x)
#else
#    define ASSUME(x)          \
        do {                   \
            if (!(x)) {        \
                DEBUG_BREAK(); \
            } else {           \
                [[assume(x)]]; \
            }                  \
        } while (0)
#    define DBGASSERT(x)       \
        do {                   \
            if (!(x)) {        \
                DEBUG_BREAK(); \
            } else {           \
            }                  \
        } while (0)
#endif
// </AI>

#define GETTER_DEFINITION(base_class, func_name) extern "C" base_class* func_name(bool create = false);
#define GETTER_DEFINITION_NO_DEFAULT_ARG(base_class, func_name) extern "C" base_class* func_name(bool create);
#define GETTER_IMPL(base_class, func_name, derived_class) \
    extern "C" base_class* func_name(bool create) {       \
        if (create) return new derived_class();           \
        static derived_class* val = nullptr;              \
        if (!val) val = new derived_class();              \
        return val;                                       \
    }

std::string runtime_datetime();
void welcome_message();

#define _nodisc [[nodiscard]]
#define _i inline
#define _nodisc_i _nodisc _i

GETTER_DEFINITION_NO_DEFAULT_ARG(Base::BaseClass, GetApplication);
void handle_loop(std::function<bool()> loop);

#ifdef NDEBUG
#    define debug_screen(entry, string) \
        do {                            \
        } while (0)
#else
extern "C" void add_debug_stream(const char* entry, size_t entrylen, const char* content, size_t contentlen);
#    define DEBUG_SCREEN true
#    define debug_screen(entry, content)                                                                                \
        do {                                                                                                            \
            ::std::ostringstream _entry;                                                                                \
            _entry << entry;                                                                                            \
            ::std::ostringstream _content;                                                                              \
            _content << content;                                                                                        \
            ::add_debug_stream(_entry.str().data(), _entry.str().size(), _content.str().data(), _content.str().size()); \
        } while (0)
#endif

#define runtime_warn_count(msg, max_count)     \
    do {                                       \
        static unsigned int warning_count = 0; \
        if (warning_count < max_count) {       \
            std::cout << msg << std::endl;     \
            warning_count++;                   \
        }                                      \
    } while (0)
#define runtime_warn(msg) runtime_warn_count(msg, 1)

void handle_loop(std::function<bool()> loop, std::function<void()> quit);

std::string uint8_to_hex_string(const uint8_t* v, const size_t s);

template <typename T>
inline std::string number_to_hex_string(const T& num) {
    return uint8_to_hex_string((uint8_t*)(&num), sizeof(T));
}

