#include "dungeon/ui/Hud.hpp"
#include <imgui.h>

namespace dungeon::ui {

    /**
     * @brief Rysuje HUD (Heads-Up Display) na ekranie.
     * * Funkcja korzysta z biblioteki ImGui do wyrenderowania nak³adki informacyjnej.
     * Okno jest ustawione jako "AlwaysAutoResize" i posiada przezroczyste t³o,
     * aby nie zas³aniaæ widoku gry 3D.
     * * Wyœwietlane informacje:
     * - Stan tury (Gracz vs Przeciwnicy)
     * - Log zdarzeñ (ostatnie akcje, np. "Podniesiono miecz")
     * * @note HP i AP s¹ zakomentowane w kodzie, poniewa¿ mog¹ byæ rysowane przez paski graficzne w innej funkcji.
     * * @param s Referencja do struktury stanu HUD, zawieraj¹ca dane z silnika gry.
     */
    void draw_hud(HudState& s) {
        // Ustawienie pozycji startowej (lewy górny róg)
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
        // Ustawienie przezroczystoœci t³a (35% widocznoœci)
        ImGui::SetNextWindowBgAlpha(0.35f);

        if (ImGui::Begin("HUD", nullptr,
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoTitleBar)) {

            // Debug info (opcjonalne)
            // ImGui::Text("HP: %d  AP: %d", s.hp, s.ap);
            // ImGui::Separator();

            // Wyœwietlanie czyja jest tura
            if (s.in_turn) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "Tura Gracza");
            }
            else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Tura Wroga...");
            }

            ImGui::Separator();

            // Wyœwietlanie logu tekstowego (TextWrapped zawija d³ugie linie)
            ImGui::TextWrapped("%s", s.log.c_str());
        }
        ImGui::End();
    }

} // namespace dungeon::ui