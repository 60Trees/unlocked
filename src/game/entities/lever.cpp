/**
 * @date 28/08/2026
 * @author 60Trees_ (github.com/60Trees)
 * @file src/game/entities/lever.cpp
 */

#include <algorithm>
#include <game/base/entity.hpp>
#include <game/base/entity_list.hpp>
#include <utils.hpp>
#include "game/anim.hpp"
#include "puzzle_aspects.hpp"

using namespace Game;
using namespace Base;
using namespace std;

struct Lever : PuzzleObject {
    Direction active_direction;
    Direction current_direction;
    bool get_state() const override { return active_direction == current_direction; }

    Hitbox get_defaults() const override { return {{12, 12}}; };
    std::string name() const override { return "switch"; }

    void spawn(const ldtk::Entity* e) override {
        PuzzleObject::spawn(e);
        if (!e) return;

        const auto dir = e->getField<ldtk::FieldType::Enum>("Direction");
        if (!dir.is_null()) this->current_direction = dir.value().name == "Right";
        const auto defval = e->getField<ldtk::FieldType::Bool>("State");
        if (!defval.is_null()) this->active_direction = defval.value() ? this->current_direction : !this->current_direction;
    }

    AnimationFrame get_anim_frame() const override {
        glm::vec<2, uint> top_left;
        top_left.y = (!get_state()) * 16;
        top_left.x = current_direction * 16;
        return {.top_left = top_left, .size = {16, 16}, .tileset = "assets/buttons_n_shi.png", .direction = RIGHT};
    }

    vector<size_t> colliding_with;

    void tick(double dt, EntityList& others) override {
        PuzzleObject::tick(dt, others);

        auto prev_colliding_with = std::move(colliding_with);
        colliding_with.clear();

        for (const auto& [i, e] : others)
            if (e.get() != this && dynamic_cast<Game::EntitySwitcher*>(e.get()) && e->colliding_with(this)) colliding_with.push_back(i);

        for (const auto i : colliding_with)
            if (!ranges::contains(prev_colliding_with, i))
                if ((others[i].data.vel.x > 0) != current_direction) {
                others[i].pause_time += 0;
            current_direction = !current_direction;
            }
    }
};

_REGISTER_FOR(new Lever(), "Lever", Entity)
