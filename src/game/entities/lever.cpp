/**
 * @date 28/08/2026
 * @author 60Trees_ (github.com/60Trees)
 * @file src/game/entities/lever.cpp
 */

#include <game/base/entity.hpp>
#include <game/base/entity_list.hpp>
#include <utils.hpp>
#include "base/app.hpp"
#include "game/anim.hpp"
#include "puzzle_aspects.hpp"

using namespace Game;
using namespace Base;
using namespace std;

struct Lever : PuzzleObject {
    Direction active_direction;
    Direction current_direction;
    optional<bool> get_state() const override { return active_direction == current_direction; }

    Hitbox get_defaults() const override { return {{12, 12}}; };
    std::string name() const override { return "Lever"; }

    void spawn(Base::Application& app, const ldtk::Entity* e) override {
        PuzzleObject::spawn(app, e);
        if (!e) return;

        const auto dir = e->getField<ldtk::FieldType::Enum>("Direction");
        if (!dir.is_null()) this->current_direction = dir.value().name == "Right";
        const auto defval = e->getField<ldtk::FieldType::Bool>("State");
        if (!defval.is_null()) this->active_direction = defval.value() ? this->current_direction : !this->current_direction;
    }

    AnimationFrame get_anim_frame(Base::Application&) const override {
        glm::vec<2, uint> top_left;
        top_left.y = (active_direction != current_direction) * 16;
        top_left.x = current_direction * 16;
        return {.top_left = top_left, .size = {16, 16}, .tileset = "assets/buttons_n_shi.png", .direction = RIGHT};
    }

    void tick(Base::Application& app, double dt) override {
        PuzzleObject::tick(app, dt);

        for (const auto& [i, e] : app.get<EntityList>())
            if (e.get() != this && dynamic_cast<Game::EntitySwitcher*>(e.get()) && e->colliding_with(this)) {
                const auto vel = app.get<EntityList>()[i].data.vel.x;
                current_direction = vel == 0 ? current_direction : (vel > 0);
            }
    }
};

_REGISTER_FOR(new Lever(), "Lever", Entity)
