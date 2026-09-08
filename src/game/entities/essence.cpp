#pragma once

#include <game/base/entity.hpp>
#include <iostream>
#include "base/app.hpp"
#include "utils.hpp"

using namespace Game;

struct Essence : Entity {
    Essence() { std::cout << "Essence " << this << ": " << *this << std::endl; }

    std::string name() const override { return "Essence"; }
    Hitbox get_defaults() const override { return {{16, 16}, {20, -88}}; }
    bool does_render() const override { return true; }

    // 0-360
    float visual_rotation = 0;
    float triangle_side_length = 2;
    float visual_y_offset = 0;

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

        debug_screen(this, "Essence " << this << ":\n- Renders: " << does_render() << "\n- Pos: " << data.pos.x << "," << data.pos.y);

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

        V2 center = {(float)(data.pos.x + data.size.x / 2.f), (float)(data.pos.y + data.size.y / 2.f + visual_y_offset)};

        A = rot(A);
        A.x += center.x;
        A.y += center.y;
        B = rot(B);
        B.x += center.x;
        B.y += center.y;
        C = rot(C);
        C.x += center.x;
        C.y += center.y;

        std::array<Renderer::Vertex, 3> tris;

        auto set = [&](Renderer::Vertex& v, V2 p) {
            v.pos.world.x = p.x;
            v.pos.world.y = p.y;
            v.shaderdata.rgba_combined = 0xff00ffff;  // yellow
        };

        set(tris[0], A);
        set(tris[1], B);
        set(tris[2], C);

        for (auto& t : tris) layer.vertices.push_back(t);
    }
} _REGISTER_FOR(new Essence(), "Essence", Entity);
