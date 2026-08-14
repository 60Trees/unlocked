#include "game/anim.hpp"

using namespace Game;
using namespace Base;
using namespace std;

namespace PlayerMovements {
    AnimationType QuickTurnAnimation() {
        return {.tileset = "assets/player.png",
            .frames = {{{0, 2 * 16}, {16, 16}}, {{16, 2 * 16}, {16, 16}}},
            .sprite_facing = LEFT,
            .seconds_per_frame = 0.1,
            .should_stop_on_last_frame = true};
    }
    AnimationType WalkAnimation() {
        return {.tileset = "assets/player.png",
            .frames = {{{0, 16}, {16, 16}}, {{16, 16}, {16, 16}}, {{32, 16}, {16, 16}}},
            .sprite_facing = RIGHT,
            .seconds_per_frame = 0.1};
    }
    AnimationType IdleAnimation() { return {.tileset = "assets/player.png", .frames = {{{0, 0}, {16, 16}}}, .sprite_facing = RIGHT}; }
}  // namespace PlayerMovements
