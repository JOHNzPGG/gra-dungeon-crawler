#pragma once
#include <string>

namespace dungeon::ui {

	struct HudState {
		int hp = 10;
		int ap = 2;
		bool in_turn = false;
		std::string log;
	};

	void draw_hud(HudState& state);

} // namespace dungeon::ui
