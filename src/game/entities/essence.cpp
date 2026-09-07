#pragma once

#include <game/base/entity.hpp>
#include <iostream>
#include "base/app.hpp"
#include "game/base/entity_list.hpp"
#include "utils.hpp"

using namespace Game;

struct Essence : Entity {
    Essence() { std::cout << "Essence " << this << ": " << *this << std::endl; }

    std::string name() const override { return "Essence"; }
    Hitbox get_defaults() const override { return {{16, 16}, {20, -88}}; }

    // 0-360
    float visual_rotation = 0;
    float triangle_side_length = 1;
    float visual_y_offset = 0;

    AnimationFrame get_anim_frame(Base::Application&) const override {
        glm::vec<2, uint> top_left;
        top_left.y = 16;
        top_left.x = 16;
        return {.top_left = top_left, .size = {16, 16}, .tileset = "assets/buttons_n_shi.png", .direction = RIGHT};
    }

    /*
    void render(Base::Application& app, Base::Renderer::VertexLayer& layer) const override {
        auto& r = app.get<Base::Renderer>();

        using namespace Base;

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

        V2 center = {2.f, 2.f + visual_y_offset};

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
            v.shaderdata.rgba_combined = 0xffff00ff;  // yellow
        };

        set(tris[0], A);
        set(tris[1], B);
        set(tris[2], C);

        for (auto& t : tris) layer.vertices.push_back(t);
    }
    //*/

} _REGISTER_FOR(new Essence(), "Essence", Entity);
