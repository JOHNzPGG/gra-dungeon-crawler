#pragma once
#include <string>

namespace dungeon::ui {

	/**
	 * @brief Struktura reprezentująca stan HUD (Heads-Up Display)
	 *
	 * Przechowuje informacje wyświetlane graczowi podczas gry:
	 * zdrowie, punkty akcji, logi i aktualna tura.
	 */
	struct HudState {
		int hp = 100;        /**< Punkty zdrowia gracza */
		int ap = 2;          /**< Punkty akcji */
		bool in_turn = true; /**< Czy gracz wykonuje swoją turę */
		std::string log;     /**< Tekstowy log wydarzeń (np. ataki, zbieranie przedmiotów) */
	};

	/**
	 * @brief Funkcja rysująca HUD na ekranie
	 * @param state Struktura z aktualnym stanem HUD
	 */
	void draw_hud(HudState& state);

} // namespace dungeon::ui
