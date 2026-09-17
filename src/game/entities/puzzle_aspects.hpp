/**
 * @file src/game/entities/puzzle_aspects.hpp
 * @author 60Trees_ (github.com/60Trees)
 */

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
#include <glm/vec4.hpp>

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
        bool transfers_to_new_level() const override { return false; }

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

        // Straight 0..1 tint, refreshed from `colour` right before each draw. This has to be a
        // *member*, not a render()-local -- Material::params is a non-owning span the renderer
        // reads later in the frame, same reasoning as ditherBgParams in game.cpp.
        glm::vec4 tint_param{};
        void refresh_tint_param() {
            tint_param = {colour.split.r / 255.0f, colour.split.g / 255.0f, colour.split.b / 255.0f, colour.split.a / 255.0f};
        }

        // Drop-in replacement for builtin_textured_pshader(): identical sampling, but any
        // near-pure-red marker pixel in buttons_n_shi.png is swapped for tint_param, scaled by
        // the marker's own red channel so any shading baked into the marker survives as a
        // brightness variation on the tint instead of being flattened.
        static constexpr std::string_view kColourKeyedTexturedPS = R"(
            // USES_TEXTURES
            @group(1) @binding(0) var atlasTex: texture_2d<f32>;
            @group(1) @binding(1) var atlasSamp: sampler;
            @group(2) @binding(0) var<uniform> params: array<vec4f, 16>;
            @fragment fn fs_main(in: VSOut) -> @location(0) vec4f {
                let texel = textureSample(atlasTex, atlasSamp, in.uv / vec2f(textureDimensions(atlasTex)));
                let tint = params[0];
                let is_marker = texel.r > 0.5 && texel.g < 0.15 && texel.b < 0.15;
                let outRGB = select(texel.rgb, tint.rgb * texel.r, is_marker);
                return vec4f(outRGB, texel.a);
            }
        )";

        virtual Base::Renderer::Material get_material(Base::Application& app) override {
            auto& r = app.get<Base::Renderer>();
            Base::Renderer::Material retval;
            retval.pixel_shader = kColourKeyedTexturedPS;
            retval.vertex_shader = r.builtin_worldspace_vshader();
            retval.blend_mode = Base::Renderer::Alpha;
            refresh_tint_param();
            retval.params = std::as_bytes(std::span(&tint_param, 1));
            return retval;
        }
    };

}  // namespace Game
