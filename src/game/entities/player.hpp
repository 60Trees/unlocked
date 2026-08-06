#pragma once

#include <game/base/entity.hpp>

namespace Game {
    struct Player : Entity {
        Entity::PosData get_defaults() const override;
        std::string name() const override { return "player"; }
        void render(Base::Renderer& r, Base::Renderer::VertexLayer& layer) const override;
        void spawn() override;
    };
}  // namespace Game
