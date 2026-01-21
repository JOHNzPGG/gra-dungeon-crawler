/**
 * @file App_UI.cpp
 * @brief Modu³ interfejsu u¿ytkownika (UI).
 * Rysuje HUD, menu, ekrany ³adowania i koñcowe przy u¿yciu ImGui.
 */

#include "dungeon/core/App.hpp"
#include "dungeon/ui/Hud.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace dungeon {

    /**
     * @brief G³ówna funkcja rysuj¹ca UI w trakcie gry.
     * Wyœwietla: HUD, Pasek ¿ycia, Minimapê, Ekwipunek oraz Damage Flash.
     */
    void App::frame_ui() {
        ImGuiIO& io = ImGui::GetIO();

        // 1. HUD (Logi i status tury)
        dungeon::ui::HudState hud;
        hud.log = "Mapa: " + current_map_name_ + "\nWidok: ";
        hud.log += (camera_mode_ == CameraMode::FirstPerson ? "FPP" : "TPP");
        dungeon::ui::draw_hud(hud);

        // 2. PASEK ¯YCIA (Lewy Górny Róg)
        ImGui::SetNextWindowPos(ImVec2(130, 10));
        ImGui::SetNextWindowSize(ImVec2(220, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.5f));
        ImGui::Begin("HealthBar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        float hpFraction = (float)player_.health / (float)player_.maxHealth;
        ImVec4 hpColor = ImVec4(0.0f, 0.8f, 0.0f, 1.0f); // Zielony
        if (hpFraction < 0.5f) hpColor = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // ¯ó³ty
        if (hpFraction < 0.25f) hpColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Czerwony

        ImGui::Text("ZDROWIE:");
        ImGui::SameLine();
        ImGui::TextColored(hpColor, "%d / %d", player_.health, player_.maxHealth);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hpColor);
        ImGui::ProgressBar(hpFraction, ImVec2(-1, 15.0f), "");
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleColor();

        // 3. PANEL BOCZNY (MINIMAPA)
        float panelWidth = 200.0f;
        float padding = 10.0f;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panelWidth - padding, padding));
        ImGui::SetNextWindowSize(ImVec2(panelWidth, 0));

        ImGui::Begin("SidePanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        ImGui::Text("Minimap");

        // Rysowanie Minimapy (Custom DrawList)
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();

        float mapSize = 180.0f;
        int   viewRange = 5; // Zasiêg widzenia na mapie
        float tileSize = mapSize / (float)(viewRange * 2 + 1);

        // T³o mapy
        drawList->AddRectFilled(p, ImVec2(p.x + mapSize, p.y + mapSize), IM_COL32(0, 0, 0, 255));

        // Pêtla rysowania kafelków
        for (int dy = -viewRange; dy <= viewRange; ++dy) {
            for (int dx = -viewRange; dx <= viewRange; ++dx) {
                int wx = player_.GameX + dx;
                int wy = player_.GameY + dy;
                float sx = p.x + (dx + viewRange) * tileSize;
                float sy = p.y + (dy + viewRange) * tileSize;

                if (wx >= 0 && wx < level_.w && wy >= 0 && wy < level_.h) {
                    int idx = wy * level_.w + wx;
                    // Fog of War: Rysujemy tylko odwiedzone
                    if (!visited_cells_.empty() && visited_cells_[idx]) {
                        auto cell = level_.cells[idx];
                        ImU32 color = (cell == io::Cell::Wall) ? IM_COL32(100, 100, 100, 255) : IM_COL32(200, 200, 200, 255);
                        if (cell == io::Cell::Exit) color = IM_COL32(255, 215, 0, 255); // Z³ote wyjœcie
                        drawList->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + tileSize, sy + tileSize), color);
                    }
                }
            }
        }

        // Rysowanie przedmiotów na mapie
        for (const auto& wItem : world_items_) {
            if (!wItem.isAlive) continue;
            int dx = (int)wItem.position.x - player_.GameX;
            int dy = (int)wItem.position.z - player_.GameY;

            if (std::abs(dx) <= viewRange && std::abs(dy) <= viewRange) {
                int idx = (int)wItem.position.z * level_.w + (int)wItem.position.x;
                if (!visited_cells_.empty() && visited_cells_[idx]) {
                    float sx = p.x + (dx + viewRange) * tileSize;
                    float sy = p.y + (dy + viewRange) * tileSize;
                    float textOffsetX = tileSize * 0.25f;
                    float textOffsetY = tileSize * 0.1f;

                    if (wItem.itemData->type == ItemType::Weapon)
                        drawList->AddText(ImVec2(sx + textOffsetX, sy + textOffsetY), IM_COL32(255, 165, 0, 255), "M");
                    else
                        drawList->AddText(ImVec2(sx + textOffsetX, sy + textOffsetY), IM_COL32(255, 50, 255, 255), "P");
                }
            }
        }

        // Rysowanie wrogów (tylko widoczni i odwiedzeni)
        for (const auto* enemy : enemies_) {
            if (!enemy->IsAlive()) continue;
            int dx = enemy->GameX - player_.GameX;
            int dy = enemy->GameY - player_.GameY;
            if (std::abs(dx) <= viewRange && std::abs(dy) <= viewRange) {
                int idx = enemy->GameY * level_.w + enemy->GameX;
                if (!visited_cells_.empty() && visited_cells_[idx]) {
                    float sx = p.x + (dx + viewRange) * tileSize;
                    float sy = p.y + (dy + viewRange) * tileSize;
                    drawList->AddRectFilled(ImVec2(sx + 2, sy + 2), ImVec2(sx + tileSize - 2, sy + tileSize - 2), IM_COL32(255, 0, 0, 255));
                }
            }
        }

        // Gracz (centrum)
        float cx = p.x + viewRange * tileSize;
        float cy = p.y + viewRange * tileSize;
        drawList->AddRectFilled(ImVec2(cx + 3, cy + 3), ImVec2(cx + tileSize - 3, cy + tileSize - 3), IM_COL32(0, 255, 0, 255));

        drawList->AddRect(p, ImVec2(p.x + mapSize, p.y + mapSize), IM_COL32(255, 255, 255, 255));
        ImGui::Dummy(ImVec2(mapSize, mapSize + 10.0f));

        // 4. EKWIPUNEK
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Equipped:");
        if (player_.equippedWeapon) ImGui::BulletText("%s (DMG: %d)", player_.equippedWeapon->name.c_str(), player_.equippedWeapon->stats.damage);
        else ImGui::TextDisabled(" [No Weapon]");

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0, 0.8f, 1, 1), "Backpack:");
        if (!player_.inventory.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("([H] to use Potion)");
        }
        if (player_.inventory.empty()) ImGui::TextDisabled(" (Empty)");
        else {
            for (auto* item : player_.inventory) {
                if (item->type == ItemType::Consumable) ImGui::BulletText("%s (Heal: %d)", item->name.c_str(), item->stats.health);
                else ImGui::BulletText("%s", item->name.c_str());
            }
        }
        ImGui::End();

        // 5. MA£E MENU W GRZE (Toggle 'M')
        if (show_menu_) {
            ImGui::SetNextWindowSize(ImVec2(260, 140), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(200, 50), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Menu", &show_menu_)) {
                ImGui::Text("Ustawienia widoku");
                ImGui::Separator();
                int mode = (camera_mode_ == CameraMode::FirstPerson) ? 0 : 1;
                if (ImGui::RadioButton("First-person (FPP)", mode == 0)) mode = 0;
                if (ImGui::RadioButton("Third-person (TPP)", mode == 1)) mode = 1;
                camera_mode_ = (mode == 0) ? CameraMode::FirstPerson : CameraMode::ThirdPerson;
                ImGui::Separator();
                ImGui::Text("M - zamknij menu");
            }
            ImGui::End();
        }

        // 6. DAMAGE FLASH (Czerwony ekran przy obra¿eniach)
        if (player_.IsHurt()) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 0.0f, 0.0f, 0.4f));
            ImGui::Begin("##DamageFlash", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
            ImGui::End();
            ImGui::PopStyleColor();
        }
    }

    /**
     * @brief Rysuje menu g³ówne (Start, Opcje, Wyjœcie).
     */
    void App::render_main_menu() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGui::Begin("MainMenuOverlay", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        float titleScale = 4.0f;
        std::string titleText = "DUNGEON CRAWLER";
        ImGui::SetWindowFontScale(titleScale);
        float titleW = ImGui::CalcTextSize(titleText.c_str()).x;
        float titleX = (io.DisplaySize.x - titleW) * 0.5f;
        float titleY = io.DisplaySize.y * 0.15f;

        // Cieñ tekstu
        ImGui::SetCursorPos(ImVec2(titleX + 5, titleY + 5));
        ImGui::TextColored(ImVec4(0, 0, 0, 1), titleText.c_str());
        ImGui::SetCursorPos(ImVec2(titleX, titleY));
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), titleText.c_str());

        ImGui::SetWindowFontScale(1.5f);
        push_retro_style();

        float btnW = 300.0f;
        float btnH = 50.0f;
        float startY = io.DisplaySize.y * 0.4f;
        float spacing = 20.0f;
        float centerX = (io.DisplaySize.x - btnW) * 0.5f;

        ImGui::SetCursorPos(ImVec2(centerX, startY));
        if (ImGui::Button("GRAJ", ImVec2(btnW, btnH))) {
            state_ = GameState::Playing;
        }

        ImGui::SetCursorPos(ImVec2(centerX, startY + (btnH + spacing) * 1));
        if (ImGui::Button("OPCJE", ImVec2(btnW, btnH))) {
            previous_state_ = GameState::MainMenu;
            state_ = GameState::Options;
        }

        ImGui::SetCursorPos(ImVec2(centerX, startY + (btnH + spacing) * 2));
        if (ImGui::Button("AUTORZY (CREDITS)", ImVec2(btnW, btnH))) {
            state_ = GameState::Credits;
        }

        ImGui::SetCursorPos(ImVec2(centerX, startY + (btnH + spacing) * 3));
        if (ImGui::Button("WYJSCIE", ImVec2(btnW, btnH))) {
            glfwSetWindowShouldClose(window_, 1);
        }
        pop_retro_style(); // Wy³¹czamy styl kamienia

        ImGui::End();
    }

    void App::render_options_menu() {
        // ... (ustawienia okna jak wczeœniej) ...

        ImGui::Begin("Opcje", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        ImGui::Text("Dzwiek:");
        ImGui::Separator();

        // --- SUWAK G£OŒNOŒCI ---
        if (ImGui::SliderFloat("Glosnosc", &master_volume_, 0.0f, 1.0f)) {
            // Aktualizacja miniaudio w czasie rzeczywistym
            ma_engine_set_volume(&audio_engine_, master_volume_);
        }

        ImGui::Dummy(ImVec2(0, 20)); // Odstêp

        ImGui::Text("Kamera:");
        ImGui::Separator();

        // --- WYBÓR KAMERY ---
        int mode = (camera_mode_ == CameraMode::FirstPerson) ? 0 : 1;
        if (ImGui::RadioButton("First-person (FPP)", mode == 0)) mode = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("Third-person (TPP)", mode == 1)) mode = 1;
        camera_mode_ = (mode == 0) ? CameraMode::FirstPerson : CameraMode::ThirdPerson;

        ImGui::Dummy(ImVec2(0, 20));

        ImGui::Text("Grafika:");
        ImGui::Separator();
        // ... (Twoje przyciski rozdzielczoœci - zostaw je tutaj) ...

        ImGui::Separator();
        if (ImGui::Button("Wroc", ImVec2(100, 40))) {
            // Prosta logika powrotu:
            // Jeœli gra ma wczytany poziom (np. player ¿yje), wracamy do pauzy/gry?
            // Najbezpieczniej: Wracamy do MainMenu (jeœli przyszliœmy z MainMenu).
            // Ale jak jesteœmy w grze?
            // Dodaj zmienn¹ pomocnicz¹ w App.hpp: GameState previous_state_;
            // Ustawiaj j¹ przed wejœciem w Opcje.
            // Na razie dla testu:
            state_ = previous_state_;
        }

        ImGui::End();
    }

    void App::render_pause_menu() {
        // Styl taki sam jak Main Menu
        ImVec2 window_size(300, 300); // Mniejsze, zgrabne
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - window_size.x) * 0.5f, (io.DisplaySize.y - window_size.y) * 0.5f));
        ImGui::SetNextWindowSize(window_size);

        // PrzeŸroczyste t³o (¿eby widzieæ grê pod spodem)
        ImGui::SetNextWindowBgAlpha(0.7f);

        ImGui::Begin("PauseMenu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // Wyœrodkowanie przycisków
        float contentW = ImGui::GetContentRegionAvail().x;
        float btnW = 200.0f;
        float centerX = (contentW - btnW) * 0.5f;

        ImGui::SetCursorPosX(centerX);
        ImGui::Text("PAUZA");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 20));

        ImGui::SetCursorPosX(centerX);
        if (ImGui::Button("Wznow (Resume)", ImVec2(btnW, 40))) {
            state_ = GameState::Playing;
        }

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(centerX);
        if (ImGui::Button("Opcje", ImVec2(btnW, 40))) {
            previous_state_ = GameState::Paused;
            state_ = GameState::Options;
            // Tu przyda³oby siê zapisaæ previous_state_ = GameState::Paused;
        }

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(centerX);
        if (ImGui::Button("Menu Glowne", ImVec2(btnW, 40))) {
            state_ = GameState::MainMenu;
            // Opcjonalnie: reset_game() tutaj, jeœli chcesz wyjœæ ca³kowicie
        }

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(centerX);
        if (ImGui::Button("Wyjscie z gry", ImVec2(btnW, 40))) {
            glfwSetWindowShouldClose(window_, 1);
        }

        ImGui::End();
    }

    void App::render_game_over() {
        // Wycentrowane okno na œrodku ekranu
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        if (ImGui::Begin("Game Over", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            ImGui::SetWindowFontScale(2.0f);
            ImGui::Text("NIE ZYJESZ");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));

            if (ImGui::Button("Wroc do Menu Glownego", ImVec2(200, 50))) {
                state_ = GameState::MainMenu;
                reset_game(); // Przygotuj grê na now¹ sesjê
            }
        }
        ImGui::End();
    }

    void App::render_victory_screen() {
        ImGuiIO& io = ImGui::GetIO();
        // Wyœrodkowanie okna
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        if (ImGui::Begin("Victory", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
            // Z³oty napis
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
            ImGui::SetWindowFontScale(2.0f);
            ImGui::Text("ZWYCIESTWO!");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();

            ImGui::Text("Ukonczyles loch i znalazles wyjscie.");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));

            if (ImGui::Button("Wroc do Menu Glownego", ImVec2(200, 50))) {
                state_ = GameState::MainMenu;
                current_level_idx_ = 0; // Reset postêpu
                current_map_name_ = map_list_[0]; // Reset do mapy 1
                reset_game();
            }
        }
        ImGui::End();
    }

    void App::render_credits() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowBgAlpha(0.7f); // Pó³przezroczyste czarne t³o

        ImGui::Begin("Credits", nullptr, ImGuiWindowFlags_NoDecoration);

        // Tytu³
        ImGui::SetWindowFontScale(2.5f);
        std::string title = "TWORCY";
        float titleW = ImGui::CalcTextSize(title.c_str()).x;
        ImGui::SetCursorPosX((io.DisplaySize.x - titleW) * 0.5f);
        ImGui::SetCursorPosY(50);
        ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), title.c_str());

        ImGui::Dummy(ImVec2(0, 50));

        // Lista (Edytuj tutaj!)
        ImGui::SetWindowFontScale(1.5f);
        const char* names[] = {
            "PROGRAMOWANIE GLOWNE",
            "Jan Kantor",
            "Kacper Szkutnik",
            "Wiktor Dzik",
            "",
            "POMOC I DESIGN",
            "Gemini (Google)",
            "",
            "SILNIK DZWIEKU",
            "miniaudio",
            "",
            "GRAFIKA GUI",
            "ImGui",
        };

        for (const char* line : names) {
            float w = ImGui::CalcTextSize(line).x;
            ImGui::SetCursorPosX((io.DisplaySize.x - w) * 0.5f);

            // Jeœli linia jest pusta, robimy odstêp, jeœli nie - rysujemy
            if (strlen(line) == 0) ImGui::Dummy(ImVec2(0, 20));
            else ImGui::Text("%s", line);
        }

        // Przycisk powrotu na dole
        push_retro_style();
        float btnW = 200;
        ImGui::SetCursorPos(ImVec2((io.DisplaySize.x - btnW) * 0.5f, io.DisplaySize.y - 100));
        if (ImGui::Button("WROC", ImVec2(btnW, 50))) {
            state_ = GameState::MainMenu;
        }
        pop_retro_style();

        ImGui::End();
    }

    void App::render_loading_screen() {
        // PÊTLA RYSOWANIA (Rysujemy 3 razy, ¿eby wype³niæ Back Buffer i Front Buffer)
        // Dziêki temu mamy pewnoœæ, ¿e obraz trafi na ekran przed zamro¿eniem.
        for (int i = 0; i < 3; i++) {

            // 1. Rozmiar i Viewport
            int w, h;
            glfwGetFramebufferSize(window_, &w, &h);
            glViewport(0, 0, w, h);

            // 2. T£O (Ciemnoszare - dla testu, czy OpenGL dzia³a)
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // 3. Start ImGui
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Rêczny rozmiar dla pewnoœci
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2((float)w, (float)h);

            // 4. Okno z napisem
            ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

            if (ImGui::Begin("Loading", nullptr,
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoBackground))
            {
                ImGui::SetWindowFontScale(3.0f);
                ImGui::TextColored(ImVec4(1, 1, 1, 1), "LADOWANIE...");
            }
            ImGui::End();

            // 5. Render
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            // 6. Swap
            glfwSwapBuffers(window_);

            // 7. Wa¿ne: Przetwórz eventy, ¿eby okno "od¿y³o"
            glfwPollEvents();
        }

        // Czekamy chwilkê, ¿eby upewniæ siê, ¿e GPU skoñczy³o
        glFinish();
    }

    void App::push_retro_style() {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
    }

    void App::pop_retro_style() {
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);
    }

}