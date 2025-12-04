#ifndef DUNGEON_CORE_APP_HPP
#define DUNGEON_CORE_APP_HPP

#include <string>
#include <vector> // Dodane: potrzebne dla std::vector

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/mat4x4.hpp>

// Dodane: Potrzebne, ¿eby klasa App zna³a typy tinyobj
#include "tiny_obj_loader.h" 

#include "dungeon/gfx/Shader.hpp"
#include "dungeon/io/MapLoader.hpp"
#include "game/clases/Player.h" // U¿ywamy Twojej klasy gracza

// Forward declaration
struct GLFWwindow;

namespace dungeon {

    struct AppConfig {
        int width = 1280;
        int height = 720;
        std::string title = "Dungeon Starter";
    };

    enum class CameraMode {
        FirstPerson,
        ThirdPerson
    };

    enum class GameState {
        MainMenu,
        Options,
        Playing
    };

    class App {
    public:
        explicit App(const AppConfig& cfg);
        ~App();

        void run();

    private:
        // Inicjalizacja systemów
        void init_glfw();
        void init_gl();
        void init_imgui();
        void shutdown_imgui();

        // Logika gry
        void load_level();
        bool can_move_to(int x, int y) const;
        void build_world_mesh(); // Budowanie siatki (œciany + pod³oga)
        void handle_input();

        // Pêtla g³ówna
        void frame_begin();
        void frame_render();
        void frame_ui();
        void frame_end();

        // Menu
        void render_main_menu();
        void render_options_menu();

        // Callbacki i pomocnicze
        void on_resize(int width, int height);
        GLuint load_texture(const char* path);

    private:
        AppConfig   cfg_;
        GLFWwindow* window_ = nullptr;

        // Renderowanie
        gfx::Shader world_shader_;
        glm::mat4   proj_{ 1.0f };
        glm::mat4   view_{ 1.0f };

        // Mapa i œwiat
        io::Level   level_{};
        std::string current_map_name_;

        // Bufory OpenGL (VAO/VBO)
        GLuint floor_vao_ = 0;
        GLuint floor_vbo_ = 0;
        GLuint wall_vao_ = 0;
        GLuint wall_vbo_ = 0;
        int    floor_vertex_count_ = 0;
        int    wall_vertex_count_ = 0;

        // Tekstury
        GLuint wall_texture_ = 0;
        GLuint floor_texture_ = 0;

        // Gracz i stan gry
        ::Player   player_; // Odwo³anie do globalnego Player z game/clases/Player.h
        CameraMode camera_mode_ = CameraMode::FirstPerson;
        GameState  state_ = GameState::MainMenu;
        bool       show_menu_ = false;

        // Input flagi (zapobieganie ci¹g³emu wciskaniu)
        bool left_was_down_ = false;
        bool right_was_down_ = false;
        bool up_was_down_ = false;
        bool m_was_down_ = false;

        // --- Zmienne dla TinyObjLoader (z App1.hpp) ---
        // S¹ potrzebne, jeœli chcia³byœ przechowywaæ dane modelu w pamiêci,
        // chocia¿ w mojej implementacji App.cpp u¿y³em zmiennej statycznej wewn¹trz funkcji.
        // Zostawiam je tutaj, aby pasowa³y do Twojego stylu, ale nie s¹ krytyczne, 
        // jeœli u¿ywasz wersji App.cpp, któr¹ wys³a³em wczeœniej.
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;

        const std::string MODEL_PATH = "assets/models/wall.obj";
    };

} // namespace dungeon

#endif // DUNGEON_CORE_APP_HPP