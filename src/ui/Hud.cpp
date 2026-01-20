#include "dungeon/ui/Hud.hpp"
#include <imgui.h>

namespace dungeon::ui {

    /**
     * @brief Rysuje HUD (Heads-Up Display) na ekranie
     *
     * Funkcja używa ImGui do wyświetlenia informacji o grze:
     * - HP (punkty zdrowia)
     * - AP (punkty akcji)
     * - Tryb tury gracza
     * - Log zdarzeń
     *
     * HUD jest wyświetlany w lewym górnym rogu ekranu z przezroczystym tłem.
     *
     * @param s Referencja do aktualnego stanu HUD (HudState)
     */
    void draw_hud(HudState& s) {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
        ImGui::SetNextWindowBgAlpha(0.35f);

        if (ImGui::Begin("HUD", nullptr,
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoTitleBar)) {
            ImGui::Text("HP: %d  AP: %d", s.hp, s.ap);
            ImGui::Separator();
            ImGui::Text("%s", s.in_turn ? "Tryb tury: TAK" : "Tryb tury: NIE");
            ImGui::Separator();
            ImGui::TextWrapped("%s", s.log.c_str());
            }
        ImGui::End();
    }

} // namespace dungeon::ui
