#pragma once
#include <string>
#include <vector>

namespace dungeon::io {

	enum class Cell : unsigned char { Wall = 1, Floor = 0 };

	struct Level {
		int w = 0;
		int h = 0;
		std::vector<Cell> cells;
		int player_x = 1;
		int player_y = 1;
	};

	/// Wczytuje mapê ASCII z pliku i zamienia na siatkê œcian/pod³ogi.
	/// Rzuca std::runtime_error, jeœli nie uda siê otworzyæ pliku.
	Level load_map_ascii(const std::string& path);

} // namespace dungeon::io
