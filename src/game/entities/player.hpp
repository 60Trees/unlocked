#pragma once

#include <game/base/entity.hpp>

namespace Game {
    struct Player : Entity {
        Hitbox get_defaults() const override;
        std::string name() const override { return "player"; }
        void render(Base::Renderer& r, Base::Renderer::VertexLayer& layer, double deltaTime) const override;
        void spawn() override;
        void tick_position(double deltaTime) override;
    };
}  // namespace Game
