#include "base.hpp"
#include <utility>

struct NullBaseClass : Base::BaseClass {
    void init() override {}
    void loop() override {}
    void quit() override {}

    bool is_null() override { return true; }
};

Base::BaseClass&& Base::BaseClass::get_null() {
    return std::move(*new NullBaseClass());
}
