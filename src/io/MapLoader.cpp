#include "dungeon/io/MapLoader.hpp"
#include <fstream>
#include <stdexcept>
#include <vector>
#include <string>

namespace dungeon::io {

    /**
     * @brief Główna funkcja parsująca plik mapy w formacie ASCII.
     * * Funkcja czyta plik linia po linii i konwertuje znaki na obiekty gry.
     * Mapa jest spłaszczana do jednowymiarowej tablicy `L.cells`.
     * * @details LEGENDA MAPY ASCII:
     * - `#` : Ściana (Cell::Wall)
     * - `.` : Podłoga (Cell::Floor)
     * - `@`, `v`, `^`, `<`, `>` : Start gracza i jego rotacja
     * - `L` : Pochodnia (Zagadka) - Traktowana jako ściana
     * - `T` : Płyta naciskowa (Zagadka) - Traktowana jako podłoga
     * - `N` : Przejście do następnego poziomu (Next Level)
     * - `E` : Wyjście z gry (Exit/Win)
     * - `S`, `Z` : Spawny wrogów (Szkielet, Zombie)
     * - `P`, `M`, `I` : Przedmioty (Potion, Miecz, Item)
     * - `1`, `2`, `3` : Specjalne tiery broni
     * * @param path Ścieżka względna lub bezwzględna do pliku mapy (np. "assets/maps/level1.map").
     * @return Level Struktura zawierająca pełne dane poziomu.
     * @throws std::runtime_error Gdy plik nie zostanie znaleziony.
     */
    Level load_map_ascii(const std::string& path) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("Cannot open map: " + path);

        std::vector<std::string> lines;
        std::string line;

        // Wczytywanie linii i usuwanie znaków końca linii (Windows CR+LF fix)
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }

        Level L;
        L.h = (int)lines.size();
        L.w = L.h ? (int)lines[0].size() : 0;

        // Inicjalizacja pustej mapy samymi ścianami
        L.cells.assign(L.w * L.h, Cell::Wall);

        // Domyślna pozycja startowa (bezpiecznik)
        L.player_x = 1;
        L.player_y = 1;
        L.player_start_yaw = 180.0f;

        for (int y = 0; y < L.h; ++y) {
            for (int x = 0; x < L.w; ++x) {
                if (x >= lines[y].size()) break;

                char c = lines[y][x];

                // 1. Parsowanie Terenu
                if (c == '#') {
                    L.cells[y * L.w + x] = Cell::Wall;
                }
                else if (c == 'L') {
                    L.cells[y * L.w + x] = Cell::Wall; // Pochodnia blokuje ruch
                    L.puzzle_torches.push_back({ x, y, 'L' });
                }
                else if (c == 'T') {
                    L.cells[y * L.w + x] = Cell::Floor; // Płyta pozwala chodzić
                    L.pressure_plates.push_back({ x, y, 'T' });
                }
                else if (c == 'N') {
                    L.cells[y * L.w + x] = Cell::NextLevel;
                }
                else if (c == 'E') {
                    L.cells[y * L.w + x] = Cell::Exit;
                }
                else {
                    L.cells[y * L.w + x] = Cell::Floor; // Domyślnie podłoga
                }

                // 2. Parsowanie Gracza (Kierunek patrzenia)
                if (c == '@' || c == 'v') {
                    L.player_x = x; L.player_y = y; L.player_start_yaw = 180.0f;
                }
                else if (c == '^') {
                    L.player_x = x; L.player_y = y; L.player_start_yaw = 0.0f;
                }
                else if (c == '<') {
                    L.player_x = x; L.player_y = y; L.player_start_yaw = 90.0f;
                }
                else if (c == '>') {
                    L.player_x = x; L.player_y = y; L.player_start_yaw = 270.0f;
                }

                // 3. Parsowanie Encji (Wrogowie i Przedmioty)
                else if (c == 'S') {
                    L.enemy_spawns.push_back({ x, y, 'S' });
                }
                else if (c == 'Z') {
                    L.enemy_spawns.push_back({ x, y, 'Z' });
                }
                else if (c == 'I') {
                    L.item_spawns.push_back({ x, y, 'I' });
                }
                else if (c == '1' || c == '2' || c == '3') {
                    L.item_spawns.push_back({ x, y, c }); // Bronie Tier 1-3
                }
                else if (c == 'P') {
                    L.item_spawns.push_back({ x, y, 'P' }); // Potion
                }
                else if (c == 'M') {
                    L.item_spawns.push_back({ x, y, 'M' }); // Standardowy miecz
                }
            }
        }

        return L;
    }

} // namespace dungeon::io