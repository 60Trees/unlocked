#pragma once

#include <cstdint>

#include <base/base.hpp>

#include <game/base/entity.hpp>
#include "LDtkLoader/DataTypes.hpp"
#include "base/app.hpp"
#include <map>
#include <optional>
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

        inline auto operator<=>(const RGBA& other) const { return rgba <=> other.rgba; }

        // unions are cool. (:
    };

    struct PuzzleState : Base::AppModule {
        void init() override {}
        void loop() override {}
        void quit() override {}

        std::map<RGBA, bool> active_colours{};
    };

    struct PuzzleObject : Entity {
        std::string name() const override { return "PuzzleObject"; }

        /// Initializes the colour
        void spawn(Base::Application& app, const ldtk::Entity* e = nullptr) override {
            Entity::spawn(app, e);

            app.ensure_class_added<PuzzleState>([] { return new PuzzleState(); });

            if (!e) return;

            data.pos.x = e->getPosition().x;
            data.pos.y = -e->getPosition().y;

            const auto field = e->getField<ldtk::FieldType::Color>("Color");
            const auto& colourstruct = field.value_or(ldtk::Color{0, 0, 0, 255});
            colour.split = {colourstruct.r, colourstruct.g, colourstruct.b, 255};
        };

        void tick(Base::Application& app, double) override {
            colour.set_opaque();
            // for (auto& Base)
            auto& state = app.get<PuzzleState>();

            if (get_state()) state.active_colours[colour] = *get_state();

            for (const auto [colour, value] : state.active_colours) {
                debug_screen("colour" << colour.rgba,
                    "Colour " << number_to_hex_string<uint32_t>(colour.rgba, 3) << " is " << (value ? "on" : "off"));
            }
        }

        virtual std::optional<bool> get_state() const = 0;

        bool does_render() const override { return true; }
        Direction get_direction() const override { return RIGHT; }

        RGBA colour;
    };

}  // namespace Game
