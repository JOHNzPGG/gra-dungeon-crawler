#include "dungeon/core/App.hpp"
#include "dungeon/ui/Hud.hpp"

// Implementacje bibliotek "single-header"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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
#include <filesystem>
#include <cmath> // dla sin, cos

static void glfw_error_cb(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

// --- SHADERY (wersja z obsługą tekstur i kolorów) ---

// Vertex Shader
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

// Fragment Shader
static const char* kWorldFS = R"(#version 330 core
out vec4 FragColor;
in vec2 vTexCoord;

uniform sampler2D uTex; 
uniform int uUseTex;    // 1 = użyj tekstury, 0 = użyj koloru (uColor)
uniform vec4 uColor;    // Kolor obiektu (gdy uUseTex == 0)

void main() {
    if (uUseTex == 1) {
        FragColor = texture(uTex, vTexCoord);
    } else {
        FragColor = uColor;
    }
}
)";


namespace dungeon {

    // --- Funkcja pomocnicza do ładowania OBJ ---
    static std::vector<float> load_obj_mesh(const std::string& path) {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string err;

        bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, path.c_str());

        if (!err.empty()) {
            fprintf(stderr, "OBJ info: %s\n", err.c_str());
        }

        if (!ret) {
            fprintf(stderr, "Failed to load/parse .obj: %s\n", path.c_str());
            return {};
        }

        std::vector<float> data;
        // Spłaszczanie danych (x,y,z, u,v)
        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                // Pozycja
                data.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                data.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                data.push_back(attrib.vertices[3 * index.vertex_index + 2]);

