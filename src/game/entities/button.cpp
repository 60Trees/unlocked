#include <game/base/entity.hpp>
#include <utils.hpp>
#include "base/app.hpp"
#include "puzzle_aspects.hpp"

using namespace Game;
using namespace Base;
using namespace std;

struct Activatable : PuzzleObject {
    bool active = false;
    bool default_val = false;

    bool get_state() const override { return active == default_val; }

    Hitbox get_defaults() const override { return {{16, 16}}; };
    std::string name() const override { return "button"; }
    void spawn(Base::Application& app, const ldtk::Entity* e) override {
        PuzzleObject::spawn(app, e);
        if (!e) return;

        const auto field = e->getField<ldtk::FieldType::Bool>("DefaultValue");
        if (!field.is_null()) this->default_val = field.value();
        active = default_val;
    };

    void tick(Base::Application& app, double deltaTime) override { PuzzleObject::tick(app, deltaTime); }
};

struct Push : Activatable {};
struct Toggle : Activatable {};

_REGISTER_FOR(new Push(), "PushButton", Entity)
_REGISTER_FOR(new Toggle(), "ToggleButton", Entity)
