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

    // Nowa struktura: Trzyma pozycje ORAZ typ wroga (np. 'S', 'Z')
    struct EnemySpawn {
        int x, y;
        char type;
    };

    struct ItemSpawn {
        int x, y;
        char type;
    };

    struct Level {
        int w = 0;
        int h = 0;
        std::vector<Cell> cells;
        int player_x = 1;
        int player_y = 1;

        float player_start_yaw = 180.0f;

        std::vector<EnemySpawn> enemy_spawns;
        std::vector<ItemSpawn> item_spawns;
        std::vector<ItemSpawn> puzzle_torches;  // Dla 'L'
        std::vector<ItemSpawn> pressure_plates; // Dla 'T'
    };

    Level load_map_ascii(const std::string& path);

} // namespace dungeon::io