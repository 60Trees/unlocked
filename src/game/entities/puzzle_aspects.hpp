#pragma once

#include <cstdint>

#include <base/base.hpp>

#include <game/base/entity.hpp>
#include "LDtkLoader/DataTypes.hpp"
#include "LDtkLoader/Entity.hpp"
#include "base/app.hpp"
#include <iostream>
#include <map>
#include <optional>
#include <utils.hpp>

namespace Game {
    union RGBA {
        uint32_t rgba;
        struct Split {
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

        bool visible = true;

        bool does_render() const override { return visible; }

        // template <typename T>
        // inline static T getfield(const ldtk::Entity* e, std::string_view name, T _default) {
        //     try {
        //         return e->getField<T>(std::string{name}).value();
        //     } catch (std::invalid_argument) {
        //         return _default;
        //     }
        // }

        /// Initializes the colour
        void spawn(Base::Application& app, const ldtk::Entity* e = nullptr) override {
            Entity::spawn(app, e);

            app.ensure_class_added<PuzzleState>([] { return new PuzzleState(); });

            if (!e) return;

            std::cout << "Entity " << e->getName() << std::endl;
            const auto& colourstruct = e->getField<ldtk::FieldType::Color>("Color").value();
            colour.split = {colourstruct.r, colourstruct.g, colourstruct.b, 255};

            visible = !e->getField<ldtk::FieldType::Bool>("Invisible").value_or(false);
        };

        void tick(Base::Application& app) override {
            colour.set_opaque();
            // for (auto& Base)
            auto& state = app.get<PuzzleState>();

            if (get_state()) state.active_colours[colour] = *get_state();
        }

        virtual std::optional<bool> get_state() const = 0;

        Direction get_direction() const override { return RIGHT; }

        RGBA colour;
    };

}  // namespace Game
