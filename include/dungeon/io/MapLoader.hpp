#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace dungeon::io {

    /**
     * @brief Typ komórki logicznej na siatce mapy.
     */
    enum class Cell : unsigned char {
        Floor = 0,     ///< Pod³oga (przechodnia)
        Wall = 1,      ///< Œciana (blokuje ruch/wzrok)
        NextLevel = 2, ///< Portal do kolejnego poziomu
        Exit = 3       ///< Portal koñcz¹cy grê
    };

    /**
     * @brief Dane spawnu przeciwnika.
     */
    struct EnemySpawn {
        int x;     ///< Pozycja X
        int y;     ///< Pozycja Y
        char type; ///< Typ wroga ('Z'=Zombie, 'S'=Skeleton)
    };

    /**
     * @brief Dane spawnu przedmiotu lub obiektu interaktywnego.
     */
    struct ItemSpawn {
        int x;     ///< Pozycja X
        int y;     ///< Pozycja Y
        char type; ///< Typ: '1'-'3' (Bronie), 'P' (Potion), 'L' (Torch), 'T' (Plate)
    };

    /**
     * @brief Struktura przechowuj¹ca pe³ne dane wczytanego poziomu.
     */
    struct Level {
        int w = 0; ///< Szerokoœæ mapy
        int h = 0; ///< Wysokoœæ mapy
        std::vector<Cell> cells; ///< Siatka logiczna (flattened 2D array)

        int player_x = 1; ///< Startowa pozycja gracza X
        int player_y = 1; ///< Startowa pozycja gracza Y
        float player_start_yaw = 180.0f; ///< Startowy obrót gracza

        std::vector<EnemySpawn> enemy_spawns;      ///< Lista wrogów do zrespienia
        std::vector<ItemSpawn> item_spawns;        ///< Lista przedmiotów do zrespienia
        std::vector<ItemSpawn> puzzle_torches;     ///< Lista pochodni zagadek
        std::vector<ItemSpawn> pressure_plates;    ///< Lista p³yt naciskowych
    };

    /**
     * @brief Wczytuje mapê z pliku ASCII i parsuje j¹ do struktury Level.
     * @param path Œcie¿ka do pliku .map
     * @return Wczytany poziom
     * @throws std::runtime_error Gdy plik nie istnieje
     */
    Level load_map_ascii(const std::string& path);

} // namespace dungeon::io