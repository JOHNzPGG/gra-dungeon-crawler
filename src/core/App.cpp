#include "dungeon/core/App.hpp"
#include "dungeon/ui/Hud.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stdexcept>
#include <cstdio>
#include <vector>
#include <filesystem>

static void glfw_error_cb(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

// Vertex shader – pozycja + UV
static const char* kWorldVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoord;

out vec2 vTexCoord;
uniform mat4 uProj;
uniform mat4 uView;

void main() {
  gl_Position = uProj * uView * vec4(aPos, 1.0);
  vTexCoord = aTexCoord;
}
)";

// Fragment shader – z samplera uTex
static const char* kWorldFS = R"(#version 330 core
out vec4 FragColor;
in vec2 vTexCoord;

uniform sampler2D uTex;

void main() {
  FragColor = texture(uTex, vTexCoord);
}
)";

namespace dungeon {

    App::App(const AppConfig& cfg)
        : cfg_(cfg),
        player_(1, 1, 0)   // startowe x, y, yaw
    {
        init_glfw();
        init_gl();
        init_imgui();
        load_level();
        build_world_mesh();
    }

    App::~App() {
        shutdown_imgui();

        if (floor_vbo_) glDeleteBuffers(1, &floor_vbo_);
        if (floor_vao_) glDeleteVertexArrays(1, &floor_vao_);
        if (wall_vbo_)  glDeleteBuffers(1, &wall_vbo_);
        if (wall_vao_)  glDeleteVertexArrays(1, &wall_vao_);

        if (window_) {
            glfwDestroyWindow(window_);
            glfwTerminate();
        }
    }

