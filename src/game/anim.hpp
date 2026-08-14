#pragma once

#include <optional>
#include <glm/vec2.hpp>
#include <string>
#include "utils.hpp"

namespace Game {
    struct AnimationFrame {
        glm::vec<2, unsigned int> top_left;
        glm::vec<2, unsigned int> size;

        std::string tileset;

        Direction direction = RIGHT;

        bool snap_to_pixel_grid = true;

        // If nullopt, will automatically determine the bottom middle
        std::optional<glm::vec<2, unsigned int>> bottom_middle = std::nullopt;

        inline bool operator==(const AnimationFrame& oth) const = default;
    };
}  // namespace Game
