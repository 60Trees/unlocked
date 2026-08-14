#pragma once

#include <game/base/entity.hpp>
#include <game/anim.hpp>

namespace Game {
    struct Player : Entity {
        Hitbox get_defaults() const override;
        std::string name() const override { return "player"; }
        void spawn() override;
    };
}  // namespace Game
