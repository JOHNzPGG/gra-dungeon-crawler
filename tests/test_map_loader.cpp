#include "dungeon/io/MapLoader.hpp"
#include <cassert>
#include <iostream>
int main() {
	auto L = dungeon::io::load_map_ascii("assets/maps/level1.map");
	assert(L.w > 0 && L.h > 0);
	assert(L.cells.size() == (size_t)(L.w * L.h));
	std::cout << "map ok\\n";
	return 0;
}