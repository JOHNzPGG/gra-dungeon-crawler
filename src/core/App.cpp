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

    App::App(const AppConfig& cfg) : cfg_(cfg), player_(1, 1, 0) {
        init_glfw();
        init_gl();
        init_imgui();
        init_audio();

        render_loading_screen();

        load_level();
        build_world_mesh();
        build_cube_mesh();
        build_weapon_mesh();
        build_enemy_mesh();
        spawn_entities_from_level();

        // Domyślny skill
        player_.LearnSkill(new Skill("Strong Hit", 1, 15));
        if (!player_.skills.empty()) {
            player_.skills[0]->offsets.push_back({ 0, -1 });
        }
    }

    App::~App() {
        shutdown_imgui();

        for (auto* e : enemies_) delete e;
        enemies_.clear();

        // Sprzątanie GPU
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
        if (window_) {
            glfwDestroyWindow(window_);
            glfwTerminate();
        }

        // Sprzątanie Audio
        ma_sound_uninit(&bg_music_);
        ma_sound_uninit(&sfx_torch_);
        ma_engine_uninit(&audio_engine_);
    }

    void App::run() {
        while (!glfwWindowShouldClose(window_)) {
            frame_begin();
            update_audio_state();

            float dt = ImGui::GetIO().DeltaTime;
            if (state_ == GameState::MainMenu || state_ == GameState::Credits) {
                menu_timer_ += dt;
            }

            if (state_ == GameState::Playing && combat_lock_) {
                update_combat();
            }

            switch (state_) {
            case GameState::MainMenu: frame_render(); render_main_menu(); break;
            case GameState::Credits:  frame_render(); render_credits(); break;
            case GameState::Options:  render_options_menu(); break;
            case GameState::Playing:  frame_render(); frame_ui(); break;
            case GameState::GameOver: frame_render(); render_game_over(); break;
            case GameState::Victory:  frame_render(); render_victory_screen(); break;
            case GameState::Paused:   frame_render(); render_pause_menu(); break;
            }

            frame_end();
        }
    }

    GLuint App::load_texture(const char* path) {
        int w, h, nrChannels;
        stbi_set_flip_vertically_on_load(true);
        unsigned char* data = stbi_load(path, &w, &h, &nrChannels, 0);

        if (!data) {
            std::fprintf(stderr, "Failed to load texture: %s\n", path);
            return 0;
        }

        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
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