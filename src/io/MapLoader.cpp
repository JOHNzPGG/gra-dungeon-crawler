#include "dungeon/io/MapLoader.hpp"
#include <fstream>
#include <stdexcept>

namespace dungeon::io {

    Level load_map_ascii(const std::string& path) {
        std::ifstream f(path);
        if (!f) {
            throw std::runtime_error("Cannot open map: " + path);
        }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty())
                lines.push_back(line);
        }

        Level L;
        L.h = static_cast<int>(lines.size());
        L.w = L.h ? static_cast<int>(lines[0].size()) : 0;
        L.cells.assign(L.w * L.h, Cell::Wall);

        for (int y = 0; y < L.h; ++y) {
            for (int x = 0; x < L.w; ++x) {
                char c = lines[y][x];
                if (c == '#') {
                    L.cells[y * L.w + x] = Cell::Wall;
                }
                else {
                    L.cells[y * L.w + x] = Cell::Floor;
                    if (c == '@') {
                        L.player_x = x;
                        L.player_y = y;
                    }
                }
            }
        }
        return L;
    }

} // namespace dungeon::io
