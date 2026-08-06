#include "player.hpp"
#include <base/renderer.hpp>

_REGISTER_FOR(new Game::Player(), "player", Game::Entity)

using namespace std;
using namespace Base;
using Vertex = Renderer::Vertex;
using Material = Renderer::Material;

Game::Entity::PosData Game::Player::get_defaults() const {
    return {
        .pos = {},
        .vel = {0, 0},
        .size = {10, 15},
        .speed = 30,
    };
}

void Game::Player::spawn() {
    Game::Entity::spawn();
    struct TestAnim : EntityAnim {};
}

void Game::Player::render(Base::Renderer& r, Base::Renderer::VertexLayer& layer) const {
    const auto worldspace = r.builtin_worldspace_vshader();
    const auto uispace = r.builtin_uispace_vshader();
    const auto textured = r.builtin_textured_pshader();
    const auto coloured = r.builtin_coloured_pshader();

    layer.material.pixel_shader = coloured;
    layer.material.vertex_shader = worldspace;
    layer.material.blend_mode = Renderer::Alpha;
    layer.vertices.clear();
    Renderer::make_coloured_square(
        {
            .pos = {(float)data.left_edge(), (float)data.top_edge(), (float)data.right_edge(), (float)data.bottom_edge()},
            .colours = {0xffff00ff, 0x00ffffff, 0x00ff00ff, 0x000000ff},
        },
        layer.vertices);
}
