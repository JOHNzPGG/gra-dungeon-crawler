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
            // Usuwamy znak powrotu karetki '\r' (problem Windows vs Linux), 
            // który czasem psuje czytanie mapy
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }

        Level L;
        L.h = (int)lines.size();
        L.w = L.h ? (int)lines[0].size() : 0;

        // Domyœlne wartoœci (¿eby nie l¹dowaæ w nicoœci w razie b³êdu)
        L.cells.assign(L.w * L.h, Cell::Wall);
        L.player_x = 1;
        L.player_y = 1;
        L.player_start_yaw = 180.0f;

        for (int y = 0; y < L.h; ++y) {
            for (int x = 0; x < L.w; ++x) {
                if (x >= lines[y].size()) break;

                char c = lines[y][x];

                // 1. Domyœlnie zak³adamy, ¿e to pod³oga (chyba ¿e trafimy na #)
                // Wczeœniej by³o odwrotnie i to powodowa³o b³¹d!
                if (c == '#') {
                    L.cells[y * L.w + x] = Cell::Wall;
                }
                else if (c == 'N') {
                    L.cells[y * L.w + x] = Cell::NextLevel;
                }
                else if (c == 'E') {
                    L.cells[y * L.w + x] = Cell::Exit;
                }
                else {
                    // Kropka, Spacja, Wrogowie, Gracz -> Wszystko stoi na pod³odze
                    L.cells[y * L.w + x] = Cell::Floor;
                }

                // 2. Parsowanie obiektów
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

                // --- TU BY£ B£¥D: Teraz 'S' i 'Z' s¹ ju¿ oznaczone jako Floor wy¿ej ---
                else if (c == 'S') {
                    L.enemy_spawns.push_back({ x, y, 'S' });
                }
                else if (c == 'Z') {
                    L.enemy_spawns.push_back({ x, y, 'Z' });
                }
                else if (c == 'I') {
                    L.item_spawns.push_back(glm::ivec2(x, y));
                }
            }
        }

        return L;
    }

} // namespace dungeon::io