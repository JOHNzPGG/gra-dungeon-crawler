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

/**
 * @brief Konfiguracja aplikacji 3D
 */
struct AppConfig {
    int width = 1280;           /**< Szerokość okna */
    int height = 720;           /**< Wysokość okna */
    std::string title = "Dungeon Starter"; /**< Tytuł okna */
};

/**
 * @brief Tryb kamery
 */
enum class CameraMode {
    FirstPerson,   /**< Pierwsza osoba */
    ThirdPerson    /**< Trzecia osoba */
};

/**
 * @brief Stan gry
 */
enum class GameState {
    MainMenu,    /**< Menu główne */
    Options,     /**< Opcje */
    Credits,     /**< Ekran autorów */
    Playing,     /**< Gra w toku */
    GameOver,    /**< Ekran śmierci */
    Victory,     /**< Ekran zwycięstwa */
    Paused       /**< Pauza */
};

/**
 * @brief Przedmiot leżący w świecie 3D
 */
struct WorldItem {
    Item* itemData;        /**< Wskaźnik na dane przedmiotu */
    glm::vec3 position;    /**< Pozycja w świecie */
    bool isAlive;          /**< Czy przedmiot jest nadal aktywny */
};

/**
 * @brief Pochodnia w puzzlu
 */
struct PuzzleTorch {
    int x, y;         /**< Pozycja na mapie */
    bool is_lit;      /**< Czy pochodnia jest zapalona */
};

/**
 * @brief Płyta naciskowa w puzzlu
 */
struct PressurePlate {
    int x, y;         /**< Pozycja na mapie */
    int id;           /**< ID płyty */
    int count;        /**< Licznik aktywacji */
};

/**
 * @brief Główna klasa aplikacji gry 3D
 */
class App {
public:
    /**
     * @brief Konstruktor aplikacji
     * @param cfg Konfiguracja aplikacji
     */
    explicit App(const AppConfig& cfg);

    /** @brief Destruktor aplikacji */
    ~App();

    /** @brief Główna pętla gry */
    void run();

private:
    // --- PUZZLE ---
    std::vector<PuzzleTorch> puzzle_torches_;   /**< Wszystkie pochodnie w puzzlach */
    std::vector<PressurePlate> pressure_plates_; /**< Wszystkie płyty naciskowe */
    int last_puzzle_x_ = -1;  /**< Ostatnia x w puzzlu */
    int last_puzzle_y_ = -1;  /**< Ostatnia y w puzzlu */
    bool puzzles_solved_ = false; /**< Flaga rozwiązania puzzli */
    int current_stage_idx_ = 0;   /**< Aktualny etap/puzzle */

    // --- INICJALIZACJA ---
    void init_glfw();       /**< Inicjalizacja GLFW */
    void init_gl();         /**< Inicjalizacja OpenGL */
    void init_imgui();      /**< Inicjalizacja ImGui */
    void shutdown_imgui();  /**< Zamykanie ImGui */

    void load_level();       /**< Ładuje aktualny poziom */
    bool can_move_to(int x, int y) const; /**< Sprawdza, czy gracz może się przesunąć */

    void build_world_mesh(); /**< Buduje siatkę świata */
    void handle_input();     /**< Obsługuje wejście użytkownika */

    Entity* GetEnemyInFront(const Entity& unit); /**< Zwraca wroga przed jednostką */
    std::vector<Entity*> ResolveSkillTarget(const Entity& unit, Skill* skill); /**< Zwraca cele umiejętności */

    void EnemiesTurn(); /**< Tura wrogów */

    void toggle_puzzle_torch(int x, int y); /**< Zapala/zgasi pochodnię puzzla */
    void update_puzzles();                  /**< Aktualizuje stan puzzli */

    void frame_begin();   /**< Przygotowanie ramki */
    void frame_render();  /**< Renderowanie sceny */
    void frame_ui();      /**< Renderowanie UI */
    void frame_end();     /**< Kończenie ramki */

    void render_main_menu(); /**< Renderowanie menu głównego */
    void render_options_menu(); /**< Renderowanie menu opcji */

    void init_puzzles(const io::Level &L); /**< Inicjalizacja puzzli z poziomu */

    void on_resize(int width, int height); /**< Obsługa zmiany rozmiaru okna */

    GLuint load_texture(const char* path); /**< Ładuje teksturę z pliku */

    void build_cube_mesh();              /**< Buduje meshe kostki */
    void spawn_entities_from_level();    /**< Tworzy jednostki w poziomie */

    void reset_game();       /**< Resetuje mapę i ekwipunek */
    void render_game_over(); /**< Renderuje ekran śmierci */
    void update_combat();    /**< Obsługuje sekwencję walki */

    std::vector<bool> visited_cells_; /**< Oznaczenie odwiedzonych komórek */
    void update_exploration();        /**< Aktualizacja eksploracji */
    bool check_los(int x1, int y1, int x2, int y2) const; /**< Sprawdza linię widoczności */

    void build_weapon_mesh(); /**< Buduje mesh broni */
    void build_enemy_mesh();  /**< Buduje meshe wrogów */

    std::vector<std::string> map_list_; /**< Lista map */
    int current_level_idx_ = 0;         /**< Indeks aktualnego poziomu */
    void load_next_level();             /**< Ładuje następny poziom */
    void render_victory_screen();       /**< Renderuje ekran zwycięstwa */

