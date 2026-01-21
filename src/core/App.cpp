/**
 * @file App.cpp
 * @brief Główny plik implementacji klasy App.
 * Odpowiada za cykl życia aplikacji: inicjalizację, główną pętlę i czyszczenie zasobów.
 */

#include "dungeon/core/App.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <imgui.h>
#include <cstdio>
#include <stdexcept>

namespace dungeon {

    /**
     * @brief Konstruktor aplikacji.
     * Inicjalizuje wszystkie podsystemy (GLFW, OpenGL, Audio, ImGui) oraz ładuje zasoby startowe.
     * @param cfg Konfiguracja okna (wymiary, tytuł).
     */
    App::App(const AppConfig& cfg) : cfg_(cfg), player_(1, 1, 0) {
        init_glfw();
        init_gl();
        init_imgui();
        init_audio();

        render_loading_screen(); // Wyświetla ekran ładowania podczas ciężkich operacji

        // Ładowanie zasobów gry
        load_level();
        build_world_mesh();
        build_cube_mesh();
        build_weapon_mesh();
        build_enemy_mesh();
        spawn_entities_from_level();

        // Nauka domyślnej umiejętności gracza
        player_.LearnSkill(new Skill("Strong Hit", 1, 15));
        if (!player_.skills.empty()) {
            player_.skills[0]->offsets.push_back({ 0, -1 });
        }
    }

    /**
     * @brief Destruktor aplikacji.
     * Odpowiada za zwolnienie pamięci GPU (bufory, tekstury) oraz zamknięcie bibliotek.
     */
    App::~App() {
        shutdown_imgui();

        // Usuwanie obiektów logicznych
        for (auto* e : enemies_) delete e;
        enemies_.clear();

        // --- Sprzątanie zasobów OpenGL (VBO/VAO) ---
        if (floor_vbo_) glDeleteBuffers(1, &floor_vbo_);
        if (floor_vao_) glDeleteVertexArrays(1, &floor_vao_);
        if (wall_vbo_)  glDeleteBuffers(1, &wall_vbo_);
        if (wall_vao_)  glDeleteVertexArrays(1, &wall_vao_);
        if (weapon_vbo_) glDeleteBuffers(1, &weapon_vbo_);
        if (weapon_vao_) glDeleteVertexArrays(1, &weapon_vao_);
        if (zombie_vao_) glDeleteVertexArrays(1, &zombie_vao_);
        if (zombie_vbo_) glDeleteBuffers(1, &zombie_vbo_);
        if (skeleton_vao_) glDeleteVertexArrays(1, &skeleton_vao_);
        if (skeleton_vbo_) glDeleteBuffers(1, &skeleton_vbo_);
        if (cube_vbo_) glDeleteBuffers(1, &cube_vbo_);
        if (cube_vao_) glDeleteVertexArrays(1, &cube_vao_);
        if (potion_vao_) glDeleteVertexArrays(1, &potion_vao_);
        if (potion_vbo_) glDeleteBuffers(1, &potion_vbo_);
        if (torch_vao_) glDeleteVertexArrays(1, &torch_vao_);
        if (torch_vbo_) glDeleteBuffers(1, &torch_vbo_);

        // Zamknięcie okna
        if (window_) {
            glfwDestroyWindow(window_);
            glfwTerminate();
        }

        // Sprzątanie Audio
        ma_sound_uninit(&bg_music_);
        ma_sound_uninit(&sfx_torch_);
        ma_engine_uninit(&audio_engine_);
    }

    /**
     * @brief Główna pętla gry (Game Loop).
     * Działa do momentu zamknięcia okna. Odpowiada za:
     * 1. Rozpoczęcie klatki (Input, Clear).
     * 2. Aktualizację logiki (np. walka, timery).
     * 3. Renderowanie odpowiedniego stanu gry (Menu, Gra, Pauza).
     * 4. Zakończenie klatki (Swap Buffers).
     */
    void App::run() {
        while (!glfwWindowShouldClose(window_)) {
            frame_begin();          // PollEvents + ImGui NewFrame
            update_audio_state();   // Zarządzanie dźwiękiem

            float dt = ImGui::GetIO().DeltaTime;

            // Timery dla animacji w menu
            if (state_ == GameState::MainMenu || state_ == GameState::Credits) {
                menu_timer_ += dt;
            }

            // Obsługa walki w czasie rzeczywistym (animacje)
            if (state_ == GameState::Playing && combat_lock_) {
                update_combat();
            }

            // Maszyna stanów renderowania
            switch (state_) {
            case GameState::MainMenu: frame_render(); render_main_menu(); break;
            case GameState::Credits:  frame_render(); render_credits(); break;
            case GameState::Options:  render_options_menu(); break;
            case GameState::Playing:  frame_render(); frame_ui(); break;
            case GameState::GameOver: frame_render(); render_game_over(); break;
            case GameState::Victory:  frame_render(); render_victory_screen(); break;
            case GameState::Paused:   frame_render(); render_pause_menu(); break;
            }

            frame_end(); // Render ImGui + SwapBuffers
        }
    }

    /**
     * @brief Ładuje teksturę z pliku na dysku do pamięci GPU.
     * Używa biblioteki stb_image.
     * @param path Ścieżka do pliku obrazka (png, jpg).
     * @return GLuint ID tekstury w OpenGL (lub 0 w przypadku błędu).
     */
    GLuint App::load_texture(const char* path) {
        int w, h, nrChannels;
        stbi_set_flip_vertically_on_load(true); // OpenGL ma Y=0 na dole
        unsigned char* data = stbi_load(path, &w, &h, &nrChannels, 0);

        if (!data) {
            std::fprintf(stderr, "Failed to load texture: %s\n", path);
            return 0;
        }

        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        // Ustawienia zawijania i filtrowania
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        stbi_image_free(data);
        return tex;
    }

    /**
     * @brief Tworzy jednokolorową teksturę 1x1 piksel.
     * Przydatne jako fallback, gdy model nie ma tekstury.
     */
    GLuint App::create_texture_from_color(float r, float g, float b) {
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        unsigned char data[3] = { (unsigned char)(r * 255.0f), (unsigned char)(g * 255.0f), (unsigned char)(b * 255.0f) };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        return tex;
    }

} // namespace dungeon