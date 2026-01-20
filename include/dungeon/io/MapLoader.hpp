#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace dungeon::io {

    /**
     * @brief Typ komórki w mapie poziomu
     *
     * Reprezentuje różne elementy planszy: podłogę, ściany, przejścia itp.
     */
    enum class Cell : unsigned char {
        Floor = 0,     /**< Podłoga */
        Wall = 1,      /**< Ściana */
        NextLevel = 2, /**< Przejście do następnego poziomu */
        Exit = 3       /**< Wyjście z poziomu */
    };

    /**
     * @brief Pozycja spawnów wrogów wraz z ich typem
     */
    struct EnemySpawn {
        int x;    /**< Współrzędna X */
        int y;    /**< Współrzędna Y */
        char type; /**< Typ wroga (np. 'S' – skeleton, 'Z' – zombie) */
    };

    /**
     * @brief Pozycja spawnów przedmiotów
     */
    struct ItemSpawn {
        int x;    /**< Współrzędna X */
        int y;    /**< Współrzędna Y */
        char type; /**< Typ przedmiotu (np. 'P' – potion, 'L' – torch) */
    };

    /**
     * @brief Struktura przechowująca dane całego poziomu
     */
    struct Level {
        int w = 0; /**< Szerokość poziomu */
        int h = 0; /**< Wysokość poziomu */
        std::vector<Cell> cells; /**< Tablica komórek poziomu (rozmiar w*h) */

        int player_x = 1; /**< Startowa pozycja gracza X */
        int player_y = 1; /**< Startowa pozycja gracza Y */

        float player_start_yaw = 180.0f; /**< Domyślny kąt startowy gracza (w stopniach) */

        std::vector<EnemySpawn> enemy_spawns;      /**< Lista spawnów wrogów */
        std::vector<ItemSpawn> item_spawns;        /**< Lista spawnów przedmiotów */
        std::vector<ItemSpawn> puzzle_torches;    /**< Lista spawnów pochodni w puzzlach ('L') */
        std::vector<ItemSpawn> pressure_plates;   /**< Lista spawnów płyt naciskowych ('T') */
    };

    /**
     * @brief Ładuje mapę ASCII z pliku i zwraca strukturę Level
     * @param path Ścieżka do pliku mapy
     * @return Level Struktura reprezentująca poziom
     */
    Level load_map_ascii(const std::string& path);

} // namespace dungeon::io
