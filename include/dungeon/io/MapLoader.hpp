#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace dungeon::io {

    enum class Cell : unsigned char { Wall = 1, Floor = 0 };

    struct Level {
        int w = 0;
        int h = 0;
        std::vector<Cell> cells;
        int player_x = 1;
        int player_y = 1;

        std::vector<glm::ivec2> enemy_spawns;
        std::vector<glm::ivec2> item_spawns;
    };

    Level load_map_ascii(const std::string& path);

} // namespace dungeon::io
