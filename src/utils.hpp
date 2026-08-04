#pragma once

// <AI>
#include <cstdint>
#include <memory>
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
    return (static_cast<uint32_t>(r) << 24) |
           (static_cast<uint32_t>(g) << 16) |
           (static_cast<uint32_t>(b) <<  8) |
            static_cast<uint32_t>(a);
}

#define _disabled if constexpr (false)

