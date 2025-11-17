#include "dungeon/core/App.hpp"
#include "dungeon/ui/Hud.hpp"

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

// proste shadery do œwiata
static const char* kWorldVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uProj;
uniform mat4 uView;
void main() {
  gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";

static const char* kWorldFS = R"(#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() {
  FragColor = vec4(uColor, 1.0);
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

        // shader œwiata
        world_shader_ = gfx::Shader(kWorldVS, kWorldFS);
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

        std::string path = "assets/maps/level1.map";

        if (!fs::exists(path)) {
            throw std::runtime_error(
                "Cannot open map: " + path +
                "\nCurrent directory: " + fs::current_path().string());
        }

        level_ = io::load_map_ascii(path);
    }

    void App::build_world_mesh() {
        // tworzymy osobne meshe: pod³ogi i œcian
        std::vector<float> floor_vertices;
        std::vector<float> wall_vertices;

        const int w = level_.w;
        const int h = level_.h;

        auto add_quad = [](std::vector<float>& buf,
            glm::vec3 a, glm::vec3 b,
            glm::vec3 c, glm::vec3 d) {
                // dwa trójk¹ty: a-b-c, a-c-d
                auto push = [&buf](glm::vec3 v) {
                    buf.push_back(v.x);
                    buf.push_back(v.y);
                    buf.push_back(v.z);
                    };
                push(a); push(b); push(c);
                push(a); push(c); push(d);
            };

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const auto cell = level_.cells[y * w + x];

                // POD£OGA – wszêdzie gdzie nie ma œciany
                if (cell == io::Cell::Floor) {
                    glm::vec3 a(x, 0.0f, y);
                    glm::vec3 b(x + 1, 0.0f, y);
                    glm::vec3 c(x + 1, 0.0f, y + 1);
                    glm::vec3 d(x, 0.0f, y + 1);
                    add_quad(floor_vertices, a, b, c, d);
                }

                // ŒCIANY – proste s³upki wokó³ kafelka typu Wall
                if (cell == io::Cell::Wall) {
                    float h0 = 0.0f;
                    float h1 = 1.5f;

                    // front (z+1)
                    add_quad(wall_vertices,
                        { x,     h0, y + 1 },
                        { x + 1, h0, y + 1 },
                        { x + 1, h1, y + 1 },
                        { x,     h1, y + 1 });

                    // back (z)
                    add_quad(wall_vertices,
                        { x + 1, h0, y },
                        { x,     h0, y },
                        { x,     h1, y },
                        { x + 1, h1, y });

                    // left (x)
                    add_quad(wall_vertices,
                        { x, h0, y },
                        { x, h0, y + 1 },
                        { x, h1, y + 1 },
                        { x, h1, y });

                    // right (x+1)
                    add_quad(wall_vertices,
                        { x + 1, h0, y + 1 },
                        { x + 1, h0, y },
                        { x + 1, h1, y },
                        { x + 1, h1, y + 1 });
                }
            }
        }

        floor_vertex_count_ = static_cast<int>(floor_vertices.size() / 3);
        wall_vertex_count_ = static_cast<int>(wall_vertices.size() / 3);

        // VAO/VBO – pod³oga
        glGenVertexArrays(1, &floor_vao_);
        glGenBuffers(1, &floor_vbo_);
        glBindVertexArray(floor_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, floor_vbo_);
        glBufferData(GL_ARRAY_BUFFER,
            floor_vertices.size() * sizeof(float),
            floor_vertices.data(),
            GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        // VAO/VBO – œciany
        glGenVertexArrays(1, &wall_vao_);
        glGenBuffers(1, &wall_vbo_);
        glBindVertexArray(wall_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, wall_vbo_);
        glBufferData(GL_ARRAY_BUFFER,
            wall_vertices.size() * sizeof(float),
            wall_vertices.data(),
            GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        glBindVertexArray(0);
    }

    void App::frame_begin() {
        glfwPollEvents();
        glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void App::frame_render() {
        // prosta kamera nad graczem, lekko odsuniêta
        float px = static_cast<float>(level_.player_x) + 0.5f;
        float pz = static_cast<float>(level_.player_y) + 0.5f;

        glm::vec3 cam_pos(px, 2.5f, pz + 4.0f);
        glm::vec3 cam_target(px, 0.5f, pz);
        glm::vec3 cam_up(0.0f, 1.0f, 0.0f);
        view_ = glm::lookAt(cam_pos, cam_target, cam_up);

        world_shader_.use();
        world_shader_.setMat4("uProj", &proj_[0][0]);
        world_shader_.setMat4("uView", &view_[0][0]);

        // pod³oga
        glBindVertexArray(floor_vao_);
        world_shader_.setVec3("uColor", 0.25f, 0.28f, 0.32f);
        glDrawArrays(GL_TRIANGLES, 0, floor_vertex_count_);

        // œciany
        glBindVertexArray(wall_vao_);
        world_shader_.setVec3("uColor", 0.78f, 0.80f, 0.85f);
        glDrawArrays(GL_TRIANGLES, 0, wall_vertex_count_);

        glBindVertexArray(0);
    }

    void App::frame_ui() {
        dungeon::ui::HudState hud;
        hud.log = "Starter uruchomiony\nMapa: level1.map";
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

} // namespace dungeon
    