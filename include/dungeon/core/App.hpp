#ifndef DUNGEON_CORE_APP_HPP
#define DUNGEON_CORE_APP_HPP

#include <map>
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
#include "miniaudio.h"


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
        Credits,
        Playing,
        GameOver,
        Victory,
        Paused
    };

    // Struktura reprezentująca przedmiot leżący na ziemi 
    struct WorldItem {
        Item* itemData;     // Wskaźnik do danych przedmiotu (statystyki, nazwa)
        glm::vec3 position; // Gdzie leży
        bool isAlive;       // Czy jeszcze nie został podniesiony
    };

    struct PuzzleTorch { int x, y; bool is_lit; };
    struct PressurePlate { int x, y; int id; int count; };




    class App {
    public:
        explicit App(const AppConfig& cfg);
        ~App();

        void run();

    private:
        std::vector<PuzzleTorch> puzzle_torches_;
        std::vector<PressurePlate> pressure_plates_;
        int last_puzzle_x_ = -1, last_puzzle_y_ = -1;
        bool puzzles_solved_ = false;
        int current_stage_idx_ = 0;

        void init_glfw();
        void init_gl();
        void init_imgui();
        void shutdown_imgui();

        void load_level();
        bool can_move_to(int x, int y) const;

        //void check_sequence_step(int x, int y);

        void build_world_mesh();
        void handle_input();

        Entity* GetEnemyInFront(const Entity& unit);
        std::vector<Entity*> ResolveSkillTarget(const Entity& unit, Skill* skill);


        void EnemiesTurn();

        void toggle_puzzle_torch(int x, int y);

        void update_puzzles();

        //void update_step_puzzle();

        void frame_begin();
        void frame_render();
        void frame_ui();
        void frame_end();

        void render_main_menu();
        void render_options_menu();

        void init_puzzles(const io::Level &L);

        void on_resize(int width, int height);

        GLuint load_texture(const char* path);

        void build_cube_mesh();
        void spawn_entities_from_level();

        void reset_game();       // Czyści mapę i EQ
        void render_game_over(); // Rysuje ekran śmierci
        void update_combat();    // Obsługuje sekwencję 1-sekundową

        std::vector<bool> visited_cells_;
        void update_exploration();
        bool check_los(int x1, int y1, int x2, int y2) const;
        void build_weapon_mesh();
        void build_enemy_mesh();

        std::vector<std::string> map_list_ = {
            "assets/maps/levelPuzzle1.map",
            "assets/maps/level1.map",
            "assets/maps/level2.map",
            "assets/maps/level3.map",
            "assets/maps/level4.map",
            "assets/maps/level5.map"
        };
        int current_level_idx_ = 0;

        void load_next_level();
        void render_victory_screen();
        PuzzleTorch* get_puzzle_torch(int x, int y);

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

        GLuint weapon_vao_ = 0;
        GLuint weapon_vbo_ = 0;
        GLuint weapon_texture_ = 0;
        int weapon_vertex_count_ = 0;

        // ZOMBIE
        GLuint zombie_vao_ = 0;
        GLuint zombie_vbo_ = 0;
        GLuint zombie_texture_ = 0;
        int zombie_vertex_count_ = 0;

        // SZKIELET
        GLuint skeleton_vao_ = 0;
        GLuint skeleton_vbo_ = 0;
        GLuint skeleton_texture_ = 0;
        int skeleton_vertex_count_ = 0;

        //POTION

        GLuint potion_vao_ = 0;
        GLuint potion_vbo_ = 0;
        GLuint potion_texture_ = 0;
        int potion_vertex_count_ = 0;
        bool h_was_down_ = false; // Do picia mikstur

        //TORCH

        GLuint torch_vao_ = 0;
        GLuint torch_vbo_ = 0;
        GLuint torch_texture_ = 0;
        int torch_vertex_count_ = 0;

        // PORTAL

        GLuint portal_vao_ = 0;
        GLuint portal_vbo_ = 0;
        GLuint portal_texture_ = 0;
        int portal_vertex_count_ = 0;

        GLuint wall_texture_ = 0;
        GLuint floor_texture_ = 0;

        ::Player   player_;
        std::vector<Enemy*> enemies_;
        CameraMode camera_mode_ = CameraMode::FirstPerson;


        bool left_was_down_ = false;
        bool right_was_down_ = false;
        bool up_was_down_ = false;
        bool esc_was_down_ = false;
        bool atk_was_down_ = false;
        bool k1_was_down_  = false;
        bool k2_was_down_  = false;
        bool k3_was_down_  = false;
        bool weapon_swap_lock_ = false; // Blokada podnoszenia broni po wymianie

        bool show_menu_ = false;
        GameState state_ = GameState::MainMenu;

        GLuint cube_vao_ = 0;
        GLuint cube_vbo_ = 0;
        int cube_vertex_count_ = 0;
        std::vector<glm::vec3> enemies_world_pos_;
        bool has_held_item_ = false;
        GLuint item_texture_ = 0;
        std::vector<WorldItem> world_items_;
        float attack_anim_timer_ = 0.0f;       // Obecny czas animacji (0 = brak ataku)
        const float kAttackDuration_ = 0.25f;  // Jak długo trwa cios (w sekundach)
        bool combat_lock_ = false;       // Czy sterowanie jest zablokowane?
        float combat_timer_ = 0.0f;      // Licznik czasu sekwencji (1.0s -> 0.0s)
        bool enemy_riposte_pending_ = false; // Czy wróg ma nam oddać w połowie sekwencji?
        Entity* current_combat_target_ = nullptr; // Kogo bijemy (żeby wiedział kto oddaje)

        bool is_moving_ = false;          // Czy gracz jest w trakcie kroku?
        glm::vec3 move_start_pos_;        // Skąd wyruszyliśmy (wizualnie)
        glm::vec3 move_target_pos_;       // Dokąd idziemy (wizualnie)
        float move_timer_ = 0.0f;         // Licznik czasu
        const float kMoveDuration_ = 0.25f; // Czas trwania kroku (0.25s jest idealne)

        float trauma_ = 0.1f; // Poziom trzęsienia ekranu (od 0.0 do 1.0)

        // --- AUDIO ---
        ma_engine audio_engine_; // Silnik dźwiękowy
        ma_sound bg_music_;      // Muzyka w tle
        ma_sound sfx_torch_;     // Dźwięk pochodni

        void init_audio();       // Funkcja inicjalizująca
        void update_audio_state(); // Funkcja sprawdzająca czy włączyć/wyłączyć ogień

        // --- AUDIO & SETTINGS ---
        float master_volume_ = 0.5f; // Domyślna głośność 50%

        // --- FUNKCJE RENDERUJĄCE ---
        void render_pause_menu();    // Nowe menu pod ESC
        GameState previous_state_ = GameState::MainMenu;

        void render_loading_screen();

        float menu_timer_ = 0.0f; // Do obracania kamery w menu
        void render_credits();    // Funkcja rysująca ekran autorów

        // Funkcja pomocnicza do stylu przycisków
        void push_retro_style();
        void pop_retro_style();

        GLuint create_texture_from_color(float r, float g, float b);

    };

} // namespace dungeon

#endif // DUNGEON_CORE_APP_HPP
