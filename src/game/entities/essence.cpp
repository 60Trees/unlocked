#pragma once

#include <cmath>
#include <complex>
#include <game/base/entity.hpp>
#include <iostream>
#include "base/app.hpp"
#include "glm/ext/vector_float2.hpp"
#include "utils.hpp"
#include <game/base/entity_list.hpp>

using namespace Game;
using namespace Base;
using namespace std;

struct Essence : Entity {
    Essence() { std::cout << "Essence " << this << ": " << *this << std::endl; }

    std::string name() const override { return "Essence"; }
    Hitbox get_defaults() const override { return {{4, 4}}; }
    bool does_render() const override { return true; }

    // 0-360
    float visual_rotation = 0;
    float triangle_side_length = 2;
    float visual_y_offset = 0;

    float orbit_around_radius = 0;
    float orbit_rotation = 0;

    glm::vec2 render_pos{};

    void tick(Base::Application& app, double dt) override {
        Entity::tick(app, dt);

        // TODO: (URGENT) Find out why this code crashes the entire computer
        // Not just the app, but the entire computer. It happens randomly,
        // from a few seconds to a minute (at most)
        // Trying to run in web browser might give a clue
        return;

        if (parent) {
            data.pos = parent->data.hitbox_center();

            data.pos.x -= data.size.x / 2;
            data.pos.y -= data.size.y / 2;

            orbit_around_radius = max(parent->data.size.x, parent->data.size.y) * 0.5;

            size_t own_index = 0;
            size_t triangle_count = 0;

            for (const Entity* sibling : parent->children) {
                const Essence* other_triangle = dynamic_cast<const Essence*>(sibling);
                if (!other_triangle) continue;
                if (this == sibling) own_index = triangle_count;
                triangle_count++;
            }

            float rotation_offset = [&] {
                //[[assume(triangle_count > 0)]];
                const float spacing = 360.0 / triangle_count;
                return spacing * own_index;
            }();

            auto& fps = app.get<Base::FpsCounter>();
            const auto& seconds_since_start = fps.seconds_since_start;

            constexpr float rps = -0.2;

            orbit_rotation = rotation_offset + seconds_since_start * (360 * rps);
            while (orbit_rotation > 360) orbit_rotation -= 360;
            while (orbit_rotation < 0) orbit_rotation += 360;
        } else {
            orbit_around_radius = 0;
            for (const auto& [i, e] : app.get<EntityList>())
                if (e->name() == "player" && e->colliding_with(this)) e->adopt(this);
        }
    }

    void render(Base::Application& app, Base::Renderer::VertexLayer& layer) override {
        auto& r = app.get<Base::Renderer>();
        auto& fps = app.get<Base::FpsCounter>();
        const auto& seconds_since_start = fps.seconds_since_start;

        using namespace Base;

        visual_y_offset = mth::sin(seconds_since_start * 2.5);

        // revolutions per second
        constexpr double rps = 0.5;

        visual_rotation += fps.deltaTime * (360 * rps);

        // TODO: (super fast) Learn how fmod works and how to properly do it
        while (visual_rotation > 360) visual_rotation -= 360;
        while (visual_rotation < 0) visual_rotation += 360;

        if (!does_render()) {
            layer.vertices.clear();
            return;
        }

        layer.material.pixel_shader = r.builtin_coloured_pshader();
        layer.material.vertex_shader = r.builtin_worldspace_vshader();
        layer.material.blend_mode = Renderer::Alpha;

        layer.vertices.clear();

        float s = triangle_side_length;
        float h = s * std::sqrt(3.f) / 2.f;

        struct V2 {
            float x, y;
        };

        V2 A = {-s / 2, -h / 3};
        V2 B = {s / 2, -h / 3};
        V2 C = {0, 2 * h / 3};

        float rad = visual_rotation * (M_PI / 180.f);
        float cs = std::cos(rad);
        float sn = std::sin(rad);

        auto rot = [&](V2 p) { return V2{p.x * cs - p.y * sn, p.x * sn + p.y * cs}; };

        // Orbit
        float orbit_rad = orbit_rotation * (M_PI / 180.f);
        float ocs = std::cos(orbit_rad);
        float osn = std::sin(orbit_rad);

        V2 orbit_offset = {orbit_around_radius * ocs, orbit_around_radius * osn};

        // Base center
        V2 center = {float(data.pos.x + data.size.x / 2.f), float(data.pos.y + data.size.y / 2.f + visual_y_offset)};

        // Apply orbit
        center.x += orbit_offset.x;
        center.y += orbit_offset.y;

        A = rot(A);
        A.x += center.x;
        A.y += center.y;
        B = rot(B);
        B.x += center.x;
        B.y += center.y;
        C = rot(C);
        C.x += center.x;
        C.y += center.y;

        std::array<Renderer::Vertex, 3> tris{};

        auto set = [&](Renderer::Vertex& v, V2 p) {
            v.pos.world.x = p.x;
            v.pos.world.y = p.y;
            v.shaderdata.rgba_combined = 0xff00ffff;  // yellow
        };

        set(tris[0], A);
        set(tris[1], B);
        set(tris[2], C);

        for (auto& t : tris) {
            t.pos.world.depth = 0.0f;
            const auto normalize_float = [](float& f) {
                if (isinf(f) || isnan(f)) f = 0.0f;
            };
            normalize_float(t.pos.world.x);
            normalize_float(t.pos.world.y);
            // that's literally everything in Renderer::Vertex
            // its all unions
            layer.vertices.push_back(t);
        }
    }
} _REGISTER_FOR(new Essence(), "Essence", Entity);
