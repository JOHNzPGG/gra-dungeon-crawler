#ifndef DUNGEON_CORE_APP_HPP
#define DUNGEON_CORE_APP_HPP

#include <string>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <glm/glm.hpp>

#include <glm/mat4x4.hpp>

#include "dungeon/gfx/Shader.hpp"
#include "dungeon/io/MapLoader.hpp"
#include "game/clases/Player.h"
#include "game/clases/Enemy.h"
#include "game/clases/Skill.h"


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
        Playing,
        GameOver
    };

    // Struktura reprezentująca przedmiot leżący na ziemi w świecie 3D
    struct WorldItem {
        Item* itemData;     // Wskaźnik do danych przedmiotu (statystyki, nazwa)
        glm::vec3 position; // Gdzie leży
        bool isAlive;       // Czy jeszcze nie został podniesiony
    };

    class App {
    public:
        explicit App(const AppConfig& cfg);
        ~App();

        void run();

    private:
        void init_glfw();
        void init_gl();
        void init_imgui();
        void shutdown_imgui();

        void load_level();
        bool can_move_to(int x, int y) const;

        void build_world_mesh();
        void handle_input();

        Entity* GetEnemyInFront(const Entity& unit);
        std::vector<Entity*> ResolveSkillTarget(const Entity& unit, Skill* skill);


        void EnemiesTurn();

        void frame_begin();
        void frame_render();
        void frame_ui();
        void frame_end();

        void render_main_menu();
        void render_options_menu();
        void on_resize(int width, int height);

        GLuint load_texture(const char* path);

        void build_cube_mesh();
        void spawn_entities_from_level();

        void reset_game();       // Czyści mapę i EQ
        void render_game_over(); // Rysuje ekran śmierci
        void update_combat();    // Obsługuje sekwencję 1-sekundową

    private:
        AppConfig   cfg_;
        GLFWwindow* window_ = nullptr;

        gfx::Shader world_shader_;
        glm::mat4   proj_{ 1.0f };
        glm::mat4   view_{ 1.0f };

        io::Level   level_{};
        std::string current_map_name_;

        GLuint floor_vao_ = 0;
        GLuint floor_vbo_ = 0;
        GLuint wall_vao_ = 0;
        GLuint wall_vbo_ = 0;
        int    floor_vertex_count_ = 0;
        int    wall_vertex_count_ = 0;

        GLuint wall_texture_ = 0;
        GLuint floor_texture_ = 0;

        ::Player   player_;
        std::vector<Enemy*> enemies_;
        CameraMode camera_mode_ = CameraMode::FirstPerson;


        bool left_was_down_ = false;
        bool right_was_down_ = false;
        bool up_was_down_ = false;
        bool m_was_down_ = false;
        bool atk_was_down_ = false;
        bool k1_was_down_  = false;
        bool k2_was_down_  = false;
        bool k3_was_down_  = false;

        bool show_menu_ = false;
        GameState state_ = GameState::MainMenu;

        GLuint cube_vao_ = 0;
        GLuint cube_vbo_ = 0;
        int cube_vertex_count_ = 0;
        std::vector<glm::vec3> enemies_world_pos_;
        bool has_held_item_ = false;
        GLuint item_texture_ = 0; // kiedy� tekstura
        std::vector<WorldItem> world_items_;
        float attack_anim_timer_ = 0.0f;       // Obecny czas animacji (0 = brak ataku)
        const float kAttackDuration_ = 0.25f;  // Jak długo trwa cios (w sekundach)
        bool combat_lock_ = false;       // Czy sterowanie jest zablokowane?
        float combat_timer_ = 0.0f;      // Licznik czasu sekwencji (1.0s -> 0.0s)
        bool enemy_riposte_pending_ = false; // Czy wróg ma nam oddać w połowie sekwencji?
        Entity* current_combat_target_ = nullptr; // Kogo bijemy (żeby wiedział kto oddaje)

    };

} // namespace dungeon

#endif // DUNGEON_CORE_APP_HPP
