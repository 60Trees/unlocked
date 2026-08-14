#include "entity.hpp"
#include <game/base/world_handler.hpp>
#include <optional>
#include "game/anim.hpp"
#include "game/anim.hpp"

using namespace std;
using namespace Base;

void Game::Entity::render(Base::Renderer& r, Base::Renderer::VertexLayer& layer, double deltaTime) const {
    constexpr bool solitaire_mode = false;

    const auto worldspace = r.builtin_worldspace_vshader();
    const auto uispace = r.builtin_uispace_vshader();
    const auto textured = r.builtin_textured_pshader();
    const auto coloured = r.builtin_coloured_pshader();
    layer.material.pixel_shader = textured;
    layer.material.vertex_shader = worldspace;
    layer.material.blend_mode = Renderer::Alpha;

    if (!solitaire_mode) layer.vertices.clear();

    const AnimationFrame anim_frame = current_movement->anim_frame(this);

    const glm::vec<2, uint> bottom_middle = anim_frame.bottom_middle.value_or(glm::vec<2, uint>{anim_frame.size.x / 2, 0});

    Renderer::TexturedRectDescriptor rect{};

    // world position: anchor `bottom_middle` (local to the frame) onto data.pos,
    // matching how Hitbox::pos already means "bottom center" for collision purposes
    const auto dopos = [&](const double num) -> float {
        if (anim_frame.snap_to_pixel_grid) return mth::round(num);
        return num;
    };

    rect.pos.l = dopos(data.pos.x - bottom_middle.x);
    rect.pos.r = dopos(rect.pos.l + anim_frame.size.x);
    rect.pos.b = dopos(data.pos.y - (int)bottom_middle.y);
    rect.pos.t = dopos(rect.pos.b + anim_frame.size.y);

    const uint16_t uv_l = anim_frame.top_left.x;
    const uint16_t uv_t = anim_frame.top_left.y;
    const uint16_t uv_r = anim_frame.top_left.x + anim_frame.size.x;
    const uint16_t uv_b = anim_frame.top_left.y + anim_frame.size.y;

    const bool flip_lr = anim_frame.direction != current_movement->direction;

    rect.uv.l = flip_lr ? uv_r : uv_l;
    rect.uv.r = flip_lr ? uv_l : uv_r;
    rect.uv.t = uv_t;
    rect.uv.b = uv_b;

    rect.atlas_index = r.getTextureID(anim_frame.tileset);
    // std::print("Atlas index={}\n", rect.atlas_index);
    rect.layer = 0;

    Renderer::make_textured_square(rect, layer.vertices);
}