    PuzzleTorch* get_puzzle_torch(int x, int y); /**< Pobiera pochodnię puzzla na współrzędnych */

private:
    // --- KONFIGURACJA I OKNO ---
    AppConfig   cfg_;        /**< Konfiguracja aplikacji */
    GLFWwindow* window_ = nullptr; /**< Wskaźnik na okno GLFW */

    // --- SHADERY I MACIERZE ---
    gfx::Shader world_shader_; /**< Shader świata */
    glm::mat4 proj_{ 1.0f };   /**< Macierz projekcji */
    glm::mat4 view_{ 1.0f };   /**< Macierz widoku */

    io::Level   level_{};       /**< Obiekt poziomu */
    std::string current_map_name_; /**< Nazwa aktualnej mapy */

    // --- MESHE ŚWIATA ---
    GLuint floor_vao_ = 0, floor_vbo_ = 0; /**< Floor VAO i VBO */
    GLuint wall_vao_ = 0, wall_vbo_ = 0;   /**< Wall VAO i VBO */
    int floor_vertex_count_ = 0;           /**< Liczba wierzchołków podłogi */
    int wall_vertex_count_ = 0;            /**< Liczba wierzchołków ścian */

    // --- MESHE BRONI, POTIONS, WROGÓW ---
    GLuint weapon_vao_ = 0, weapon_vbo_ = 0, weapon_texture_ = 0;
    int weapon_vertex_count_ = 0;

    GLuint zombie_vao_ = 0, zombie_vbo_ = 0, zombie_texture_ = 0;
    int zombie_vertex_count_ = 0;

    GLuint skeleton_vao_ = 0, skeleton_vbo_ = 0, skeleton_texture_ = 0;
    int skeleton_vertex_count_ = 0;

    GLuint potion_vao_ = 0, potion_vbo_ = 0, potion_texture_ = 0;
    int potion_vertex_count_ = 0;
    bool h_was_down_ = false; /**< Flaga picia mikstur */

    GLuint torch_vao_ = 0, torch_vbo_ = 0, torch_texture_ = 0;
    int torch_vertex_count_ = 0;

    GLuint wall_texture_ = 0;  /**< Tekstura ścian */
    GLuint floor_texture_ = 0; /**< Tekstura podłogi */

    ::Player   player_;           /**< Gracz */
    std::vector<Enemy*> enemies_; /**< Wrogowie */
    CameraMode camera_mode_ = CameraMode::FirstPerson; /**< Tryb kamery */

    // --- INPUT ---
    bool left_was_down_ = false;  /**< Czy klawisz w lewo był wciśnięty */
    bool right_was_down_ = false;
    bool up_was_down_ = false;
    bool esc_was_down_ = false;
    bool atk_was_down_ = false;
    bool k1_was_down_  = false;
    bool k2_was_down_  = false;
    bool k3_was_down_  = false;

    // --- MENU I STAN GRY ---
    bool show_menu_ = false; /**< Flaga menu */
    GameState state_ = GameState::MainMenu; /**< Aktualny stan gry */

    GLuint cube_vao_ = 0, cube_vbo_ = 0;
    int cube_vertex_count_ = 0;
    std::vector<glm::vec3> enemies_world_pos_; /**< Pozycje wrogów w świecie */
    bool has_held_item_ = false;
    GLuint item_texture_ = 0; /**< Tekstura trzymanego przedmiotu */
    std::vector<WorldItem> world_items_; /**< Przedmioty w świecie */

    // --- WALKI ---
    float attack_anim_timer_ = 0.0f;       /**< Czas animacji ataku */
    const float kAttackDuration_ = 0.25f;  /**< Długość animacji ataku */
    bool combat_lock_ = false;             /**< Czy sterowanie jest zablokowane */
    float combat_timer_ = 0.0f;            /**< Licznik czasu sekwencji walki */
    bool enemy_riposte_pending_ = false;   /**< Flaga kontrataku wroga */
    Entity* current_combat_target_ = nullptr; /**< Aktualny cel walki */

    // --- RUCH ---
    bool is_moving_ = false;          /**< Flaga ruchu gracza */
    glm::vec3 move_start_pos_;        /**< Pozycja startowa ruchu */
    glm::vec3 move_target_pos_;       /**< Pozycja końcowa ruchu */
    float move_timer_ = 0.0f;         /**< Licznik ruchu */
    const float kMoveDuration_ = 0.25f; /**< Czas trwania kroku */

    float trauma_ = 0.1f; /**< Siła trzęsienia ekranu */

    // --- AUDIO ---
    ma_engine audio_engine_; /**< Silnik audio */
    ma_sound bg_music_;      /**< Muzyka w tle */
    ma_sound sfx_torch_;     /**< Dźwięk pochodni */

    void init_audio();        /**< Inicjalizacja audio */
    void update_audio_state();/**< Aktualizacja stanu audio */

    float master_volume_ = 0.5f; /**< Głośność globalna */

    // --- MENU PAUZY, ŁADOWANIA, AUTORZY ---
    void render_pause_menu();    /**< Renderowanie menu pauzy */
    GameState previous_state_ = GameState::MainMenu;

    void render_loading_screen(); /**< Renderowanie ekranu ładowania */
    float menu_timer_ = 0.0f;    /**< Licznik czasu menu */
    void render_credits();        /**< Renderowanie ekranu autorów */

    void push_retro_style();      /**< Ustawienie stylu retro przycisków */
    void pop_retro_style();       /**< Przywrócenie stylu przycisków */

    GLuint create_texture_from_color(float r, float g, float b); /**< Tworzy teksturę z koloru */
};

} // namespace dungeon

#endif // DUNGEON_CORE_APP_HPP
