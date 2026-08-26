#pragma once

#include <cstdint>

#include <base/base.hpp>

#include <game/base/entity.hpp>
#include "LDtkLoader/DataTypes.hpp"
#include <map>
#include <utils.hpp>

namespace Game {
    union RGBA {
        uint32_t rgba;
        struct {
            uint8_t r, g, b, a;
        } split;
        constexpr static uint32_t RED = 0xff0000ff;
        constexpr static uint32_t GREEN = 0x00ff00ff;
        constexpr static uint32_t BLUE = 0x0000ffff;
        constexpr static uint32_t BLACK = 0x000000ff;

        void set_opaque() { split.a = 0xff; }

        // unions are cool. (:
    };

    struct PuzzleState : Base::BaseClass {
        void init() override {}
        void loop() override {}
        void quit() override {}

        std::map<RGBA, bool> active_colours{};
    };

    struct PuzzleObject : Entity {
        std::string name() const override { return "PuzzleObject"; }

        /// Initializes the colour
        void spawn(const ldtk::Entity* e = nullptr) override {
            if (!e) return;

            const auto field = e->getField<ldtk::FieldType::Color>("Color");
            const auto& colourstruct = field.value_or(ldtk::Color{0, 0, 0, 255});
            colour.split = {colourstruct.r, colourstruct.g, colourstruct.b, 255};
        };
        void tick(double) override { colour.set_opaque(); }

        virtual bool get_state() const = 0;

        bool does_render() const override { return true; }
        Direction get_direction() const override { return RIGHT; }

        RGBA colour;
    };

}  // namespace Game
