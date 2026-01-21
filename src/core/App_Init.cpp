/**
 * @file App_Init.cpp
 * @brief Modu³ inicjalizacyjny aplikacji.
 * Zawiera kod odpowiedzialny za konfiguracjê GLFW, OpenGL, Audio oraz
 * ³adowanie i przetwarzanie modeli 3D (TinyObjLoader).
 */

#include "dungeon/core/App.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "tiny_obj_loader.h" 

 // --- KONFIGURACJA SHADERÓW GLSL ---

 /**
  * @brief Vertex Shader.
  * Odpowiada za transformacjê wierzcho³ków z przestrzeni modelu do przestrzeni ekranu (MVP Matrix).
  */
static const char* kWorldVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoord;
out vec2 vTexCoord;
out vec3 vFragPos;
uniform mat4 uProj;
uniform mat4 uView;
uniform mat4 uModel;
void main() {
  vec4 worldPos = uModel * vec4(aPos, 1.0);
  vFragPos = vec3(worldPos);
  gl_Position = uProj * uView * worldPos;
  vTexCoord = aTexCoord;
})";

/**
 * @brief Fragment Shader.
 * Odpowiada za kolorowanie pikseli, nak³adanie tekstur i obliczanie oœwietlenia (Latarka + Pochodnie).
 */
static const char* kWorldFS = R"(#version 330 core
out vec4 FragColor;
in vec2 vTexCoord;
in vec3 vFragPos;
uniform sampler2D uTex;
uniform int uUseTex;
uniform vec4 uColor;
uniform vec3 uCamPos;
uniform float uTime;
uniform vec3 uPuzzleLights[16];
uniform int uActivePuzzleLights;
void main() {
    vec4 baseColor;
    if (uUseTex == 1) baseColor = texture(uTex, vTexCoord) * uColor;
    else baseColor = uColor;
    
    if(baseColor.a < 0.1) discard; // Alpha test

    // --- OŒWIETLENIE DYNAMICZNE ---
    // 1. Œwiat³o gracza (latarka/pochodnia)
    float dist = distance(vFragPos, uCamPos);
    float flicker = sin(uTime * 10.0) * 0.05 + sin(uTime * 23.0) * 0.02; // Efekt migotania
    float lightStart = 2.5 + flicker;
    float lightEnd = 8.0 + flicker * 2.0;
    float playerLight = clamp((lightEnd - dist) / (lightEnd - lightStart), 0.0, 1.0);

    // 2. Œwiat³a punktowe zagadek (Pochodnie na œcianach)
    float puzzleLightTotal = 0.0;
    for(int i = 0; i < uActivePuzzleLights; i++) {
        float d = distance(vFragPos, uPuzzleLights[i]);
        float pFlicker = sin(uTime * 12.0 + float(i)) * 0.05;
        float pLight = clamp((4.5 + pFlicker - d) / (4.5 + pFlicker - 0.5), 0.0, 1.0);
        puzzleLightTotal = max(puzzleLightTotal, pLight);
    }

    vec3 torchColor = vec3(1.0, 0.85, 0.6); // Ciep³a barwa œwiat³a
    vec3 ambient = vec3(0.05, 0.05, 0.1);   // Ciemny niebieski ambient
    float combinedLight = max(playerLight, puzzleLightTotal);
    vec3 finalLight = (torchColor * combinedLight) + ambient;
    FragColor = vec4(baseColor.rgb * finalLight, baseColor.a);
})";

