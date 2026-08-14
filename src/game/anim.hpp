#pragma once

#include <optional>
#include <string>
#include <glm/vec2.hpp>
#include <vector>
#include "utils.hpp"

namespace Game {
    struct AnimationFrame {
        glm::vec<2, uint> top_left;
        glm::vec<2, uint> size;
        // If nullopt, will automatically determine the bottom middle
        std::optional<glm::vec<2, uint>> bottom_middle = std::nullopt;

        inline bool operator==(const AnimationFrame& oth) const = default;
    };
    struct AnimationType {
        std::string tileset;

        std::vector<AnimationFrame> frames;

        Direction sprite_facing = RIGHT;

        // <=0 means no animation (uses frames[0])
        float seconds_per_frame = 0;

        bool should_stop_on_last_frame = false;
        // If nullopt, will simply mirror the `frames`.
        // Else, it will be the sprites but mirrored.
        // @note `frames.size()` must equal `flipped_frames.size()`
        std::optional<std::vector<AnimationFrame>> flipped_frames = std::nullopt;

        inline bool operator==(const AnimationType& oth) const = default;
    };
}  // namespace Game
