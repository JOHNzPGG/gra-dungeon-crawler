#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace dungeon::io {

    // Dodano NextLevel i Exit
    enum class Cell : unsigned char {
        Floor = 0,
        Wall = 1,
        NextLevel = 2,
        Exit = 3
    };

    // Nowa struktura: Trzyma pozycjê ORAZ typ wroga (np. 'S', 'Z')
    struct EnemySpawn {
        int x, y;
        char type;
    };

    struct Level {
        int w = 0;
        int h = 0;
        std::vector<Cell> cells;
        int player_x = 1;
        int player_y = 1;

        // Zmieniono z vector<ivec2> na vector<EnemySpawn>
        std::vector<EnemySpawn> enemy_spawns;
        std::vector<glm::ivec2> item_spawns;
    };

    Level load_map_ascii(const std::string& path);

} // namespace dungeon::io