static void glfw_error_cb(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

namespace dungeon {

    void App::init_glfw() {
        glfwSetErrorCallback(glfw_error_cb);
        if (!glfwInit()) throw std::runtime_error("GLFW init failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window_ = glfwCreateWindow(cfg_.width, cfg_.height, cfg_.title.c_str(), nullptr, nullptr);
        if (!window_) throw std::runtime_error("Window creation failed");
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1); // V-Sync
        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* win, int w, int h) {
            auto* app = static_cast<App*>(glfwGetWindowUserPointer(win));
            if (app) app->on_resize(w, h);
            });
    }

    void App::init_gl() {
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
            throw std::runtime_error("GLAD load failed");
        glViewport(0, 0, cfg_.width, cfg_.height);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Macierz projekcji
        float aspect = static_cast<float>(cfg_.width) / static_cast<float>(cfg_.height);
        proj_ = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

        world_shader_ = gfx::Shader(kWorldVS, kWorldFS);
        wall_texture_ = load_texture("assets/models/wmremove-transformed.PNG");
        floor_texture_ = load_texture("assets/models/kamienna_posadzka.PNG");

        glfwMaximizeWindow(window_);
        int fb_w = 0, fb_h = 0;
        glfwGetFramebufferSize(window_, &fb_w, &fb_h);
        if (fb_w > 0 && fb_h > 0) on_resize(fb_w, fb_h);
    }

    // ... (init_imgui, shutdown_imgui - standardowa implementacja) ...
    void App::init_imgui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        ImGui_ImplOpenGL3_Init("#version 330");
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }

    void App::shutdown_imgui() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    /**
     * @brief Inicjalizacja silnika dŸwiêkowego miniaudio.
     * £aduje muzykê t³a (stream) oraz efekty dŸwiêkowe (do pamiêci).
     */
    void App::init_audio() {
        if (ma_engine_init(NULL, &audio_engine_) != MA_SUCCESS) {
            printf("Error: Audio init failed.\n");
            return;
        }
        if (ma_sound_init_from_file(&audio_engine_, "assets/audio/music.mp3", MA_SOUND_FLAG_STREAM, NULL, NULL, &bg_music_) == MA_SUCCESS) {
            ma_sound_set_looping(&bg_music_, MA_TRUE);
            ma_sound_set_volume(&bg_music_, 0.3f);
            ma_sound_start(&bg_music_);
        }
        if (ma_sound_init_from_file(&audio_engine_, "assets/audio/torch.mp3", 0, NULL, NULL, &sfx_torch_) == MA_SUCCESS) {
            ma_sound_set_looping(&sfx_torch_, MA_TRUE);
            ma_sound_set_volume(&sfx_torch_, 0.6f);
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

    /**
     * @brief Generuje siatkê (mesh) szeœcianu. U¿ywane jako fallback i debug.
     */
    void App::build_cube_mesh() {
        const float v[] = {
            // ... (dane wierzcho³ków szeœcianu) ...
            -0.5f,-0.5f,0.5f,0,0, 0.5f,-0.5f,0.5f,1,0, 0.5f,0.5f,0.5f,1,1, -0.5f,-0.5f,0.5f,0,0, 0.5f,0.5f,0.5f,1,1, -0.5f,0.5f,0.5f,0,1,
            0.5f,-0.5f,-0.5f,0,0, -0.5f,-0.5f,-0.5f,1,0, -0.5f,0.5f,-0.5f,1,1, 0.5f,-0.5f,-0.5f,0,0, -0.5f,0.5f,-0.5f,1,1, 0.5f,0.5f,-0.5f,0,1,
            -0.5f,-0.5f,-0.5f,0,0, -0.5f,-0.5f,0.5f,1,0, -0.5f,0.5f,0.5f,1,1, -0.5f,-0.5f,-0.5f,0,0, -0.5f,0.5f,0.5f,1,1, -0.5f,0.5f,-0.5f,0,1,
            0.5f,-0.5f,0.5f,0,0, 0.5f,-0.5f,-0.5f,1,0, 0.5f,0.5f,-0.5f,1,1, 0.5f,-0.5f,0.5f,0,0, 0.5f,0.5f,-0.5f,1,1, 0.5f,0.5f,0.5f,0,1,
            -0.5f,0.5f,0.5f,0,0, 0.5f,0.5f,0.5f,1,0, 0.5f,0.5f,-0.5f,1,1, -0.5f,0.5f,0.5f,0,0, 0.5f,0.5f,-0.5f,1,1, -0.5f,0.5f,-0.5f,0,1,
            -0.5f,-0.5f,-0.5f,0,0, 0.5f,-0.5f,-0.5f,1,0, 0.5f,-0.5f,0.5f,1,1, -0.5f,-0.5f,-0.5f,0,0, 0.5f,-0.5f,0.5f,1,1, -0.5f,-0.5f,0.5f,0,1
        };
        cube_vertex_count_ = sizeof(v) / sizeof(float) / 5;
        glGenVertexArrays(1, &cube_vao_); glGenBuffers(1, &cube_vbo_);
        glBindVertexArray(cube_vao_); glBindBuffer(GL_ARRAY_BUFFER, cube_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    /**
     * @brief Generuje geometriê œwiata na podstawie mapy poziomu.
     * £aduje modele œcian i pod³ogi, a nastêpnie rozmieszcza je w odpowiednich miejscach siatki.
     */
    void App::build_world_mesh() {
        std::vector<float> floor_vertices;
        std::vector<float> wall_vertices;
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string err;
        std::string base_dir = "assets/models/";

        // 1. ŒCIANY
        std::string wall_obj_path = base_dir + "Untitled.obj";
        std::vector<float> wall_model_data;
        if (tinyobj::LoadObj(&attrib, &shapes, &materials, &err, wall_obj_path.c_str(), base_dir.c_str())) {
            if (!materials.empty() && !materials[0].diffuse_texname.empty()) {
                if (wall_texture_) glDeleteTextures(1, &wall_texture_);
                wall_texture_ = load_texture((base_dir + materials[0].diffuse_texname).c_str());
            }
            // Ekstrakcja wierzcho³ków
            for (const auto& shape : shapes) {
                for (const auto& index : shape.mesh.indices) {
                    wall_model_data.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                    wall_model_data.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                    wall_model_data.push_back(attrib.vertices[3 * index.vertex_index + 2]);
                    if (index.texcoord_index >= 0) {
                        wall_model_data.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                        wall_model_data.push_back(attrib.texcoords[2 * index.texcoord_index + 1]);
                    }
                    else { wall_model_data.push_back(0.0f); wall_model_data.push_back(0.0f); }
                }
            }
        }

        // 2. POD£OGA
        attrib.vertices.clear(); attrib.texcoords.clear(); shapes.clear(); materials.clear(); err.clear();
        std::string floor_obj_path = base_dir + "kamien1.obj";
        std::vector<float> floor_model_data;
        if (tinyobj::LoadObj(&attrib, &shapes, &materials, &err, floor_obj_path.c_str(), base_dir.c_str())) {
            if (!materials.empty() && !materials[0].diffuse_texname.empty()) {
                if (floor_texture_) glDeleteTextures(1, &floor_texture_);
                floor_texture_ = load_texture((base_dir + materials[0].diffuse_texname).c_str());
            }
            for (const auto& shape : shapes) {
                for (const auto& index : shape.mesh.indices) {
                    floor_model_data.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                    floor_model_data.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                    floor_model_data.push_back(attrib.vertices[3 * index.vertex_index + 2]);
                    if (index.texcoord_index >= 0) {
                        floor_model_data.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                        floor_model_data.push_back(attrib.texcoords[2 * index.texcoord_index + 1]);
                    }
                    else { floor_model_data.push_back(0.0f); floor_model_data.push_back(0.0f); }
                }
            }
        }

        bool use_wall_model = !wall_model_data.empty();
        bool use_floor_model = !floor_model_data.empty();

        auto add_quad = [](std::vector<float>& buf, glm::vec3 a, glm::vec2 ua, glm::vec3 b, glm::vec2 ub, glm::vec3 c, glm::vec2 uc, glm::vec3 d, glm::vec2 ud) {
            auto push = [&buf](glm::vec3 v, glm::vec2 uv) { buf.push_back(v.x); buf.push_back(v.y); buf.push_back(v.z); buf.push_back(uv.x); buf.push_back(uv.y); };
            push(a, ua); push(b, ub); push(c, uc); push(a, ua); push(c, uc); push(d, ud);
            };

        // Generowanie geometrii dla ka¿dego kafelka mapy
        for (int y = 0; y < level_.h; ++y) {
            for (int x = 0; x < level_.w; ++x) {
                const auto cell = level_.cells[y * level_.w + x];

                // POD£OGA i SUFIT
                if (cell != io::Cell::Wall) {
                    // 1. Pod³oga (Floor)
                    if (use_floor_model) {
                        float scale = 0.53f, offset_x = 0.5f, offset_y = 0.0f, offset_z = 0.5f;
                        for (size_t i = 0; i < floor_model_data.size(); i += 5) {
                            floor_vertices.push_back(floor_model_data[i + 0] * scale + offset_x + x);
                            floor_vertices.push_back(floor_model_data[i + 1] * scale + offset_y);
                            floor_vertices.push_back(floor_model_data[i + 2] * scale + offset_z + y);
                            floor_vertices.push_back(floor_model_data[i + 3]);
                            floor_vertices.push_back(floor_model_data[i + 4]);
                        }
                    }
                    else {
                        add_quad(floor_vertices, { x,0,y }, { 0,0 }, { x + 1,0,y }, { 1,0 }, { x + 1,0,y + 1 }, { 1,1 }, { x,0,y + 1 }, { 0,1 });
                    }

                    // 2. SUFIT (Ceiling) - P³aski quad na wysokoœci 1.5
                    add_quad(floor_vertices,
                        { x + 1, 1.5f, y }, { 1.0f, 0.0f },
                        { x,     1.5f, y }, { 0.0f, 0.0f },
                        { x,     1.5f, y + 1 }, { 0.0f, 1.0f },
                        { x + 1, 1.5f, y + 1 }, { 1.0f, 1.0f }
                    );
                }

                // ŒCIANA (Wall)
                if (cell == io::Cell::Wall) {
                    if (use_wall_model) {
                        float scale = 0.5f, yscale = 1.0f, offset_x = 0.5f, offset_y = 0.5f, offset_z = 0.5f;
                        for (size_t i = 0; i < wall_model_data.size(); i += 5) {
                            wall_vertices.push_back(wall_model_data[i + 0] * scale + offset_x + x);
                            wall_vertices.push_back(wall_model_data[i + 1] * yscale + offset_y);
                            wall_vertices.push_back(wall_model_data[i + 2] * scale + offset_z + y);
                            wall_vertices.push_back(wall_model_data[i + 3]);
                            wall_vertices.push_back(wall_model_data[i + 4]);
                        }
                    }
                    else {
                        // Fallback cube
                        add_quad(wall_vertices, { x,0,y + 1 }, { 0,0 }, { x + 1,0,y + 1 }, { 1,0 }, { x + 1,1,y + 1 }, { 1,1 }, { x,1,y + 1 }, { 0,1 });
                        add_quad(wall_vertices, { x + 1,0,y }, { 0,0 }, { x,0,y }, { 1,0 }, { x,1,y }, { 1,1 }, { x + 1,1,y }, { 0,1 });
                    }
                }
            }
        }

        auto setup_vao = [](GLuint& vao, GLuint& vbo, const std::vector<float>& data) {
            if (data.empty()) return;
            glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
            glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
            };

        if (floor_vao_) glDeleteVertexArrays(1, &floor_vao_); if (floor_vbo_) glDeleteBuffers(1, &floor_vbo_);
        if (wall_vao_) glDeleteVertexArrays(1, &wall_vao_); if (wall_vbo_) glDeleteBuffers(1, &wall_vbo_);
        floor_vao_ = floor_vbo_ = wall_vao_ = wall_vbo_ = 0;

        floor_vertex_count_ = (int)(floor_vertices.size() / 5);
        wall_vertex_count_ = (int)(wall_vertices.size() / 5);
        setup_vao(floor_vao_, floor_vbo_, floor_vertices);
        setup_vao(wall_vao_, wall_vbo_, wall_vertices);
    }

    /**
     * @brief £aduje modele broni i przedmiotów interaktywnych.
     */
    void App::build_weapon_mesh() {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string err;
        std::string baseDir = "assets/models/";

        // 1. MIECZ (Sword)
        std::string swordPath = baseDir + "sword2.obj";
        if (tinyobj::LoadObj(&attrib, &shapes, &materials, &err, swordPath.c_str(), baseDir.c_str())) {
            weapon_texture_ = 0;
            if (!materials.empty() && !materials[0].diffuse_texname.empty())
                weapon_texture_ = load_texture((baseDir + materials[0].diffuse_texname).c_str());
            else
                weapon_texture_ = create_texture_from_color(0.6f, 0.6f, 0.7f);

            std::vector<float> vertices;
            for (const auto& shape : shapes) for (const auto& index : shape.mesh.indices) {
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 2]);
                if (index.texcoord_index >= 0) {
                    vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                    vertices.push_back(1.0f - attrib.texcoords[2 * index.texcoord_index + 1]);
                }
                else { vertices.push_back(0.0f); vertices.push_back(0.0f); }
            }
            weapon_vertex_count_ = (int)(vertices.size() / 5);

            if (weapon_vao_) glDeleteVertexArrays(1, &weapon_vao_); if (weapon_vbo_) glDeleteBuffers(1, &weapon_vbo_);
            glGenVertexArrays(1, &weapon_vao_); glGenBuffers(1, &weapon_vbo_);
            glBindVertexArray(weapon_vao_); glBindBuffer(GL_ARRAY_BUFFER, weapon_vbo_);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
            glBindVertexArray(0);
        }

        // 2. MIKSTURA (Potion)
        attrib.vertices.clear(); attrib.texcoords.clear(); shapes.clear(); materials.clear(); err.clear();
        std::string potionPath = baseDir + "potion.obj";
        if (tinyobj::LoadObj(&attrib, &shapes, &materials, &err, potionPath.c_str(), baseDir.c_str())) {
            if (!materials.empty() && !materials[0].diffuse_texname.empty())
                potion_texture_ = load_texture((baseDir + materials[0].diffuse_texname).c_str());

            std::vector<float> vertices;
            for (const auto& shape : shapes) for (const auto& index : shape.mesh.indices) {
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 2]);
                if (index.texcoord_index >= 0) {
                    vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                    vertices.push_back(1.0f - attrib.texcoords[2 * index.texcoord_index + 1]);
                }
                else { vertices.push_back(0.0f); vertices.push_back(0.0f); }
            }
            potion_vertex_count_ = (int)(vertices.size() / 5);
            glGenVertexArrays(1, &potion_vao_); glGenBuffers(1, &potion_vbo_);
            glBindVertexArray(potion_vao_); glBindBuffer(GL_ARRAY_BUFFER, potion_vbo_);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
            glBindVertexArray(0);
        }

        // 3. POCHODNIA (Torch)
        attrib.vertices.clear(); attrib.texcoords.clear(); shapes.clear(); materials.clear(); err.clear();
        std::string torchPath = baseDir + "torch.obj";
        if (tinyobj::LoadObj(&attrib, &shapes, &materials, &err, torchPath.c_str(), baseDir.c_str())) {
            if (!materials.empty() && !materials[0].diffuse_texname.empty())
                torch_texture_ = load_texture((baseDir + materials[0].diffuse_texname).c_str());

            std::vector<float> vertices;
            for (const auto& shape : shapes) for (const auto& index : shape.mesh.indices) {
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 2]);
                if (index.texcoord_index >= 0) {
                    vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                    vertices.push_back(1.0f - attrib.texcoords[2 * index.texcoord_index + 1]);
                }
                else { vertices.push_back(0.0f); vertices.push_back(0.0f); }
            }
            torch_vertex_count_ = (int)(vertices.size() / 5);
            if (torch_vao_) glDeleteVertexArrays(1, &torch_vao_); if (torch_vbo_) glDeleteBuffers(1, &torch_vbo_);
            glGenVertexArrays(1, &torch_vao_); glGenBuffers(1, &torch_vbo_);
            glBindVertexArray(torch_vao_); glBindBuffer(GL_ARRAY_BUFFER, torch_vbo_);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
            glBindVertexArray(0);
        }

        // 4. PORTAL
        attrib.vertices.clear(); attrib.texcoords.clear(); shapes.clear(); materials.clear(); err.clear();
        std::string portalPath = baseDir + "portal.obj";
        if (tinyobj::LoadObj(&attrib, &shapes, &materials, &err, portalPath.c_str(), baseDir.c_str())) {
            std::string portalTex = baseDir + "portal.jpeg";
            portal_texture_ = load_texture(portalTex.c_str());

            std::vector<float> vertices;
            for (const auto& shape : shapes) for (const auto& index : shape.mesh.indices) {
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 2]);
                if (index.texcoord_index >= 0) {
                    vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                    vertices.push_back(1.0f - attrib.texcoords[2 * index.texcoord_index + 1]);
                }
                else { vertices.push_back(0.0f); vertices.push_back(0.0f); }
            }
            portal_vertex_count_ = (int)(vertices.size() / 5);
            glGenVertexArrays(1, &portal_vao_); glGenBuffers(1, &portal_vbo_);
            glBindVertexArray(portal_vao_); glBindBuffer(GL_ARRAY_BUFFER, portal_vbo_);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
            glBindVertexArray(0);
        }
    }

    /**
     * @brief £aduje modele wrogów.
     */
    void App::build_enemy_mesh() {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string err;
        std::string baseDir = "assets/models/";

        // 1. ZOMBIE
        std::string zombiePath = baseDir + "zombie.obj";
        if (tinyobj::LoadObj(&attrib, &shapes, &materials, &err, zombiePath.c_str(), baseDir.c_str())) {
            if (!materials.empty() && !materials[0].diffuse_texname.empty())
                zombie_texture_ = load_texture((baseDir + materials[0].diffuse_texname).c_str());
            else zombie_texture_ = load_texture("assets/models/zombie.png");

            std::vector<float> vertices;
            for (const auto& shape : shapes) for (const auto& index : shape.mesh.indices) {
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 2]);
                if (index.texcoord_index >= 0) {
                    vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                    vertices.push_back(1.0f - attrib.texcoords[2 * index.texcoord_index + 1]);
                }
                else { vertices.push_back(0.0f); vertices.push_back(0.0f); }
            }
            zombie_vertex_count_ = (int)(vertices.size() / 5);
            glGenVertexArrays(1, &zombie_vao_); glGenBuffers(1, &zombie_vbo_);
            glBindVertexArray(zombie_vao_); glBindBuffer(GL_ARRAY_BUFFER, zombie_vbo_);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        }

        // 2. SZKIELET
        attrib.vertices.clear(); attrib.texcoords.clear(); shapes.clear(); materials.clear(); err.clear();
        std::string skeletonPath = baseDir + "skeleton.obj";
        if (tinyobj::LoadObj(&attrib, &shapes, &materials, &err, skeletonPath.c_str(), baseDir.c_str())) {
            if (!materials.empty() && !materials[0].diffuse_texname.empty())
                skeleton_texture_ = load_texture((baseDir + materials[0].diffuse_texname).c_str());
            else skeleton_texture_ = load_texture("assets/models/skeleton.png");

            std::vector<float> vertices;
            for (const auto& shape : shapes) for (const auto& index : shape.mesh.indices) {
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 2]);
                if (index.texcoord_index >= 0) {
                    vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                    vertices.push_back(1.0f - attrib.texcoords[2 * index.texcoord_index + 1]);
                }
                else { vertices.push_back(0.0f); vertices.push_back(0.0f); }
            }
            skeleton_vertex_count_ = (int)(vertices.size() / 5);
            glGenVertexArrays(1, &skeleton_vao_); glGenBuffers(1, &skeleton_vbo_);
            glBindVertexArray(skeleton_vao_); glBindBuffer(GL_ARRAY_BUFFER, skeleton_vbo_);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        }
        glBindVertexArray(0);
    }

} // namespace