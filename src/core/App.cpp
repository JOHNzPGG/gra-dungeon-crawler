#include "dungeon/core/App.hpp"
#include "dungeon/ui/Hud.hpp"

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

struct MeshData {
    std::vector<float> vertices;       // x,y,z, u,v, material_id
    std::map<int, GLuint> textures;    // Mapa: ID materiału -> ID tekstury OpenGL
};

static void glfw_error_cb(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

// 2. NOWY SHADER VERTEX (przyjmuje aTexCoord)
static const char* kWorldVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoord; // Nowe: współrzędne UV

out vec2 vTexCoord; // Przekazujemy do Fragment Shadera
uniform mat4 uProj;
uniform mat4 uView;

void main() {
  gl_Position = uProj * uView * vec4(aPos, 1.0);
  vTexCoord = aTexCoord;
}
)";

// 3. NOWY SHADER FRAGMENT (używa texture() zamiast koloru)
//static const char* kWorldFS = R"(#version 330 core
//out vec4 FragColor;
//in vec2 vTexCoord;
//
//uniform sampler2D uTex; // Sampler tekstury
//
//void main() {
//  FragColor = texture(uTex, vTexCoord);
//}
//)";
// 3. ZAKTUALIZOWANY SHADER FRAGMENT
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

    App::App(const AppConfig& cfg) : cfg_(cfg) {
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
        if (wall_vbo_) glDeleteBuffers(1, &wall_vbo_);
        if (wall_vao_) glDeleteVertexArrays(1, &wall_vao_);

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

        // macierz projekcji (perspektywa)
        float aspect = static_cast<float>(cfg_.width) / static_cast<float>(cfg_.height);
        proj_ = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

        // shader świata
        world_shader_ = gfx::Shader(kWorldVS, kWorldFS);

        // Ładowanie tekstur
        wall_texture_ = load_texture("assets/models/wmremove-transformed.PNG");
        floor_texture_ = load_texture("assets/models/kamienna_posadzka.PNG");
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
        player_.x = level_.player_x;
        player_.y = level_.player_y;
        player_.dir = Dir::North; // startowo patrzymy na "północ"

        if (!fs::exists(path)) {
            throw std::runtime_error(
                "Cannot open map: " + path +
                "\nCurrent directory: " + fs::current_path().string());
        }

        level_ = io::load_map_ascii(path);
    }

    bool App::can_move_to(int x, int y) const {
        if (x < 0 || y < 0) return false;
        if (x >= level_.w || y >= level_.h) return false;

        auto cell = level_.cells[y * level_.w + x];
        return cell != io::Cell::Wall;
    }

    void App::handle_input() {
        // aktualny stan klawiszy
        bool left = (glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS);
        bool right = (glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS);
        bool up = (glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS);

        // --- Obrót w lewo: tylko gdy klawisz został właśnie wciśnięty ---
        if (left && !left_was_down_) {
            switch (player_.dir) {
            case Dir::North: player_.dir = Dir::West;  break;
            case Dir::West:  player_.dir = Dir::South; break;
            case Dir::South: player_.dir = Dir::East;  break;
            case Dir::East:  player_.dir = Dir::North; break;
            }
        }

        // --- Obrót w prawo ---
        if (right && !right_was_down_) {
            switch (player_.dir) {
            case Dir::North: player_.dir = Dir::East;  break;
            case Dir::East:  player_.dir = Dir::South; break;
            case Dir::South: player_.dir = Dir::West;  break;
            case Dir::West:  player_.dir = Dir::North; break;
            }
        }

        // --- Ruch do przodu ---
        if (up && !up_was_down_) {
            int nx = player_.x;
            int ny = player_.y;

            switch (player_.dir) {
            case Dir::North: ny -= 1; break;
            case Dir::South: ny += 1; break;
            case Dir::West:  nx -= 1; break;
            case Dir::East:  nx += 1; break;
            }

            if (can_move_to(nx, ny)) {
                player_.x = nx;
                player_.y = ny;
            }
        }

        // zapamiętanie stan na następną klatkę
        left_was_down_ = left;
        right_was_down_ = right;
        up_was_down_ = up;
    }

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

        floor_vertex_count_ = floor_vertices.size() / 5;
        wall_vertex_count_ = wall_vertices.size() / 5;

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
    void App::frame_begin() {
        glfwPollEvents();
        handle_input();
        glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void App::frame_ui() {
        dungeon::ui::HudState hud;
        hud.log = "Starter uruchomiony\nMapa: " + current_map_name_;
        dungeon::ui::draw_hud(hud);
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
        // OpenGL oczekuje osi Y od dołu, a obrazki mają od góry -> odwracamy
        stbi_set_flip_vertically_on_load(true);
        unsigned char* data = stbi_load(path, &w, &h, &nrChannels, 0);

        if (!data) {
            fprintf(stderr, "Failed to load texture: %s\n", path);
            return 0;
        }

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);

        // Parametry powtarzania i filtrowania
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Wykrywanie formatu (RGB czy RGBA z przezroczystością)
        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        stbi_image_free(data);
        return texture;
    }

} // namespace dungeon
    