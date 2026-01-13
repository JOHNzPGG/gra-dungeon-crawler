#include "dungeon/core/App.hpp"
#include "dungeon/ui/Hud.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Dodano z drugiego kodu: obsługa modeli 3D
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

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
#include <cmath>
#include <filesystem>
#include <map> // Potrzebne dla tiny_obj_loader logic

static void glfw_error_cb(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

// Vertex shader – pozycja + UV (Bez zmian)
static const char* kWorldVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoord;

out vec2 vTexCoord;

uniform mat4 uProj;
uniform mat4 uView;
uniform mat4 uModel;

void main() {
  gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
  vTexCoord = aTexCoord;
}
)";

// Fragment shader – ZMIENIONY NA WERSJĘ Z KODU DRUGIEGO (obsługa uUseTex)
static const char* kWorldFS = R"(#version 330 core
out vec4 FragColor;
in vec2 vTexCoord;

uniform sampler2D uTex; 
uniform int uUseTex;    // 1 = użyj tekstury, 0 = użyj koloru
uniform vec4 uColor;    // Kolor obiektu (jeśli uUseTex == 0)

void main() {
    if (uUseTex == 1) {
        FragColor = texture(uTex, vTexCoord);
    } else {
        FragColor = uColor;
    }
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
        build_cube_mesh();
        spawn_entities_from_level();
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
        if (cube_vbo_) glDeleteBuffers(1, &cube_vbo_);
        if (cube_vao_) glDeleteVertexArrays(1, &cube_vao_);

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

        // pozwala wywołać metody App z callbacków GLFW
        glfwSetWindowUserPointer(window_, this);

        glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* win, int w, int h) {
            auto* app = static_cast<App*>(glfwGetWindowUserPointer(win));
            if (app) {
                app->on_resize(w, h);
            }
            });

    }

    void App::init_gl() {
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
            throw std::runtime_error("GLAD load failed");

        glViewport(0, 0, cfg_.width, cfg_.height);
        glEnable(GL_DEPTH_TEST);

        float aspect = static_cast<float>(cfg_.width) / static_cast<float>(cfg_.height);
        proj_ = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

        world_shader_ = gfx::Shader(kWorldVS, kWorldFS);

        // ZMIANA: Ścieżki do tekstur z drugiego kodu
        wall_texture_ = load_texture("assets/models/wmremove-transformed.PNG");
        floor_texture_ = load_texture("assets/models/kamienna_posadzka.PNG");

        // Start w trybie "full windowed"
        glfwMaximizeWindow(window_);
        int fb_w = 0, fb_h = 0;
        glfwGetFramebufferSize(window_, &fb_w, &fb_h);
        if (fb_w > 0 && fb_h > 0) {
            on_resize(fb_w, fb_h);
        }

    }

    void App::on_resize(int width, int height) {
        if (width <= 0 || height <= 0) return;

        cfg_.width = width;
        cfg_.height = height;

        glViewport(0, 0, width, height);

        float aspect = static_cast<float>(width) / static_cast<float>(height);
        proj_ = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
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

    void App::spawn_entities_from_level() {
        enemies_world_pos_.clear();
        items_world_pos_.clear();
        items_alive_.clear();

        // Enemy: środek kafelka + lekko w górę (lewituje)
        for (const auto& p : level_.enemy_spawns) {
            float x = static_cast<float>(p.x) + 0.5f;
            float z = static_cast<float>(p.y) + 0.5f;
            float y = 0.75f;
            enemies_world_pos_.push_back(glm::vec3(x, y, z));
        }

        // Item: środek kafelka + lekko nad ziemią
        for (const auto& p : level_.item_spawns) {
            float x = static_cast<float>(p.x) + 0.5f;
            float z = static_cast<float>(p.y) + 0.5f;
            float y = 0.7f;
            items_world_pos_.push_back(glm::vec3(x, y, z));

            // --- POPRAWKA TUTAJ ---
            // Musisz zsynchronizować wektory. Skoro dodajesz pozycję, 
            // musisz też dodać informację, że przedmiot "żyje".
            items_alive_.push_back(true);
        }
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

        // po ruchu sprawdź pickup
        for (size_t i = 0; i < items_world_pos_.size(); ++i) {
            if (!items_alive_[i]) continue;

            int ix = (int)std::floor(items_world_pos_[i].x);
            int iy = (int)std::floor(items_world_pos_[i].z);

            if (ix == player_.GameX && iy == player_.GameY) {
                items_alive_[i] = false;
                has_held_item_ = true;
                break;
            }
        }

        left_was_down_ = left;
        right_was_down_ = right;
        up_was_down_ = up;
        m_was_down_ = m;
    }

    // ZMIANA: Cała funkcja build_world_mesh podmieniona na wersję z drugiego kodu (ładowanie modeli)
    void App::build_world_mesh() {
        std::vector<float> floor_vertices;
        std::vector<float> wall_vertices;

        // Kontenery TinyObjLoader (będziemy ich używać dwukrotnie)
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string err;
        std::string base_dir = "assets/models/";

        // ---------------------------------------------------------
        // KROK 1: ŁADOWANIE MODELU ŚCIANY
        // ---------------------------------------------------------
        std::string wall_obj_path = base_dir + "Untitled.obj"; // Plik ściany
        std::vector<float> wall_model_data; // Tu trzymamy wzorzec ściany

        bool retWall = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, wall_obj_path.c_str(), base_dir.c_str());

        if (!err.empty()) fprintf(stderr, "[WALL INFO]: %s\n", err.c_str());

        if (retWall) {
            // A. Tekstura z MTL dla ściany
            if (!materials.empty() && !materials[0].diffuse_texname.empty()) {
                std::string tex_path = base_dir + materials[0].diffuse_texname;
                if (wall_texture_) glDeleteTextures(1, &wall_texture_);
                wall_texture_ = load_texture(tex_path.c_str());
            }

            // B. Spłaszczanie danych ściany
            for (const auto& shape : shapes) {
                for (const auto& index : shape.mesh.indices) {
                    wall_model_data.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                    wall_model_data.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                    wall_model_data.push_back(attrib.vertices[3 * index.vertex_index + 2]);

                    if (index.texcoord_index >= 0) {
                        wall_model_data.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                        wall_model_data.push_back(attrib.texcoords[2 * index.texcoord_index + 1]);
                    }
                    else {
                        wall_model_data.push_back(0.0f); wall_model_data.push_back(0.0f);
                    }
                }
            }
        }

        // ---------------------------------------------------------
        // KROK 2: ŁADOWANIE MODELU PODŁOGI (NOWOŚĆ)
        // ---------------------------------------------------------

        // Czyścimy kontenery przed drugim ładowaniem
        attrib.vertices.clear();
        attrib.texcoords.clear();
        shapes.clear();
        materials.clear();
        err.clear();

        std::string floor_obj_path = base_dir + "kamien1.obj"; // TUTAJ WPISZ NAZWĘ PLIKU PODŁOGI
        std::vector<float> floor_model_data; // Tu trzymamy wzorzec podłogi

        bool retFloor = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, floor_obj_path.c_str(), base_dir.c_str());

        if (!err.empty()) fprintf(stderr, "[FLOOR INFO]: %s\n", err.c_str());

        if (retFloor) {
            // A. Tekstura z MTL dla podłogi (opcjonalnie)
            if (!materials.empty() && !materials[0].diffuse_texname.empty()) {
                std::string tex_path = base_dir + materials[0].diffuse_texname;
                if (floor_texture_) glDeleteTextures(1, &floor_texture_);
                floor_texture_ = load_texture(tex_path.c_str());
            }

            // B. Spłaszczanie danych podłogi
            for (const auto& shape : shapes) {
                for (const auto& index : shape.mesh.indices) {
                    floor_model_data.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                    floor_model_data.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                    floor_model_data.push_back(attrib.vertices[3 * index.vertex_index + 2]);

                    if (index.texcoord_index >= 0) {
                        floor_model_data.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                        floor_model_data.push_back(attrib.texcoords[2 * index.texcoord_index + 1]);
                    }
                    else {
                        floor_model_data.push_back(0.0f); floor_model_data.push_back(0.0f);
                    }
                }
            }
        }

        bool use_wall_model = !wall_model_data.empty();
        bool use_floor_model = !floor_model_data.empty();

        // ---------------------------------------------------------
        // KROK 3: GENEROWANIE SIATKI ŚWIATA
        // ---------------------------------------------------------

        const int w = level_.w;
        const int h = level_.h;

        // Helper do generowania płaskiego quada (fallback, jeśli nie ma modelu)
        auto add_quad = [](std::vector<float>& buf, glm::vec3 a, glm::vec2 ua, glm::vec3 b, glm::vec2 ub, glm::vec3 c, glm::vec2 uc, glm::vec3 d, glm::vec2 ud) {
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

                // === PODŁOGA ===
                if (cell == io::Cell::Floor) {
                    if (use_floor_model) {
                        // Skalowanie i ustawianie modelu podłogi
                        float scale = 0.53f;     // Dostosuj skalę
                        float offset_x = 0.5f;  // Centrowanie w kratce
                        float offset_y = 0.0f;  // Podłoga jest na poziomie 0
                        float offset_z = 0.5f;  // Centrowanie w kratce

                        for (size_t i = 0; i < floor_model_data.size(); i += 5) {
                            float vx = floor_model_data[i + 0];
                            float vy = floor_model_data[i + 1];
                            float vz = floor_model_data[i + 2];
                            float tu = floor_model_data[i + 3];
                            float tv = floor_model_data[i + 4];

                            vx = vx * scale + offset_x + x;
                            vy = vy * scale + offset_y;     // Podłoga na 0
                            vz = vz * scale + offset_z + y;

                            floor_vertices.push_back(vx);
                            floor_vertices.push_back(vy);
                            floor_vertices.push_back(vz);
                            floor_vertices.push_back(tu);
                            floor_vertices.push_back(tv);
                        }
                    }
                    else {
                        // Fallback (stary sposób generowania quada)
                        add_quad(floor_vertices,
                            { x, 0.0f, y }, { 0.0f, 0.0f },
                            { x + 1, 0.0f, y }, { 1.0f, 0.0f },
                            { x + 1, 0.0f, y + 1 }, { 1.0f, 1.0f },
                            { x, 0.0f, y + 1 }, { 0.0f, 1.0f });
                    }
                }

                // === ŚCIANY ===
                if (cell == io::Cell::Wall) {
                    if (use_wall_model) {
                        float scale = 0.50f;
                        float yscale = 1.0f;
                        float offset_x = 0.5f;
                        float offset_y = 0.5f;
                        float offset_z = 0.5f;

                        for (size_t i = 0; i < wall_model_data.size(); i += 5) {
                            float vx = wall_model_data[i + 0];
                            float vy = wall_model_data[i + 1];
                            float vz = wall_model_data[i + 2];
                            float tu = wall_model_data[i + 3];
                            float tv = wall_model_data[i + 4];

                            vx = vx * scale + offset_x + x;
                            vy = vy * yscale + offset_y;
                            vz = vz * scale + offset_z + y;

                            wall_vertices.push_back(vx);
                            wall_vertices.push_back(vy);
                            wall_vertices.push_back(vz);
                            wall_vertices.push_back(tu);
                            wall_vertices.push_back(tv);
                        }
                    }
                    else {
                        // Fallback (sześcian)
                        float h0 = 0.0f, h1 = 1.0f;
                        add_quad(wall_vertices, { x, h0, y + 1 }, { 0.0f, 0.0f }, { x + 1, h0, y + 1 }, { 1.0f, 0.0f }, { x + 1, h1, y + 1 }, { 1.0f, 1.0f }, { x, h1, y + 1 }, { 0.0f, 1.0f });
                        add_quad(wall_vertices, { x + 1, h0, y }, { 0.0f, 0.0f }, { x, h0, y }, { 1.0f, 0.0f }, { x, h1, y }, { 1.0f, 1.0f }, { x + 1, h1, y }, { 0.0f, 1.0f });
                        add_quad(wall_vertices, { x, h0, y }, { 0.0f, 0.0f }, { x, h0, y + 1 }, { 1.0f, 0.0f }, { x, h1, y + 1 }, { 1.0f, 1.0f }, { x, h1, y }, { 0.0f, 1.0f });
                        add_quad(wall_vertices, { x + 1, h0, y + 1 }, { 0.0f, 0.0f }, { x + 1, h0, y }, { 1.0f, 0.0f }, { x + 1, h1, y }, { 1.0f, 1.0f }, { x + 1, h1, y + 1 }, { 0.0f, 1.0f });
                    }
                }
            }
        }

        // ---------------------------------------------------------
        // KROK 4: PRZESŁANIE DANYCH DO GPU (VAO / VBO)
        // ---------------------------------------------------------

        floor_vertex_count_ = static_cast<int>(floor_vertices.size() / 5);
        wall_vertex_count_ = static_cast<int>(wall_vertices.size() / 5);

        auto setup_vao = [](GLuint& vao, GLuint& vbo, const std::vector<float>& data) {
            if (data.empty()) return;
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);

            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);

            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            };

        // Usuwamy stare bufory, jeśli istniały (ważne przy przeładowaniu poziomu)
        if (floor_vao_) glDeleteVertexArrays(1, &floor_vao_);
        if (floor_vbo_) glDeleteBuffers(1, &floor_vbo_);
        if (wall_vao_) glDeleteVertexArrays(1, &wall_vao_);
        if (wall_vbo_) glDeleteBuffers(1, &wall_vbo_);
        floor_vao_ = 0; floor_vbo_ = 0; wall_vao_ = 0; wall_vbo_ = 0;

        setup_vao(floor_vao_, floor_vbo_, floor_vertices);
        setup_vao(wall_vao_, wall_vbo_, wall_vertices);
    }

    void App::build_cube_mesh() {
        // Kostka jednostkowa w centrum (od -0.5 do +0.5)
        // Vertex: pos(3) + uv(2) => 5 floatów, żeby pasowało do shadera
        const float v[] = {
            // front
            -0.5f,-0.5f, 0.5f, 0,0,   0.5f,-0.5f, 0.5f, 1,0,   0.5f, 0.5f, 0.5f, 1,1,
            -0.5f,-0.5f, 0.5f, 0,0,   0.5f, 0.5f, 0.5f, 1,1,  -0.5f, 0.5f, 0.5f, 0,1,
            // back
             0.5f,-0.5f,-0.5f, 0,0,  -0.5f,-0.5f,-0.5f, 1,0,  -0.5f, 0.5f,-0.5f, 1,1,
             0.5f,-0.5f,-0.5f, 0,0,  -0.5f, 0.5f,-0.5f, 1,1,   0.5f, 0.5f,-0.5f, 0,1,
             // left
             -0.5f,-0.5f,-0.5f, 0,0,  -0.5f,-0.5f, 0.5f, 1,0,  -0.5f, 0.5f, 0.5f, 1,1,
             -0.5f,-0.5f,-0.5f, 0,0,  -0.5f, 0.5f, 0.5f, 1,1,  -0.5f, 0.5f,-0.5f, 0,1,
             // right
              0.5f,-0.5f, 0.5f, 0,0,   0.5f,-0.5f,-0.5f, 1,0,   0.5f, 0.5f,-0.5f, 1,1,
              0.5f,-0.5f, 0.5f, 0,0,   0.5f, 0.5f,-0.5f, 1,1,   0.5f, 0.5f, 0.5f, 0,1,
              // top
              -0.5f, 0.5f, 0.5f, 0,0,   0.5f, 0.5f, 0.5f, 1,0,   0.5f, 0.5f,-0.5f, 1,1,
              -0.5f, 0.5f, 0.5f, 0,0,   0.5f, 0.5f,-0.5f, 1,1,  -0.5f, 0.5f,-0.5f, 0,1,
              // bottom
              -0.5f,-0.5f,-0.5f, 0,0,   0.5f,-0.5f,-0.5f, 1,0,   0.5f,-0.5f, 0.5f, 1,1,
              -0.5f,-0.5f,-0.5f, 0,0,   0.5f,-0.5f, 0.5f, 1,1,  -0.5f,-0.5f, 0.5f, 0,1,
        };

        cube_vertex_count_ = (int)(sizeof(v) / sizeof(float) / 5);

        glGenVertexArrays(1, &cube_vao_);
        glGenBuffers(1, &cube_vbo_);
        glBindVertexArray(cube_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, cube_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
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

    // ZMIANA: Zachowana logika z kodu głównego (kamera/HUD), ale z dodaną obsługą uUseTex dla nowego shadera
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

            cam_target = glm::vec3(px, 0.7f, pz) + forward * 0.35f;

            float cam_distance = behind_is_blocked ? 0.35f : 1.0f;
            float cam_height = 0.9f;

            cam_pos = cam_target - forward * cam_distance
                + glm::vec3(0.0f, cam_height, 0.0f);
        }

        view_ = glm::lookAt(cam_pos, cam_target, glm::vec3(0.0f, 1.0f, 0.0f));

        world_shader_.use();
        world_shader_.setMat4("uProj", &proj_[0][0]);
        world_shader_.setMat4("uView", &view_[0][0]);
        glm::mat4 I(1.0f);
        world_shader_.setMat4("uModel", &I[0][0]);


        // WAŻNE: Włączamy teksturowanie dla nowego shadera
        world_shader_.setInt("uUseTex", 1);

        // PODŁOGA
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, floor_texture_);
        world_shader_.setInt("uTex", 0);

        if (floor_vertex_count_ > 0) {
            glBindVertexArray(floor_vao_);
            glDrawArrays(GL_TRIANGLES, 0, floor_vertex_count_);
        }

        // ŚCIANY
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, wall_texture_);
        world_shader_.setInt("uTex", 0);

        if (wall_vertex_count_ > 0) {
            glBindVertexArray(wall_vao_);
            glDrawArrays(GL_TRIANGLES, 0, wall_vertex_count_);
        }

        // --- ENEMIES (czarne kostki 0.5m) ---
        world_shader_.setInt("uUseTex", 0);
        world_shader_.setVec4("uColor", 0.0f, 0.0f, 0.0f, 1.0f);

        glBindVertexArray(cube_vao_);

        for (const auto& pos : enemies_world_pos_) {
            glm::mat4 M(1.0f);
            M = glm::translate(M, pos);
            M = glm::scale(M, glm::vec3(0.5f)); // 0.5m szerokości
            world_shader_.setMat4("uModel", &M[0][0]);

            glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
        }
        glBindVertexArray(0);

        // --- ITEMS (placeholder: jasnoszara kostka 0.25m) ---
        world_shader_.setInt("uUseTex", 0);
        world_shader_.setVec4("uColor", 0.8f, 0.8f, 0.8f, 1.0f);

        glBindVertexArray(cube_vao_);
        for (const auto& pos : items_world_pos_) {
            glm::mat4 M(1.0f);
            M = glm::translate(M, pos);
            M = glm::scale(M, glm::vec3(0.25f)); // mniejszy niż enemy
            world_shader_.setMat4("uModel", &M[0][0]);
            glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
        }
        glBindVertexArray(0);

        // --- HELD ITEM (placeholder przy kamerze) ---
        if (has_held_item_) {
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            glm::vec3 rightv = glm::normalize(glm::cross(forward, up));

            // pozycja zależna od kamery (działa w FPP i TPP)
            glm::vec3 item_pos = cam_pos
                + forward * 0.55f    // przed kamerą
                + rightv * 0.25f    // w prawo
                + up * -0.20f;  // w dół

            float t = (float)glfwGetTime();

            world_shader_.setInt("uUseTex", 0);
            world_shader_.setVec4("uColor", 0.15f, 0.15f, 0.15f, 1.0f);

            glm::mat4 M(1.0f);
            M = glm::translate(M, item_pos);
            M = glm::rotate(M, t * 2.0f, glm::vec3(0, 1, 0));           // obrót
            M = glm::scale(M, glm::vec3(0.18f, 0.18f, 0.40f));          // “kształt broni”

            world_shader_.setMat4("uModel", &M[0][0]);

            glBindVertexArray(cube_vao_);
            glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
            glBindVertexArray(0);

            // ważne: przywróć model na identity, żebyś nie złapał artefaktów w przyszłości
            world_shader_.setMat4("uModel", &I[0][0]);
        }

    }

    void App::frame_ui() {
        dungeon::ui::HudState hud;
        hud.log = "Starter uruchomiony\nMapa: " + current_map_name_ + "\nWidok: ";
        hud.log += (camera_mode_ == CameraMode::FirstPerson ? "FPP" : "TPP");
        hud.log += "\nItem: ";
        hud.log += (has_held_item_ ? "TAK" : "NIE");
        
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

    void App::render_main_menu() {
        // Rozmiar okna ImGui (stały, ale możesz potem zmienić)
        ImVec2 window_size(520, 320);

        // Wyśrodkowanie okna względem aktualnej rozdzielczości
        ImVec2 display_size = ImGui::GetIO().DisplaySize;
        ImVec2 window_pos(
            (display_size.x - window_size.x) * 0.5f,
            (display_size.y - window_size.y) * 0.5f
        );

        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);

        // Trochę ładniejszy styl okna
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 24.0f));

        // Bez tytułu, bez resize/drag
        ImGui::Begin("##MainMenu", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse);

        // Tytuł gry – wycentrowany
        ImGui::PushFont(nullptr); // jeśli kiedyś dodasz większą czcionkę, użyjesz jej tutaj
        const char* title = "Dungeon Crawler Prototype";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        float title_x = (window_size.x - title_size.x) * 0.5f;
        ImGui::SetCursorPosX(title_x);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 20.0f)); // odstęp pod tytułem

        // Przyciski – szerokie, wycentrowane
        const ImVec2 button_size(260.0f, 48.0f);
        float button_x = (window_size.x - button_size.x) * 0.5f;

        // Start Gry
        ImGui::SetCursorPosX(button_x);
        if (ImGui::Button("Start Gry", button_size)) {
            state_ = GameState::Playing;
        }

        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        // Opcje
        ImGui::SetCursorPosX(button_x);
        if (ImGui::Button("Opcje", button_size)) {
            state_ = GameState::Options;
        }

        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        // Wyjście
        ImGui::SetCursorPosX(button_x);
        if (ImGui::Button("Wyjscie", button_size)) {
            glfwSetWindowShouldClose(window_, 1);
        }

        ImGui::End();
        ImGui::PopStyleVar(3); // WindowRounding, FrameRounding, WindowPadding
    }

    void App::render_options_menu() {

        ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(
            ImVec2(cfg_.width / 2 - 210, cfg_.height / 2 - 150),
            ImGuiCond_FirstUseEver
        );

        ImGui::Begin("Opcje", nullptr,
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse);

        ImGui::Text("Ustawienia obrazu:");
        ImGui::Separator();

        // 1280 x 720 - normalne okno z ramką
        if (ImGui::Button("1280 x 720 (okno)", ImVec2(250, 30))) {
            glfwSetWindowMonitor(window_, nullptr, 100, 100, 1280, 720, 0);
            glfwSetWindowAttrib(window_, GLFW_DECORATED, GLFW_TRUE);
            on_resize(1280, 720);
        }

        // 1600 x 900 - większe okno z ramką
        if (ImGui::Button("1600 x 900 (okno)", ImVec2(250, 30))) {
            glfwSetWindowMonitor(window_, nullptr, 100, 100, 1600, 900, 0);
            glfwSetWindowAttrib(window_, GLFW_DECORATED, GLFW_TRUE);
            on_resize(1600, 900);
        }

        // Borderless fullscreen (okno bez ramek, na cały ekran)
        if (ImGui::Button("Borderless fullscreen", ImVec2(250, 30))) {
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(mon);

            // okno jako zwykłe (monitor = nullptr), ale rozciągnięte na cały ekran i bez ramek
            glfwSetWindowMonitor(window_, nullptr, 0, 0, mode->width, mode->height, 0);
            glfwSetWindowAttrib(window_, GLFW_DECORATED, GLFW_FALSE);

            on_resize(mode->width, mode->height);
        }

        // Pelny ekran ekskluzywny (GLFW fullscreen na monitorze)
        if (ImGui::Button("Pelny ekran (exclusive)", ImVec2(250, 30))) {
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(mon);

            glfwSetWindowMonitor(window_, mon, 0, 0, mode->width, mode->height, mode->refreshRate);

            on_resize(mode->width, mode->height);
        }

        ImGui::Separator();
        ImGui::Text("Dźwięk:");
        static float volume = 0.5f;
        ImGui::SliderFloat("Glosnosc", &volume, 0.0f, 1.0f);

        ImGui::Separator();
        if (ImGui::Button("Wroc", ImVec2(200, 40))) {
            state_ = GameState::MainMenu;
        }

        ImGui::End();
    }

    void App::run() {
        while (!glfwWindowShouldClose(window_)) {

            frame_begin();

            switch (state_) {

            case GameState::MainMenu:
                render_main_menu();
                break;

            case GameState::Options:
                render_options_menu();
                break;

            case GameState::Playing:
                frame_render();   // render świata 3D
                frame_ui();       // HUD gry
                break;
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
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format,
            GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        stbi_image_free(data);
        return tex;
    }

} // namespace dungeon