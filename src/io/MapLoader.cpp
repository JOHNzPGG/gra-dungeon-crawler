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
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }

        Level L;
        L.h = (int)lines.size();
        L.w = L.h ? (int)lines[0].size() : 0;

        L.cells.assign(L.w * L.h, Cell::Wall);
        L.player_x = 1;
        L.player_y = 1;
        L.player_start_yaw = 180.0f;

        for (int y = 0; y < L.h; ++y) {
            for (int x = 0; x < L.w; ++x) {
                if (x >= lines[y].size()) break;

                char c = lines[y][x];

                if (c == '#') {
                    L.cells[y * L.w + x] = Cell::Wall;
                }
                else if (c == 'L') {
                    L.cells[y * L.w + x] = Cell::Wall; // POCHODNIA = ŚCIANA
                    L.puzzle_torches.push_back({ x, y, 'L' });
                }
                else if (c == 'T') {
                    L.cells[y * L.w + x] = Cell::Floor; // PŁYTA = PODŁOGA
                    L.pressure_plates.push_back({ x, y, 'T' });
                }
                else if (c == 'N') {
                    L.cells[y * L.w + x] = Cell::NextLevel;
                }
                else if (c == 'E') {
                    L.cells[y * L.w + x] = Cell::Exit;
                }
                else {
                    L.cells[y * L.w + x] = Cell::Floor;
                }

				// gracz i jego położenie
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

                // --- ENEMY I ITEMY ---
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
                    L.item_spawns.push_back({ x, y, c });
                }
                else if (c == 'P') {
                    L.item_spawns.push_back({ x, y, 'P' });
                }
                else if (c == 'M') {
                    L.item_spawns.push_back({ x, y, 'M' });
                }
            }
        }

        return L;
    }

} // namespace dungeon::io