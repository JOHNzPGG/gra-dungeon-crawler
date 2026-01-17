#include "dungeon/io/MapLoader.hpp"
#include <fstream>
#include <stdexcept>
#include <vector>
#include <string>

namespace dungeon::io {

    Level load_map_ascii(const std::string& path) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("Cannot open map: " + path);

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty()) lines.push_back(line);
        }

        Level L;
        L.h = (int)lines.size();
        L.w = L.h ? (int)lines[0].size() : 0;
        L.cells.assign(L.w * L.h, Cell::Wall); // Domyœlnie wszystko jest œcian¹

        for (int y = 0; y < L.h; ++y) {
            for (int x = 0; x < L.w; ++x) {
                // Zabezpieczenie, gdyby linijka by³a krótsza
                if (x >= lines[y].size()) break;

                char c = lines[y][x];

                // --- PARSOWANIE ZNAKÓW ---
                if (c == '#') {
                    L.cells[y * L.w + x] = Cell::Wall;
                }
                else {
                    // Wszystko inne to pod³oga (Floor), chyba ¿e nadpiszemy ni¿ej
                    L.cells[y * L.w + x] = Cell::Floor;

                    if (c == '@') {
                        L.player_x = x;
                        L.player_y = y;
                    }
                    else if (c == 'S') {
                        // Szkielet
                        L.enemy_spawns.push_back({ x, y, 'S' });
                    }
                    else if (c == 'Z') {
                        // Zombie
                        L.enemy_spawns.push_back({ x, y, 'Z' });
                    }
                    else if (c == 'I') {
                        L.item_spawns.push_back(glm::ivec2(x, y));
                    }
                    else if (c == 'N') {
                        L.cells[y * L.w + x] = Cell::NextLevel; // Przejœcie
                    }
                    else if (c == 'E') {
                        L.cells[y * L.w + x] = Cell::Exit; // Koniec gry
                    }
                }
            }
        }

        return L;
    }

} // namespace dungeon::io