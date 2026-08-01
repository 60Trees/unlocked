#pragma once

// <AI>
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