                // Tekstura UV
                if (index.texcoord_index >= 0) {
                    data.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                    data.push_back(attrib.texcoords[2 * index.texcoord_index + 1]);
                }
                else {
                    data.push_back(0.0f);
                    data.push_back(0.0f);
                }
            }
        }
        return data;
    }


    App::App(const AppConfig& cfg)
        : cfg_(cfg),
        player_(1, 1, 0)
    {
        // Ustawienie startowe gracza (jeśli klasa Player tego nie robi)
        player_.GameX = level_.player_x;
        player_.GameY = level_.player_y;

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

        // Pointer dla callbacków
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

        // Domyślny viewport
        glViewport(0, 0, cfg_.width, cfg_.height);
        glEnable(GL_DEPTH_TEST);

        float aspect = static_cast<float>(cfg_.width) / static_cast<float>(cfg_.height);
        proj_ = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

        // Kompilacja shaderów
        world_shader_ = gfx::Shader(kWorldVS, kWorldFS);

        // Ładowanie tekstur
        wall_texture_ = load_texture("assets/textures/sciana.png");
        floor_texture_ = load_texture("assets/textures/podloga.png");

        // --- PRZYWRÓCONE: Start w trybie zmaksymalizowanym ---
        glfwMaximizeWindow(window_);

        // Pobranie faktycznego rozmiaru po maksymalizacji, żeby obraz nie był rozciągnięty
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
            // Fallback, jeśli mapa nie istnieje
            fprintf(stderr, "Mapa nie znaleziona: %s\n", path.c_str());
            // Tutaj można wygenerować pustą mapę w locie, żeby nie crashować
            level_.w = 10; level_.h = 10;
            level_.player_x = 1; level_.player_y = 1;
            level_.cells.assign(100, io::Cell::Floor);
        }
        else {
            level_ = io::load_map_ascii(path);
        }

        // Ustawienie pozycji gracza
        //player_.GameX = level_.player_x;
        //player_.GameY = level_.player_y;
        // Reset kierunku, jeśli potrzebny
        // player_.dir = ... 
    }

    bool App::can_move_to(int x, int y) const {
        if (x < 0 || y < 0) return false;
        if (x >= level_.w || y >= level_.h) return false;
        auto cell = level_.cells[y * level_.w + x];
        return cell != io::Cell::Wall;
    }

    void App::handle_input() {
        // Obsługa menu
        if (glfwGetKey(window_, GLFW_KEY_M) == GLFW_PRESS && !m_was_down_) {
            show_menu_ = !show_menu_;
        }
        m_was_down_ = (glfwGetKey(window_, GLFW_KEY_M) == GLFW_PRESS);

        if (show_menu_) return;

        bool left = (glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS);
        bool right = (glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS);
        bool up = (glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS);

        // --- POPRAWKA: Używamy metod klasy Player z Twojego projektu ---
        // (Zakładam, że Twoja klasa Player ma metody TurnLeft/Right i pola GameX/GameY)

        // Obrót
        if (left && !left_was_down_) {
            player_.TurnLeft(); // Używamy metody z Player.h
        }
        if (right && !right_was_down_) {
            player_.TurnRight(); // Używamy metody z Player.h
        }

        // Ruch
        if (up && !up_was_down_) {
            // Pobieramy aktualną pozycję
            int nx = player_.GameX;
            int ny = player_.GameY;

            // Pobieramy kąt (zakładam, że Player ma pole yaw lub metodę GetYaw/kierunek)
            // Jeśli Player ma pole 'yaw':
            int yaw = ((player_.yaw % 360) + 360) % 360;

            // Logika ruchu zależna od kąta (prosta wersja 90 stopni)
            if (yaw == 0)   ny -= 1;      // Północ
            if (yaw == 180) ny += 1;      // Południe
            if (yaw == 270) nx -= 1;      // Zachód
            if (yaw == 90)  nx += 1;      // Wschód

            if (can_move_to(nx, ny)) {
                player_.GameX = nx;
                player_.GameY = ny;
                // Aktualizacja pozycji do renderowania (płynne przejścia jeśli masz)
                player_.RenderPosition = glm::vec3(player_.GameX, 0.0f, player_.GameY);
            }
        }

        left_was_down_ = left;
        right_was_down_ = right;
        up_was_down_ = up;
    }

    // W pliku src/core/App.cpp

    void App::build_world_mesh() {
        std::vector<float> floor_vertices;
        std::vector<float> wall_vertices;

        // --- USTAWIENIA MODELU ---
        // Jeśli model wciąż jest "czerwony/rozmazany", zmień poniższą wartość na true.
        // To sprawi, że gra wygeneruje sześciany kodem (jak w Minecrafcie), zamiast ładować plik .obj.
        const bool USE_GENERATED_CUBE = false;

        static std::vector<float> wall_model;

        // Próbujemy załadować "lepszy" model, jeśli nie używamy generatora
        if (!USE_GENERATED_CUBE && wall_model.empty()) {
            // Zmienilem nazwe na wall_improved.obj, bo taki miales w App1.cpp
            wall_model = load_obj_mesh("assets/models/wall_improved.obj");

            // Jeśli plik nie istnieje, fallback do zwykłego wall.obj
            if (wall_model.empty()) {
                wall_model = load_obj_mesh("assets/models/wall.obj");
            }
        }

        // Czy mamy poprawny model do wyświetlenia?
        bool use_model = !USE_GENERATED_CUBE && !wall_model.empty();

        const int w = level_.w;
        const int h = level_.h;

        // Helper lambda
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
                    if (use_model) {
                        // --- RYSOWANIE Z MODELU .OBJ ---
                        float scale = 1.0f;
                        float offset_x = 0.5f;
                        float offset_y = 0.0f;
                        float offset_z = 0.5f;

                        // Kąt obrotu (0.0f domyślnie)
                        float rotation_deg = 0.0f;
                        float rad = glm::radians(rotation_deg);
                        float cs = cos(rad);
                        float sn = sin(rad);

                        for (size_t i = 0; i < wall_model.size(); i += 5) {
                            float raw_x = wall_model[i + 0];
                            float raw_y = wall_model[i + 1];
                            float raw_z = wall_model[i + 2];
                            float tu = wall_model[i + 3];
                            float tv = wall_model[i + 4];

                            // Skala i Rotacja
                            raw_x *= scale;
                            raw_y *= scale;
                            raw_z *= scale;

                            float rx = raw_x * cs - raw_z * sn;
                            float rz = raw_x * sn + raw_z * cs;

                            // Pozycja w świecie
                            wall_vertices.push_back(rx + offset_x + x);
                            wall_vertices.push_back(raw_y + offset_y);
                            wall_vertices.push_back(rz + offset_z + y);

                            // UV
                            wall_vertices.push_back(tu);
                            wall_vertices.push_back(tv);
                        }
                    }
                    else {
                        // --- FALLBACK: GENEROWANIE SZEŚCIANU KODEM ---
                        // To zadziała na pewno, jeśli tekstury są załadowane poprawnie
                        float h0 = 0.0f, h1 = 1.0f;

                        // Front (Z+1)
                        add_quad(wall_vertices,
                            { x, h0, y + 1 }, { 0.0f, 0.0f },
                            { x + 1, h0, y + 1 }, { 1.0f, 0.0f },
                            { x + 1, h1, y + 1 }, { 1.0f, 1.0f },
                            { x, h1, y + 1 }, { 0.0f, 1.0f });

                        // Back (Z)
                        add_quad(wall_vertices,
                            { x + 1, h0, y }, { 0.0f, 0.0f },
                            { x, h0, y }, { 1.0f, 0.0f },
                            { x, h1, y }, { 1.0f, 1.0f },
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

                        // Top (opcjonalnie, sufit ściany)
                        add_quad(wall_vertices,
                            { x, h1, y + 1 }, { 0.0f, 0.0f },
                            { x + 1, h1, y + 1 }, { 1.0f, 0.0f },
                            { x + 1, h1, y }, { 1.0f, 1.0f },
                            { x, h1, y }, { 0.0f, 1.0f });
                    }
                }
            }
        }

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

    // W pliku src/core/App.cpp

    void App::frame_render() {
        // --- 1. STARA LOGIKA KAMERY (Przywrócona) ---
        float px = static_cast<float>(player_.GameX) + 0.5f;
        float pz = static_cast<float>(player_.GameY) + 0.5f;

        glm::vec3 forward;
        int dx = 0;
        int dz = 0;

        // Pobieramy yaw (kąt) gracza
        int yaw = ((player_.yaw % 360) + 360) % 360;

        switch (yaw) {
        case 0:   forward = { 0.0f, 0.0f, -1.0f }; dx = 0;  dz = -1; break; // Północ
        case 180: forward = { 0.0f, 0.0f,  1.0f }; dx = 0;  dz = 1; break;  // Południe
        case 270: forward = { -1.0f, 0.0f,  0.0f }; dx = -1; dz = 0; break; // Zachód
        case 90:  forward = { 1.0f, 0.0f,  0.0f }; dx = 1;  dz = 0; break;  // Wschód
        default:  forward = { 0.0f, 0.0f, -1.0f }; dx = 0;  dz = -1; break;
        }

        glm::vec3 cam_pos;
        glm::vec3 cam_target;

        if (camera_mode_ == CameraMode::FirstPerson) {
            // Widok z oczu
            glm::vec3 eye(px, 0.6f, pz); // Wysokość oczu 0.6f
            cam_pos = eye;
            cam_target = eye + forward;
        }
        else {
            // Widok TPP (Third Person)
            // Sprawdzamy czy za plecami jest ściana
            int bx = player_.GameX - dx;
            int by = player_.GameY - dz;
            bool behind_is_blocked = !can_move_to(bx, by);

            // Punkt patrzenia (trochę przed graczem)
            cam_target = glm::vec3(px, 0.6f, pz) + forward * 0.35f;

            // Jeśli za plecami ściana -> kamera bliżej (0.35f), w przeciwnym razie 1.0f
            float cam_distance = behind_is_blocked ? 0.35f : 1.0f;
            float cam_height = 0.8f; // Trochę wyżej w TPP

            cam_pos = cam_target - forward * cam_distance + glm::vec3(0.0f, cam_height, 0.0f);
        }

        view_ = glm::lookAt(cam_pos, cam_target, glm::vec3(0.0f, 1.0f, 0.0f));

        // --- 2. NOWA LOGIKA RYSOWANIA (Tekstury + Modele) ---

        world_shader_.use();
        world_shader_.setMat4("uProj", &proj_[0][0]);
        world_shader_.setMat4("uView", &view_[0][0]);

        // RYSOWANIE PODŁOGI
        world_shader_.setInt("uUseTex", 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, floor_texture_);
        world_shader_.setInt("uTex", 0);

        if (floor_vertex_count_ > 0) {
            glBindVertexArray(floor_vao_);
            glDrawArrays(GL_TRIANGLES, 0, floor_vertex_count_);
        }

        // RYSOWANIE ŚCIAN
        // Tu używamy tekstury ściany
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, wall_texture_);

        if (wall_vertex_count_ > 0) {
            glBindVertexArray(wall_vao_);
            glDrawArrays(GL_TRIANGLES, 0, wall_vertex_count_);
        }

        glBindVertexArray(0);
    }

    // W pliku src/core/App.cpp

    void App::frame_ui() {
        // Rysowanie HUD (Twoja klasa)
        dungeon::ui::HudState hud;
        hud.log = "Starter uruchomiony\nMapa: " + current_map_name_ + "\nWidok: ";
        hud.log += (camera_mode_ == CameraMode::FirstPerson ? "FPP" : "TPP");
        dungeon::ui::draw_hud(hud);

        // Menu pod klawiszem M
        if (show_menu_) {
            ImGui::SetNextWindowSize(ImVec2(260, 140), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(200, 50), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("Menu (M)", &show_menu_)) {
                ImGui::Text("Ustawienia widoku");
                ImGui::Separator();

                int mode = (camera_mode_ == CameraMode::FirstPerson) ? 0 : 1;

                if (ImGui::RadioButton("First-person (FPP)", mode == 0)) {
                    camera_mode_ = CameraMode::FirstPerson;
                }
                if (ImGui::RadioButton("Third-person (TPP)", mode == 1)) {
                    camera_mode_ = CameraMode::ThirdPerson;
                }

                ImGui::Separator();
                if (ImGui::Button("Zamknij menu")) {
                    show_menu_ = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Wyjscie z gry")) {
                    glfwSetWindowShouldClose(window_, 1);
                }
            }
            ImGui::End();
        }
    }

    void App::frame_end() {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }

    // W pliku src/core/App.cpp

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

    // W pliku src/core/App.cpp

    void App::render_options_menu() {
        ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(
            ImVec2(cfg_.width / 2 - 510, cfg_.height / 2 - 350),
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

            glfwSetWindowMonitor(window_, nullptr, 0, 0, mode->width, mode->height, 0);
            glfwSetWindowAttrib(window_, GLFW_DECORATED, GLFW_FALSE);

            on_resize(mode->width, mode->height);
        }

        // Pełny ekran ekskluzywny
        if (ImGui::Button("Pelny ekran (exclusive)", ImVec2(250, 30))) {
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(mon);

            glfwSetWindowMonitor(window_, mon, 0, 0, mode->width, mode->height, mode->refreshRate);

            on_resize(mode->width, mode->height);
        }

        ImGui::Separator();
        ImGui::Text("Dzwiek:");
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
            case GameState::MainMenu: render_main_menu(); break;
            case GameState::Options:  render_options_menu(); break;
            case GameState::Playing:  frame_render(); frame_ui(); break;
            }
            frame_end();
        }
    }

    GLuint App::load_texture(const char* path) {
        int w, h, nrChannels;
        stbi_set_flip_vertically_on_load(true);
        unsigned char* data = stbi_load(path, &w, &h, &nrChannels, 0);

        if (!data) {
            fprintf(stderr, "Failed to load texture: %s\n", path);
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

} // namespace dungeon