    void App::init_glfw() {
        glfwSetErrorCallback(glfw_error_cb);
        if (!glfwInit())
            throw std::runtime_error("GLFW init failed");

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        window_ = glfwCreateWindow(cfg_.width, cfg_.height, cfg_.title.c_str(), nullptr, nullptr);
        if (!window_)
            throw std::runtime_error("Window creation failed");

        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1);
    }

    void App::init_gl() {
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
            throw std::runtime_error("GLAD load failed");

        glViewport(0, 0, cfg_.width, cfg_.height);
        glEnable(GL_DEPTH_TEST);

        float aspect = static_cast<float>(cfg_.width) / static_cast<float>(cfg_.height);
        proj_ = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

        world_shader_ = gfx::Shader(kWorldVS, kWorldFS);

        wall_texture_ = load_texture("assets/textures/sciana.png");
        floor_texture_ = load_texture("assets/textures/podloga.png");
    }

    void App::init_imgui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        ImGui_ImplOpenGL3_Init("#version 330");
    }

    void App::shutdown_imgui() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void App::load_level() {
        namespace fs = std::filesystem;

        std::string path = "assets/maps/test.map";
        current_map_name_ = path;

        if (!fs::exists(path)) {
            throw std::runtime_error(
                "Cannot open map: " + path +
                "\nCurrent directory: " + fs::current_path().string());
        }

        level_ = io::load_map_ascii(path);

        // startowa pozycja z mapy
        player_.GameX = level_.player_x;
        player_.GameY = level_.player_y;
        player_.yaw = 0; // 0° = "północ"
        player_.RenderPosition = glm::vec3(player_.GameX, 0.0f, player_.GameY);
    }

    bool App::can_move_to(int x, int y) const {
        if (x < 0 || y < 0) return false;
        if (x >= level_.w || y >= level_.h) return false;

        auto cell = level_.cells[y * level_.w + x];
        return cell != io::Cell::Wall;
    }

    void App::handle_input() {
        bool left = (glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS);
        bool right = (glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS);
        bool up = (glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS);
        bool m = (glfwGetKey(window_, GLFW_KEY_M) == GLFW_PRESS);

        // --- Toggle menu (M) ---
        if (m && !m_was_down_) {
            show_menu_ = !show_menu_;
        }

        if (show_menu_) {
            left_was_down_ = left;
            right_was_down_ = right;
            up_was_down_ = up;
            m_was_down_ = m;
            return;
        }

        // Obrót
        if (left && !left_was_down_) {
            player_.TurnLeft();
        }
        if (right && !right_was_down_) {
            player_.TurnRight();
        }

        // Ruch do przodu (siatka, 1 kafelek)
        if (up && !up_was_down_) {
            int nx = player_.GameX;
            int ny = player_.GameY;

            int yaw = ((player_.yaw % 360) + 360) % 360;

            switch (yaw) {
            case 0:   ny -= 1; break; // północ
            case 180: ny += 1; break; // południe
            case 270: nx -= 1; break; // zachód
            case 90:  nx += 1; break; // wschód
            default: break;
            }

            if (can_move_to(nx, ny)) {
                player_.GameX = nx;
                player_.GameY = ny;
                player_.RenderPosition = glm::vec3(player_.GameX, 0.0f, player_.GameY);
            }
        }

        left_was_down_ = left;
        right_was_down_ = right;
        up_was_down_ = up;
        m_was_down_ = m;
    }

    void App::build_world_mesh() {
        std::vector<float> floor_vertices;
        std::vector<float> wall_vertices;

        const int w = level_.w;
        const int h = level_.h;

        auto add_quad = [](std::vector<float>& buf,
            glm::vec3 a, glm::vec2 ua,
            glm::vec3 b, glm::vec2 ub,
            glm::vec3 c, glm::vec2 uc,
            glm::vec3 d, glm::vec2 ud) {
                auto push = [&buf](glm::vec3 v, glm::vec2 uv) {
                    buf.push_back(v.x); buf.push_back(v.y); buf.push_back(v.z);
                    buf.push_back(uv.x); buf.push_back(uv.y);
                    };
                push(a, ua); push(b, ub); push(c, uc);
                push(a, ua); push(c, uc); push(d, ud);
            };

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const auto cell = level_.cells[y * w + x];

                // PODŁOGA
                if (cell == io::Cell::Floor) {
                    add_quad(floor_vertices,
                        { x,     0.0f, y }, { 0.0f, 0.0f },
                        { x + 1, 0.0f, y }, { 1.0f, 0.0f },
                        { x + 1, 0.0f, y + 1 }, { 1.0f, 1.0f },
                        { x,     0.0f, y + 1 }, { 0.0f, 1.0f });
                }

                // ŚCIANY
                if (cell == io::Cell::Wall) {
                    float h0 = 0.0f, h1 = 1.5f;

                    // Front (Z+1)
                    add_quad(wall_vertices,
                        { x,     h0, y + 1 }, { 0.0f, 0.0f },
                        { x + 1, h0, y + 1 }, { 1.0f, 0.0f },
                        { x + 1, h1, y + 1 }, { 1.0f, 1.0f },
                        { x,     h1, y + 1 }, { 0.0f, 1.0f });

                    // Back (Z)
                    add_quad(wall_vertices,
                        { x + 1, h0, y }, { 0.0f, 0.0f },
                        { x,     h0, y }, { 1.0f, 0.0f },
                        { x,     h1, y }, { 1.0f, 1.0f },
                        { x + 1, h1, y }, { 0.0f, 1.0f });

                    // Left (X)
                    add_quad(wall_vertices,
                        { x, h0, y }, { 0.0f, 0.0f },
                        { x, h0, y + 1 }, { 1.0f, 0.0f },
                        { x, h1, y + 1 }, { 1.0f, 1.0f },
                        { x, h1, y }, { 0.0f, 1.0f });

                    // Right (X+1)
                    add_quad(wall_vertices,
                        { x + 1, h0, y + 1 }, { 0.0f, 0.0f },
                        { x + 1, h0, y }, { 1.0f, 0.0f },
                        { x + 1, h1, y }, { 1.0f, 1.0f },
                        { x + 1, h1, y + 1 }, { 0.0f, 1.0f });
                }
            }
        }

        floor_vertex_count_ = static_cast<int>(floor_vertices.size() / 5);
        wall_vertex_count_ = static_cast<int>(wall_vertices.size() / 5);

        auto setup_vao = [](GLuint& vao, GLuint& vbo, const std::vector<float>& data) {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);

            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);

            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            };

        setup_vao(floor_vao_, floor_vbo_, floor_vertices);
        setup_vao(wall_vao_, wall_vbo_, wall_vertices);
    }

    void App::frame_begin() {
        glfwPollEvents();
        handle_input();

        glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void App::frame_render() {
        float px = static_cast<float>(player_.GameX) + 0.5f;
        float pz = static_cast<float>(player_.GameY) + 0.5f;

        glm::vec3 forward;
        int dx = 0;
        int dz = 0;

        int yaw = ((player_.yaw % 360) + 360) % 360;

        switch (yaw) {
        case 0:   forward = { 0.0f, 0.0f, -1.0f }; dx = 0;  dz = -1; break;
        case 180: forward = { 0.0f, 0.0f,  1.0f }; dx = 0;  dz = 1; break;
        case 270: forward = { -1.0f, 0.0f,  0.0f }; dx = -1; dz = 0; break;
        case 90:  forward = { 1.0f, 0.0f,  0.0f }; dx = 1;  dz = 0; break;
        default:  forward = { 0.0f, 0.0f, -1.0f }; dx = 0;  dz = -1; break;
        }

        glm::vec3 cam_pos;
        glm::vec3 cam_target;

        if (camera_mode_ == CameraMode::FirstPerson) {
            glm::vec3 eye(px, 0.9f, pz);
            cam_pos = eye - forward * 0.05f;
            cam_target = eye + forward * 1.2f;
        }
        else {
            int bx = player_.GameX - dx;
            int by = player_.GameY - dz;
            bool behind_is_blocked = !can_move_to(bx, by);

            cam_target = glm::vec3(px, 0.75f, pz) + forward * 0.35f;

            float cam_distance = behind_is_blocked ? 0.35f : 1.0f;
            float cam_height = 0.9f;

            cam_pos = cam_target - forward * cam_distance
                + glm::vec3(0.0f, cam_height, 0.0f);
        }

        view_ = glm::lookAt(cam_pos, cam_target, glm::vec3(0.0f, 1.0f, 0.0f));

        world_shader_.use();
        world_shader_.setMat4("uProj", &proj_[0][0]);
        world_shader_.setMat4("uView", &view_[0][0]);

        // PODŁOGA
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, floor_texture_);
        world_shader_.setInt("uTex", 0);

        glBindVertexArray(floor_vao_);
        glDrawArrays(GL_TRIANGLES, 0, floor_vertex_count_);

        // ŚCIANY
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, wall_texture_);
        world_shader_.setInt("uTex", 0);

        glBindVertexArray(wall_vao_);
        glDrawArrays(GL_TRIANGLES, 0, wall_vertex_count_);

        glBindVertexArray(0);
    }

    void App::frame_ui() {
        dungeon::ui::HudState hud;
        hud.log = "Starter uruchomiony\nMapa: " + current_map_name_ + "\nWidok: ";
        hud.log += (camera_mode_ == CameraMode::FirstPerson ? "FPP" : "TPP");
        dungeon::ui::draw_hud(hud);

        if (show_menu_) {
            ImGui::SetNextWindowSize(ImVec2(260, 140), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(200, 50), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("Menu", &show_menu_)) {
                ImGui::Text("Ustawienia widoku");
                ImGui::Separator();

                int mode = (camera_mode_ == CameraMode::FirstPerson) ? 0 : 1;
                if (ImGui::RadioButton("First-person (FPP)", mode == 0)) {
                    mode = 0;
                }
                if (ImGui::RadioButton("Third-person (TPP)", mode == 1)) {
                    mode = 1;
                }

                camera_mode_ = (mode == 0)
                    ? CameraMode::FirstPerson
                    : CameraMode::ThirdPerson;

                ImGui::Separator();
                ImGui::Text("M - zamknij menu");
            }
            ImGui::End();
        }
    }

    void App::frame_end() {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }

    void App::run() {
        while (!glfwWindowShouldClose(window_)) {
            frame_begin();
            frame_render();
            frame_ui();
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
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format,
            GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        stbi_image_free(data);
        return tex;
    }

} // namespace dungeon
