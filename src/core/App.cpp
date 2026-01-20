#include "dungeon/core/App.hpp"
#include "dungeon/ui/Hud.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

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
#include <cmath>
#include <filesystem>
#include <map>

/**
 * @brief Callback błędu GLFW
 *
 * Wypisuje kod i opis błędu do stderr.
 * @param code Kod błędu
 * @param desc Opis błędu
 */
static void glfw_error_cb(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

/** Vertex shader świata 3D */
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
}
)";

/** Fragment shader świata 3D z efektem pochodni i zagadki */
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
    if (uUseTex == 1) {
        baseColor = texture(uTex, vTexCoord) * uColor;
    } else {
        baseColor = uColor;
    }
    if(baseColor.a < 0.1) discard;

    float dist = distance(vFragPos, uCamPos);
    float flicker = sin(uTime * 10.0) * 0.05 + sin(uTime * 23.0) * 0.02;
    float lightStart = 2.5 + flicker;
    float lightEnd = 8.0 + flicker * 2.0;
    float playerLight = clamp((lightEnd - dist) / (lightEnd - lightStart), 0.0, 1.0);

    float puzzleLightTotal = 0.0;
    for(int i = 0; i < uActivePuzzleLights; i++) {
        float d = distance(vFragPos, uPuzzleLights[i]);
        float pFlicker = sin(uTime * 12.0 + float(i)) * 0.05;
        float pLight = clamp((4.5 + pFlicker - d) / (4.5 + pFlicker - 0.5), 0.0, 1.0);
        puzzleLightTotal = max(puzzleLightTotal, pLight);
    }

    vec3 torchColor = vec3(1.0, 0.85, 0.6);
    vec3 ambient = vec3(0.05, 0.05, 0.1);

    float combinedLight = max(playerLight, puzzleLightTotal);
    vec3 finalLight = (torchColor * combinedLight) + ambient;

    FragColor = vec4(baseColor.rgb * finalLight, baseColor.a);
}
)";

namespace dungeon {

/**
 * @brief Konstruktor głównej aplikacji Dungeon
 *
 * Inicjalizuje GLFW, OpenGL, ImGui, audio, ładuje poziom startowy,
 * buduje siatki świata, jednostki i przedmioty, a także domyślny skill gracza.
 * @param cfg Konfiguracja aplikacji (rozmiar okna i tytuł)
 */
App::App(const AppConfig& cfg)
    : cfg_(cfg),
      player_(1, 1, 0)
{
    init_glfw();
    init_gl();
    init_imgui();
    init_audio();
    render_loading_screen();
    load_level();
    build_world_mesh();
    build_cube_mesh();
    build_weapon_mesh();
    build_enemy_mesh();
    spawn_entities_from_level();

    player_.LearnSkill(new Skill("Strong Hit", 1, 15));
    if (!player_.skills.empty()) {
        player_.skills[0]->offsets.push_back({ 0, -1 });
    }
}

/**
 * @brief Destruktor klasy App
 *
 * Czyści pamięć GPU, usuwa jednostki, przedmioty i de-inicjalizuje audio oraz okno GLFW.
 */
App::~App() {
    shutdown_imgui();

    for (auto* e : enemies_) delete e;
    enemies_.clear();

    if (floor_vbo_) glDeleteBuffers(1, &floor_vbo_);
    if (floor_vao_) glDeleteVertexArrays(1, &floor_vao_);
    if (wall_vbo_)  glDeleteBuffers(1, &wall_vbo_);
    if (wall_vao_)  glDeleteVertexArrays(1, &wall_vao_);
    if (weapon_vbo_) glDeleteBuffers(1, &weapon_vbo_);
    if (weapon_vao_) glDeleteVertexArrays(1, &weapon_vao_);

    if (zombie_vao_) glDeleteVertexArrays(1, &zombie_vao_);
    if (zombie_vbo_) glDeleteBuffers(1, &zombie_vbo_);

    if (skeleton_vao_) glDeleteVertexArrays(1, &skeleton_vao_);
    if (skeleton_vbo_) glDeleteBuffers(1, &skeleton_vbo_);

    if (window_) {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    if (cube_vbo_) glDeleteBuffers(1, &cube_vbo_);
    if (cube_vao_) glDeleteVertexArrays(1, &cube_vao_);

    if (potion_vao_) glDeleteVertexArrays(1, &potion_vao_);
    if (potion_vbo_) glDeleteBuffers(1, &potion_vbo_);

    if (torch_vao_) glDeleteVertexArrays(1, &torch_vao_);
    if (torch_vbo_) glDeleteBuffers(1, &torch_vbo_);

    ma_sound_uninit(&bg_music_);
    ma_sound_uninit(&sfx_torch_);
    ma_engine_uninit(&audio_engine_);
}

/**
 * @brief Inicjalizacja GLFW
 *
 * Tworzy okno, ustawia callbacki i kontekst OpenGL.
 * @throws std::runtime_error Jeśli inicjalizacja GLFW lub okna się nie powiedzie
 */
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

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* win, int w, int h) {
        auto* app = static_cast<App*>(glfwGetWindowUserPointer(win));
        if (app) app->on_resize(w, h);
    });
}

/**
 * @brief Inicjalizacja OpenGL i shaderów
 *
 * Włącza test głębokości, mieszanie kolorów, tworzy shader świata i ładuje tekstury.
 * @throws std::runtime_error Jeśli GLAD nie załaduje funkcji OpenGL
 */
void App::init_gl() {
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        throw std::runtime_error("GLAD load failed");

    glViewport(0, 0, cfg_.width, cfg_.height);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

/**
 * @brief Callback zmiany rozdzielczości okna
 * @param width Nowa szerokość okna
 * @param height Nowa wysokość okna
 */
void App::on_resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    cfg_.width = width;
    cfg_.height = height;
    glViewport(0, 0, width, height);
    float aspect = static_cast<float>(width) / static_cast<float>(height);
    proj_ = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
}

/**
 * @brief Inicjalizacja ImGui
 */
void App::init_imgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    ImGui_ImplOpenGL3_CreateFontsTexture();
}

/**
 * @brief Wyłączenie i czyszczenie ImGui
 */
void App::shutdown_imgui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

/**
 * @brief Ładowanie poziomu z pliku mapy
 */
void App::load_level() {
    namespace fs = std::filesystem;

    if (current_map_name_.empty()) current_map_name_ = map_list_[0];
    std::string path = current_map_name_;

    if (!fs::exists(path)) {
        printf("Brak mapy: %s. Próba wczytania test.map\n", path.c_str());
        path = "assets/maps/test.map";
        if (!fs::exists(path)) throw std::runtime_error("BRAK ZADNYCH MAP!");
    }

    level_ = io::load_map_ascii(path);

    player_.GameX = level_.player_x;
    player_.GameY = level_.player_y;
    player_.yaw = level_.player_start_yaw;
    player_.RenderPosition = glm::vec3(level_.player_x, 0.0f, level_.player_y);

    is_moving_ = false;
    move_timer_ = 0.0f;
    move_start_pos_ = player_.RenderPosition;
    move_target_pos_ = player_.RenderPosition;

    visited_cells_.assign(level_.w * level_.h, false);
    update_exploration();
}

/**
 * @brief Tworzy wrogów, przedmioty i zagadki na podstawie danych poziomu
 */
void App::spawn_entities_from_level() {
    for (auto* e : enemies_) delete e;
    enemies_.clear();
    world_items_.clear();
    puzzle_torches_.clear();
    pressure_plates_.clear();
    puzzles_solved_ = false;

    for (const auto& spawn : level_.enemy_spawns) {
        Enemy* newEnemy = nullptr;
        if (spawn.type == 'Z') {
            newEnemy = new Enemy(spawn.x, spawn.y, 180, 140, 140, 1, 25, "Zombie");
        } else if (spawn.type == 'S') {
            newEnemy = new Enemy(spawn.x, spawn.y, 180, 60, 60, 2, 10, "Skeleton");
        } else {
            newEnemy = new Enemy(spawn.x, spawn.y, 180, 60, 60, 2, 10, "Skeleton");
        }

        if (newEnemy) {
            newEnemy->yaw = 0.0f;
            float vx = (float)newEnemy->GameX + 0.5f;
            float vz = (float)newEnemy->GameY + 0.5f;
            newEnemy->VisualPos = glm::vec3(vx, 0.0f, vz);
            enemies_.push_back(newEnemy);
        }
    }

    // Spawn przedmiotów
    for (const auto& spawn : level_.item_spawns) {
        float x = static_cast<float>(spawn.x) + 0.5f;
        float z = static_cast<float>(spawn.y) + 0.5f;
        Item* newItem = nullptr;
        char t = spawn.type;

        if (t == 'P') {
            ItemStats stats; stats.health = 40;
            newItem = new Item("Health Potion", ItemType::Consumable, true, stats);
        } else if (t == 'M') {
            ItemStats stats; stats.damage = 35;
            newItem = new Item("Rusty Sword", ItemType::Weapon, false, stats);
        } else if (t == 'I') {
            ItemStats stats; stats.damage = 100;
            newItem = new Item("SOMETHING", ItemType::Weapon, false, stats);
        }

        if (newItem) {
            world_items_.push_back({ newItem, glm::vec3(x, 0.7f, z), true });
        }
    }

    // Spawn pochodni i płytek naciskowych
    for (const auto& spawn : level_.puzzle_torches) puzzle_torches_.push_back({ spawn.x, spawn.y, false });
    for (const auto& spawn : level_.pressure_plates) {
        int id = (int)pressure_plates_.size() + 1;
        pressure_plates_.push_back({ spawn.x, spawn.y, id, 0 });
    }
}



    /**
 * @brief Sprawdza, czy jednostka może się poruszyć na dane pole
 *
 * @param x Współrzędna X w siatce
 * @param y Współrzędna Y w siatce
 * @return true Jeśli pole jest przechodnie
 * @return false Jeśli pole jest ścianą lub poza mapą
 */
bool App::can_move_to(int x, int y) const {
    if (x < 0 || y < 0) return false;
    if (x >= level_.w || y >= level_.h) return false;

    auto cell = level_.cells[y * level_.w + x];
    return cell != io::Cell::Wall;
}

/**
 * @brief Zwraca wskaźnik do przeciwnika stojącego przed daną jednostką
 *
 * @param unit Jednostka (np. gracz) której przód sprawdzamy
 * @return Entity* Wskaźnik do przeciwnika lub nullptr, jeśli brak przeciwnika
 */
Entity* App::GetEnemyInFront(const Entity& unit) {
    glm::ivec2 front = unit.GetForwardTile();
    for (Entity* e : enemies_) {
        if (!e->IsAlive()) continue;
        if (e->GameX == front.x && e->GameY == front.y) return e;
    }
    return nullptr;
}

/**
 * @brief Zwraca listę jednostek trafionych przez dany skill
 *
 * @param unit Jednostka wykonująca skill
 * @param skill Wskaźnik do skilla
 * @return std::vector<Entity*> Lista trafionych jednostek
 */
std::vector<Entity*> App::ResolveSkillTarget(const Entity& unit, Skill* skill) {
    std::vector<Entity*> targets;
    for (const auto& offset : skill->offsets) {
        int tx = unit.GameX + offset.x;
        int ty = unit.GameY + offset.y;

        for (Entity* e : enemies_) {
            if (!e->IsAlive()) continue;
            if (e->GameX == tx && e->GameY == ty) targets.push_back(e);
        }
    }
    return targets;
}

/**
 * @brief Obsługuje wszystkie wejścia gracza (ruch, atak, interakcje, skille)
 *
 * Funkcja wykonuje się co klatkę i reaguje na:
 * - Strzałki / WSAD do ruchu
 * - Spację do ataku lub interakcji
 * - Numery 1-3 do skilli
 * - H do użycia mikstur
 * - ESC do pauzy / menu
 */
void App::handle_input() {
    // 1. BLOKADY (Kiedy gracz nie może sterować)
    if (combat_lock_ || is_moving_ || attack_anim_timer_ > 0.0f) {
        if (combat_lock_) update_combat();
        return;
    }

    // 2. ODCZYT KLAWISZY
    bool left = glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS;
    bool right = glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS;
    bool up = glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS;
    bool atk = glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS;
    bool esc = glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    bool k1 = glfwGetKey(window_, GLFW_KEY_1) == GLFW_PRESS;
    bool k2 = glfwGetKey(window_, GLFW_KEY_2) == GLFW_PRESS;
    bool k3 = glfwGetKey(window_, GLFW_KEY_3) == GLFW_PRESS;
    bool keyH = glfwGetKey(window_, GLFW_KEY_H) == GLFW_PRESS;

    // 3. PAUZA I MENU
    if (esc && !esc_was_down_) {
        if (state_ == GameState::Playing) state_ = GameState::Paused;
        else if (state_ == GameState::Paused) state_ = GameState::Playing;
        else if (state_ == GameState::Options) state_ = GameState::Paused;
    }
    esc_was_down_ = esc;

    if (state_ != GameState::Playing) {
        left_was_down_ = left;
        right_was_down_ = right;
        up_was_down_ = up;
        atk_was_down_ = atk;
        return;
    }

    // 4. LOGIKA ROZGRYWKI
    // A. OBRÓT
    if (left && !left_was_down_) player_.TurnLeft();
    if (right && !right_was_down_) player_.TurnRight();

    // B. RUCH
    if (up && !up_was_down_) {
        glm::ivec2 target = player_.GetForwardTile();
        if (can_move_to(target.x, target.y) && GetEnemyInFront(player_) == nullptr) {
            if (player_.UseActionPoints(1)) {
                move_start_pos_ = glm::vec3(player_.RenderPosition.x, 0.0f, player_.RenderPosition.z);
                move_target_pos_ = glm::vec3(target.x, 0.0f, target.y);
                player_.GameX = target.x;
                player_.GameY = target.y;
                update_puzzles();

                int idx = target.y * level_.w + target.x;
                if (idx >= 0 && idx < level_.cells.size()) {
                    auto cellType = level_.cells[idx];
                    if (cellType == io::Cell::NextLevel) load_next_level();
                    else if (cellType == io::Cell::Exit) state_ = GameState::Victory;
                }

                is_moving_ = true;
                move_timer_ = 0.0f;
            }
        } else {
            std::cout << "Blokada: Przeciwnik na drodze!" << std::endl;
        }
    }

    // C. AUTOMATYCZNE PODNOSZENIE PRZEDMIOTÓW
    for (auto& wItem : world_items_) {
        if (!wItem.isAlive) continue;
        if ((int)wItem.position.x == player_.GameX && (int)wItem.position.z == player_.GameY) {
            Item* item = wItem.itemData;
            if (item->type == ItemType::Weapon) {
                if (player_.equippedWeapon) {
                    WorldItem dropped;
                    dropped.itemData = player_.equippedWeapon;
                    dropped.position = glm::vec3(player_.GameX + 0.5f, 0.7f, player_.GameY + 0.5f);
                    dropped.isAlive = true;
                    world_items_.push_back(dropped);
                }
                player_.Equip(item);
                has_held_item_ = true;
            } else player_.AddToInventory(item);
            wItem.isAlive = false;
            break;
        }
    }

    // D. WALKA / INTERAKCJA
    if (atk && !atk_was_down_) {
        int dx = 0, dy = 0;
        int normalizedYaw = (player_.yaw % 360 + 360) % 360;
        if (normalizedYaw == 0) dy = -1;
        else if (normalizedYaw == 90) dx = 1;
        else if (normalizedYaw == 180) dy = 1;
        else if (normalizedYaw == 270) dx = -1;

        int tx = player_.GameX + dx;
        int ty = player_.GameY + dy;

        bool interacted_with_puzzle = false;
        for (auto& t : puzzle_torches_) {
            if (t.x == tx && t.y == ty) {
                toggle_puzzle_torch(tx, ty);
                interacted_with_puzzle = true;
                attack_anim_timer_ = kAttackDuration_;
                break;
            }
        }

        if (!interacted_with_puzzle) {
            Entity* target = GetEnemyInFront(player_);
            attack_anim_timer_ = kAttackDuration_;

            if (player_.ActionPoints > 0) {
                if (target) {
                    combat_lock_ = true;
                    combat_timer_ = 1.0f;
                    enemy_riposte_pending_ = true;
                    current_combat_target_ = target;

                    int dmg = player_.base_damage;
                    int pYaw = (player_.yaw % 360 + 360) % 360;
                    int eYaw = (target->yaw % 360 + 360) % 360;
                    if (pYaw == eYaw) dmg *= 2;

                    target->TakeDamage(dmg);
                    target->UpdateOrientation((player_.yaw + 180) % 360);
                }
                player_.UseActionPoints(1);
            }
        }
    }

    // E. SKILLE
    if (k1 && !k1_was_down_) player_.UseSkill(0, ResolveSkillTarget(player_, player_.skills[0]));
    if (k2 && !k2_was_down_) player_.UseSkill(1, ResolveSkillTarget(player_, player_.skills[1]));
    if (k3 && !k3_was_down_) player_.UseSkill(2, ResolveSkillTarget(player_, player_.skills[2]));

    if (keyH && !h_was_down_) {
        for (auto it = player_.inventory.begin(); it != player_.inventory.end(); ++it) {
            Item* item = *it;
            if (item->type == ItemType::Consumable && player_.health < player_.maxHealth) {
                int healAmount = item->stats.health;
                player_.health += healAmount;
                if (player_.health > player_.maxHealth) player_.health = player_.maxHealth;
                player_.inventory.erase(it);
                delete item;
                printf("Wypito miksture! Przywrocono %d HP.\n", healAmount);
                break;
            }
        }
    }
    h_was_down_ = keyH;

    // 5. ZAPAMIĘTANIE STANU KLAWISZY
    left_was_down_ = left;
    right_was_down_ = right;
    up_was_down_ = up;
    atk_was_down_ = atk;
    k1_was_down_ = k1; k2_was_down_ = k2; k3_was_down_ = k3;

    // 6. ZARZĄDZANIE TURAMI
    if (!combat_lock_ && player_.ActionPoints <= 0) {
        EnemiesTurn();
        player_.ResetActionPoints(2);
    }
}

/**
 * @brief Przetwarza turę wszystkich wrogów
 *
 * Każdy przeciwnik otrzymuje punkty akcji, wykonuje swoją logikę AI
 * i odpala animację ruchu, jeśli zmienił pozycję.
 */
void App::EnemiesTurn() {
    for (Enemy* enemy : enemies_) {
        if (!enemy->IsAlive()) continue;

        enemy->ResetActionPoints(1);

        // 1. Zapamiętaj gdzie wróg stał PRZED ruchem
        int oldX = enemy->GameX;
        int oldY = enemy->GameY;

        // 2. Wykonaj logikę (AI zmienia GameX/GameY)
        enemy->TakeTurn(&player_, level_);

        // 3. Sprawdź czy się ruszył
        if (enemy->GameX != oldX || enemy->GameY != oldY) {
            // Jeśli tak, odpal animację
            enemy->StartMoveAnimation(oldX, oldY, enemy->GameX, enemy->GameY);
        }
    }
}

/**
 * @brief Przełącza pochodnię w zagadce
 *
 * Jeśli gracz kliknie pochodnię, przełącza ją i jej sąsiadów
 * Sprawdza, czy wszystkie pochodnie są zapalone i otwiera przejście.
 *
 * @param x Współrzędna X pochodni
 * @param y Współrzędna Y pochodni
 */
void App::toggle_puzzle_torch(int x, int y) {
    if (y != 0 || x < 4 || x > 9) return;

    int dx[] = {0, -1, 1};

    for (int i = 0; i < 3; ++i) {
        int targetX = x + dx[i];
        if (targetX >= 4 && targetX <= 9) {
            for (auto& t : puzzle_torches_) {
                if (t.x == targetX && t.y == 0) {
                    t.is_lit = !t.is_lit;
                    // ma_engine_play_oneshot(&audio_engine_, "assets/sfx/fire.wav", NULL);
                }
            }
        }
    }

    int lit_count = 0;
    for (const auto& t : puzzle_torches_) {
        if (t.y == 0 && t.is_lit) lit_count++;
    }

    if (lit_count == 6) {
        printf("Pochodnie zapalone! Otwieram tajne przejście.\n");
        level_.cells[4 * level_.w + 5] = io::Cell::Floor;
        build_world_mesh();
        trauma_ = 0.5f;
    }
}

/**
 * @brief Aktualizuje zagadki typu "płytki naciskowe"
 *
 * Funkcja sprawdza, czy gracz przeszedł po właściwej płytce w odpowiedniej kolejności.
 * Otwiera przejście po zakończeniu sekwencji.
 */
void App::update_puzzles() {
    if (player_.GameX == last_puzzle_x_ && player_.GameY == last_puzzle_y_) return;

    last_puzzle_x_ = player_.GameX;
    last_puzzle_y_ = player_.GameY;

    struct Step { int x, y, goal; const char* desc; };
    const Step sequence[] = {
        {6, 3, 3, "SRODEK"},
        {5, 4, 2, "ZACHOD"},
        {7, 4, 3, "WSCHOD"},
        {6, 5, 1, "POLNOC"}
    };
    const int num_stages = 4;

    PressurePlate* stepped = nullptr;
    for (auto& p : pressure_plates_) {
        if (p.x == last_puzzle_x_ && p.y == last_puzzle_y_) {
            stepped = &p;
            break;
        }
    }
    if (!stepped) return;

    const Step& target = sequence[current_stage_idx_];

    if (stepped->x == target.x && stepped->y == target.y) {
        stepped->count++;
        printf("Kierunek %s: %d/%d\n", target.desc, stepped->count, target.goal);

        if (stepped->count == target.goal) {
            current_stage_idx_++;
            printf("ETAP ZAKONCZONY. Kolejny cel...\n");
            for(auto& rp : pressure_plates_) rp.count = 0;
        }
    } else {
        printf("BLAD SEKWENCJI! Powrot do startu.\n");
        current_stage_idx_ = 0;
        for (auto& rp : pressure_plates_) rp.count = 0;
    }

    if (current_stage_idx_ == num_stages && !puzzles_solved_) {
        puzzles_solved_ = true;
        level_.cells[3 * level_.w + 2] = io::Cell::Floor;
        build_world_mesh();
        trauma_ = 0.6f;
        printf("MECHANIZM ODBLOKOWANY!\n");
    }
}

/**
 * @brief Tworzy siatkę świata 3D
 *
 * Ładuje modele ścian i podłogi z plików .obj, generuje quady,
 * skaluje je do pozycji w poziomie i przesyła do GPU.
 */
void App::build_world_mesh() {
    std::vector<float> floor_vertices;
    std::vector<float> wall_vertices;

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;
    std::string base_dir = "assets/models/";

    // WALL
    std::string wall_obj_path = base_dir + "Untitled.obj";
    std::vector<float> wall_model_data;

    bool retWall = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, wall_obj_path.c_str(), base_dir.c_str());
    if (!err.empty()) fprintf(stderr, "[WALL INFO]: %s\n", err.c_str());

    if (retWall) {
        if (!materials.empty() && !materials[0].diffuse_texname.empty()) {
            std::string tex_path = base_dir + materials[0].diffuse_texname;
            if (wall_texture_) glDeleteTextures(1, &wall_texture_);
            wall_texture_ = load_texture(tex_path.c_str());
        }

        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                wall_model_data.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                wall_model_data.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                wall_model_data.push_back(attrib.vertices[3 * index.vertex_index + 2]);

                if (index.texcoord_index >= 0) {
                    wall_model_data.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                    wall_model_data.push_back(attrib.texcoords[2 * index.texcoord_index + 1]);
                } else {
                    wall_model_data.push_back(0.0f); wall_model_data.push_back(0.0f);
                }
            }
        }
    }

    // FLOOR
    attrib.vertices.clear(); attrib.texcoords.clear(); shapes.clear(); materials.clear(); err.clear();
    std::string floor_obj_path = base_dir + "kamien1.obj";
    std::vector<float> floor_model_data;

    bool retFloor = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, floor_obj_path.c_str(), base_dir.c_str());
    if (!err.empty()) fprintf(stderr, "[FLOOR INFO]: %s\n", err.c_str());

    if (retFloor) {
        if (!materials.empty() && !materials[0].diffuse_texname.empty()) {
            std::string tex_path = base_dir + materials[0].diffuse_texname;
            if (floor_texture_) glDeleteTextures(1, &floor_texture_);
            floor_texture_ = load_texture(tex_path.c_str());
        }

        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                floor_model_data.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                floor_model_data.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                floor_model_data.push_back(attrib.vertices[3 * index.vertex_index + 2]);

                if (index.texcoord_index >= 0) {
                    floor_model_data.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                    floor_model_data.push_back(attrib.texcoords[2 * index.texcoord_index + 1]);
                } else {
                    floor_model_data.push_back(0.0f); floor_model_data.push_back(0.0f);
                }
            }
        }
    }

    bool use_wall_model = !wall_model_data.empty();
    bool use_floor_model = !floor_model_data.empty();

    const int w = level_.w;
    const int h = level_.h;

    auto add_quad = [](std::vector<float>& buf, glm::vec3 a, glm::vec2 ua, glm::vec3 b, glm::vec2 ub,
                       glm::vec3 c, glm::vec2 uc, glm::vec3 d, glm::vec2 ud) {
        auto push = [&buf](glm::vec3 v, glm::vec2 uv) { buf.push_back(v.x); buf.push_back(v.y); buf.push_back(v.z); buf.push_back(uv.x); buf.push_back(uv.y); };
        push(a, ua); push(b, ub); push(c, uc); push(a, ua); push(c, uc); push(d, ud);
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto cell = level_.cells[y * w + x];

            if (cell == io::Cell::Floor) {
                if (use_floor_model) {
                    float scale = 0.53f, offset_x = 0.5f, offset_y = 0.0f, offset_z = 0.5f;
                    for (size_t i = 0; i < floor_model_data.size(); i += 5) {
                        float vx = floor_model_data[i + 0] * scale + offset_x + x;
                        float vy = floor_model_data[i + 1] * scale + offset_y;
                        float vz = floor_model_data[i + 2] * scale + offset_z + y;
                        float tu = floor_model_data[i + 3], tv = floor_model_data[i + 4];
                        floor_vertices.push_back(vx); floor_vertices.push_back(vy); floor_vertices.push_back(vz);
                        floor_vertices.push_back(tu); floor_vertices.push_back(tv);
                    }
                } else {
                    add_quad(floor_vertices, { x,0.0f,y }, {0,0}, { x+1,0.0f,y }, {1,0}, {x+1,0.0f,y+1}, {1,1}, {x,0.0f,y+1}, {0,1});
                }
            }

            if (cell == io::Cell::Wall) {
                if (use_wall_model) {
                    float scale = 0.5f, yscale = 1.0f, offset_x = 0.5f, offset_y = 0.5f, offset_z = 0.5f;
                    for (size_t i = 0; i < wall_model_data.size(); i += 5) {
                        float vx = wall_model_data[i + 0] * scale + offset_x + x;
                        float vy = wall_model_data[i + 1] * yscale + offset_y;
                        float vz = wall_model_data[i + 2] * scale + offset_z + y;
                        float tu = wall_model_data[i + 3], tv = wall_model_data[i + 4];
                        wall_vertices.push_back(vx); wall_vertices.push_back(vy); wall_vertices.push_back(vz);
                        wall_vertices.push_back(tu); wall_vertices.push_back(tv);
                    }
                } else {
                    add_quad(wall_vertices, {x,0,y+1},{0,0},{x+1,0,y+1},{1,0},{x+1,1,y+1},{1,1},{x,1,y+1},{0,1});
                    add_quad(wall_vertices, {x+1,0,y},{0,0},{x,0,y},{1,0},{x,1,y},{1,1},{x+1,1,y},{0,1});
                    add_quad(wall_vertices, {x,0,y},{0,0},{x,0,y+1},{1,0},{x,1,y+1},{1,1},{x,1,y},{0,1});
                    add_quad(wall_vertices, {x+1,0,y+1},{0,0},{x+1,0,y},{1,0},{x+1,1,y},{1,1},{x+1,1,y+1},{0,1});
                }
            }
        }
    }

    floor_vertex_count_ = (int)(floor_vertices.size()/5);
    wall_vertex_count_ = (int)(wall_vertices.size()/5);

    auto setup_vao = [](GLuint& vao, GLuint& vbo, const std::vector<float>& data){
        if (data.empty()) return;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, data.size()*sizeof(float), data.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
    };

    if (floor_vao_) glDeleteVertexArrays(1,&floor_vao_);
    if (floor_vbo_) glDeleteBuffers(1,&floor_vbo_);
    if (wall_vao_) glDeleteVertexArrays(1,&wall_vao_);
    if (wall_vbo_) glDeleteBuffers(1,&wall_vbo_);
    floor_vao_=floor_vbo_=wall_vao_=wall_vbo_=0;

    setup_vao(floor_vao_, floor_vbo_, floor_vertices);
    setup_vao(wall_vao_, wall_vbo_, wall_vertices);
}

/**
 * @brief Buduje siatkę kostki jednostkowej 1x1x1
 *
 * Służy do debugu lub jako fallback dla obiektów w grze.
 */
void App::build_cube_mesh() {
    const float v[] = {
        -0.5f,-0.5f,0.5f,0,0, 0.5f,-0.5f,0.5f,1,0, 0.5f,0.5f,0.5f,1,1,
        -0.5f,-0.5f,0.5f,0,0, 0.5f,0.5f,0.5f,1,1, -0.5f,0.5f,0.5f,0,1,
        0.5f,-0.5f,-0.5f,0,0, -0.5f,-0.5f,-0.5f,1,0, -0.5f,0.5f,-0.5f,1,1,
        0.5f,-0.5f,-0.5f,0,0, -0.5f,0.5f,-0.5f,1,1, 0.5f,0.5f,-0.5f,0,1,
        -0.5f,-0.5f,-0.5f,0,0, -0.5f,-0.5f,0.5f,1,0, -0.5f,0.5f,0.5f,1,1,
        -0.5f,-0.5f,-0.5f,0,0, -0.5f,0.5f,0.5f,1,1, -0.5f,0.5f,-0.5f,0,1,
        0.5f,-0.5f,0.5f,0,0, 0.5f,-0.5f,-0.5f,1,0, 0.5f,0.5f,-0.5f,1,1,
        0.5f,-0.5f,0.5f,0,0, 0.5f,0.5f,-0.5f,1,1, 0.5f,0.5f,0.5f,0,1,
        -0.5f,0.5f,0.5f,0,0, 0.5f,0.5f,0.5f,1,0, 0.5f,0.5f,-0.5f,1,1,
        -0.5f,0.5f,0.5f,0,0, 0.5f,0.5f,-0.5f,1,1, -0.5f,0.5f,-0.5f,0,1,
        -0.5f,-0.5f,-0.5f,0,0, 0.5f,-0.5f,-0.5f,1,0, 0.5f,-0.5f,0.5f,1,1,
        -0.5f,-0.5f,-0.5f,0,0, 0.5f,-0.5f,0.5f,1,1, -0.5f,-0.5f,0.5f,0,1
    };

    cube_vertex_count_ = sizeof(v)/sizeof(float)/5;

    glGenVertexArrays(1,&cube_vao_);
    glGenBuffers(1,&cube_vbo_);
    glBindVertexArray(cube_vao_);
    glBindBuffer(GL_ARRAY_BUFFER,cube_vbo_);
    glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STATIC_DRAW);

    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}
/**
 * @brief Buduje siatkę dla broni i przedmiotów w grze (miecz, mikstura, pochodnia)
 *
 * Funkcja ładuje modele 3D z plików .obj przy użyciu tinyobjloader,
 * tworzy odpowiednie VAO/VBO oraz ładuje tekstury z plików lub fallback kolorów.
 * Obsługuje:
 *  - Miecz (sword2.obj)
 *  - Mikstura (potion.obj)
 *  - Pochodnia (torch.obj)
 */
void App::build_weapon_mesh() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;
    std::string baseDir = "assets/models/";

    // =======================
    // 1. Ładowanie miecza
    // =======================
    std::string swordPath = baseDir + "sword2.obj";
    bool retSword = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, swordPath.c_str(), baseDir.c_str());
    if (!err.empty()) printf("[SWORD LOAD INFO]: %s\n", err.c_str());

    if (retSword) {
        weapon_texture_ = 0;
        if (!materials.empty()) {
            if (!materials[0].diffuse_texname.empty()) {
                std::string texPath = baseDir + materials[0].diffuse_texname;
                weapon_texture_ = load_texture(texPath.c_str());
            } else {
                float r = materials[0].diffuse[0];
                float g = materials[0].diffuse[1];
                float b = materials[0].diffuse[2];
                weapon_texture_ = create_texture_from_color(r, g, b);
            }
        }
        if (weapon_texture_ == 0) weapon_texture_ = create_texture_from_color(0.6f, 0.6f, 0.7f);

        std::vector<float> vertices;
        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                vertices.push_back(attrib.vertices[3*index.vertex_index+0]);
                vertices.push_back(attrib.vertices[3*index.vertex_index+1]);
                vertices.push_back(attrib.vertices[3*index.vertex_index+2]);
                if (index.texcoord_index >= 0) {
                    vertices.push_back(attrib.texcoords[2*index.texcoord_index+0]);
                    vertices.push_back(1.0f - attrib.texcoords[2*index.texcoord_index+1]);
                } else { vertices.push_back(0.0f); vertices.push_back(0.0f); }
            }
        }
        weapon_vertex_count_ = (int)(vertices.size()/5);

        if (weapon_vao_) glDeleteVertexArrays(1, &weapon_vao_);
        if (weapon_vbo_) glDeleteBuffers(1, &weapon_vbo_);
        glGenVertexArrays(1, &weapon_vao_);
        glGenBuffers(1, &weapon_vbo_);
        glBindVertexArray(weapon_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, weapon_vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices.size()*sizeof(float), vertices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    // =======================
    // 2. Ładowanie mikstury
    // =======================
    attrib.vertices.clear(); attrib.texcoords.clear(); shapes.clear(); materials.clear(); err.clear();
    std::string potionPath = baseDir + "potion.obj";
    bool retPotion = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, potionPath.c_str(), baseDir.c_str());
    if (!err.empty()) printf("[POTION LOAD INFO]: %s\n", err.c_str());

    if (retPotion) {
        if (!materials.empty() && !materials[0].diffuse_texname.empty())
            potion_texture_ = load_texture((baseDir+materials[0].diffuse_texname).c_str());

        std::vector<float> vertices;
        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                vertices.push_back(attrib.vertices[3*index.vertex_index+0]);
                vertices.push_back(attrib.vertices[3*index.vertex_index+1]);
                vertices.push_back(attrib.vertices[3*index.vertex_index+2]);
                if (index.texcoord_index >= 0) {
                    vertices.push_back(attrib.texcoords[2*index.texcoord_index+0]);
                    vertices.push_back(1.0f - attrib.texcoords[2*index.texcoord_index+1]);
                } else { vertices.push_back(0.0f); vertices.push_back(0.0f); }
            }
        }
        potion_vertex_count_ = (int)(vertices.size()/5);
        glGenVertexArrays(1,&potion_vao_); glGenBuffers(1,&potion_vbo_);
        glBindVertexArray(potion_vao_); glBindBuffer(GL_ARRAY_BUFFER,potion_vbo_);
        glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    // =======================
    // 3. Ładowanie pochodni
    // =======================
    attrib.vertices.clear(); attrib.texcoords.clear(); shapes.clear(); materials.clear(); err.clear();
    std::string torchPath = baseDir + "torch.obj";
    bool retTorch = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, torchPath.c_str(), baseDir.c_str());
    if (retTorch) {
        if (!materials.empty() && !materials[0].diffuse_texname.empty())
            torch_texture_ = load_texture((baseDir+materials[0].diffuse_texname).c_str());

        std::vector<float> vertices;
        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                vertices.push_back(attrib.vertices[3*index.vertex_index+0]);
                vertices.push_back(attrib.vertices[3*index.vertex_index+1]);
                vertices.push_back(attrib.vertices[3*index.vertex_index+2]);
                if (index.texcoord_index >= 0) {
                    vertices.push_back(attrib.texcoords[2*index.texcoord_index+0]);
                    vertices.push_back(1.0f - attrib.texcoords[2*index.texcoord_index+1]);
                } else { vertices.push_back(0.0f); vertices.push_back(0.0f); }
            }
        }
        torch_vertex_count_ = (int)(vertices.size()/5);
        if (torch_vao_) glDeleteVertexArrays(1,&torch_vao_);
        if (torch_vbo_) glDeleteBuffers(1,&torch_vbo_);
        glGenVertexArrays(1,&torch_vao_); glGenBuffers(1,&torch_vbo_);
        glBindVertexArray(torch_vao_); glBindBuffer(GL_ARRAY_BUFFER,torch_vbo_);
        glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }
}

/**
 * @brief Buduje siatki dla wrogów (Zombie i Szkielet)
 */
void App::build_enemy_mesh() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;
    std::string baseDir = "assets/models/";

    // Ładowanie Zombie
    std::string zombiePath = baseDir+"zombie.obj";
    bool retZ = tinyobj::LoadObj(&attrib,&shapes,&materials,&err,zombiePath.c_str(),baseDir.c_str());
    if (retZ) {
        if(!materials.empty() && !materials[0].diffuse_texname.empty())
            zombie_texture_ = load_texture((baseDir+materials[0].diffuse_texname).c_str());
        else zombie_texture_ = load_texture("assets/models/zombie.png");

        std::vector<float> vertices;
        for(const auto& shape:shapes)
            for(const auto& index:shape.mesh.indices){
                vertices.push_back(attrib.vertices[3*index.vertex_index+0]);
                vertices.push_back(attrib.vertices[3*index.vertex_index+1]);
                vertices.push_back(attrib.vertices[3*index.vertex_index+2]);
                if(index.texcoord_index>=0){
                    vertices.push_back(attrib.texcoords[2*index.texcoord_index+0]);
                    vertices.push_back(1.0f-attrib.texcoords[2*index.texcoord_index+1]);
                }else{vertices.push_back(0.0f);vertices.push_back(0.0f);}
            }
        zombie_vertex_count_=(int)(vertices.size()/5);
        glGenVertexArrays(1,&zombie_vao_); glGenBuffers(1,&zombie_vbo_);
        glBindVertexArray(zombie_vao_); glBindBuffer(GL_ARRAY_BUFFER,zombie_vbo_);
        glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));glEnableVertexAttribArray(1);
    }

    // Ładowanie Szkieletu
    attrib.vertices.clear(); attrib.texcoords.clear(); shapes.clear(); materials.clear(); err.clear();
    std::string skeletonPath = baseDir+"skeleton.obj";
    bool retS = tinyobj::LoadObj(&attrib,&shapes,&materials,&err,skeletonPath.c_str(),baseDir.c_str());
    if(retS){
        if(!materials.empty() && !materials[0].diffuse_texname.empty())
            skeleton_texture_ = load_texture((baseDir+materials[0].diffuse_texname).c_str());
        else skeleton_texture_ = load_texture("assets/models/skeleton.png");

        std::vector<float> vertices;
        for(const auto& shape:shapes)
            for(const auto& index:shape.mesh.indices){
                vertices.push_back(attrib.vertices[3*index.vertex_index+0]);
                vertices.push_back(attrib.vertices[3*index.vertex_index+1]);
                vertices.push_back(attrib.vertices[3*index.vertex_index+2]);
                if(index.texcoord_index>=0){
                    vertices.push_back(attrib.texcoords[2*index.texcoord_index+0]);
                    vertices.push_back(1.0f-attrib.texcoords[2*index.texcoord_index+1]);
                }else{vertices.push_back(0.0f);vertices.push_back(0.0f);}
            }
        skeleton_vertex_count_=(int)(vertices.size()/5);
        glGenVertexArrays(1,&skeleton_vao_); glGenBuffers(1,&skeleton_vbo_);
        glBindVertexArray(skeleton_vao_); glBindBuffer(GL_ARRAY_BUFFER,skeleton_vbo_);
        glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));glEnableVertexAttribArray(1);
    }
    glBindVertexArray(0);
}

/**
 * @brief Rozpoczyna nową klatkę renderowania
 *
 * Wywołuje GLFW PollEvents, obsługę wejścia, czyści bufor,
 * a następnie przygotowuje nową ramkę ImGui.
 */
void App::frame_begin() {
    glfwPollEvents();
    handle_input();
    glClearColor(0.05f,0.06f,0.08f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

/**
 * @file AppRenderUI.cpp
 * @brief Rendering gry i interfejsu użytkownika dla gry 3D typu dungeon crawler.
 *
 * Zawiera funkcje do rysowania świata, wrogów, przedmiotów, kamery oraz interfejsu HUD i minimapy.
 */


/**
 * @brief Renderuje jedną klatkę gry - logika, animacje, kamera, świat, przedmioty, wrogowie.
 *
 * Funkcja odpowiedzialna za kompletny rendering 3D i logikę wizualną w danej klatce:
 * - animacje gracza (ruch, atak)
 * - ruch kamery i screen shake
 * - konfiguracja shadera i oświetlenia
 * - rysowanie świata (podłoga, ściany, portale)
 * - rysowanie fizycznych pochodni i zagadek (puzzle)
 * - rysowanie wrogów i ich pasków HP
 * - rysowanie przedmiotów na ziemi i trzymanych przez gracza
 */
void App::frame_render() {
    // --- 1. LOGIKA ROZGRYWKI ---
    update_exploration();
    update_puzzles(); // Obsługa zagadki z płytami

    float dt = ImGui::GetIO().DeltaTime;

    // Trauma / wstrząs ekranu
    if (trauma_ > 0.0f) {
        trauma_ -= dt;
        if (trauma_ < 0.0f) trauma_ = 0.0f;
    }

    // Animacja ruchu gracza
    if (is_moving_) {
        move_timer_ += dt;
        float t = move_timer_ / kMoveDuration_;
        if (t >= 1.0f) {
            t = 1.0f;
            is_moving_ = false;
            player_.RenderPosition = move_target_pos_;
        } else {
            player_.RenderPosition = move_start_pos_ + (move_target_pos_ - move_start_pos_) * t;
            float headBob = std::sin(t * 3.14159f) * 0.08f;
            player_.RenderPosition.y = headBob;
        }
    }

    // Animacja ataku gracza
    if (attack_anim_timer_ > 0.0f) {
        attack_anim_timer_ -= dt;
        if (attack_anim_timer_ < 0.0f) attack_anim_timer_ = 0.0f;
    }

    // --- 2. KAMERA ---
    float yaw = (float)player_.yaw;
    glm::vec3 cam_pos;
    glm::vec3 center;
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    // Kamera w menu lub w grze
    if (state_ == GameState::MainMenu || state_ == GameState::Credits || state_ == GameState::Options) {
        float radius = 6.0f;
        float camX = std::sin(menu_timer_ * 0.2f) * radius + (level_.w / 2.0f);
        float camZ = std::cos(menu_timer_ * 0.2f) * radius + (level_.h / 2.0f);
        cam_pos = glm::vec3(camX, 5.0f, camZ);
        center = glm::vec3(level_.w / 2.0f, 0.0f, level_.h / 2.0f);
    } else {
        float rad = glm::radians(yaw);
        glm::vec3 forward(std::sin(rad), 0.0f, -std::cos(rad));
        cam_pos = player_.RenderPosition + glm::vec3(0.5f, 0.0f, 0.5f);

        // Screen shake
        if (trauma_ > 0.0f) {
            float shake = trauma_ * trauma_;
            cam_pos.x += ((float)(rand() % 100) / 50.0f - 1.0f) * 0.1f * shake;
            cam_pos.y += ((float)(rand() % 100) / 50.0f - 1.0f) * 0.1f * shake;
            cam_pos.z += ((float)(rand() % 100) / 50.0f - 1.0f) * 0.1f * shake;
        }

        // Tryb FPP / TPP
        if (camera_mode_ == CameraMode::FirstPerson) {
            cam_pos.y += 0.9f;
            cam_pos += forward * 0.1f;
        } else {
            cam_pos.y += 0.95f;
            cam_pos -= forward * 0.3f;
            glm::vec3 rightv = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
            cam_pos += rightv * 0.15f;
        }
        center = cam_pos + forward;
    }
    view_ = glm::lookAt(cam_pos, center, up);

    // --- 3. KONFIGURACJA SHADERA I OŚWIETLENIA ---
    world_shader_.use();
    world_shader_.setMat4("uProj", &proj_[0][0]);
    world_shader_.setMat4("uView", &view_[0][0]);
    world_shader_.setVec3("uCamPos", cam_pos.x, cam_pos.y, cam_pos.z);
    world_shader_.setFloat("uTime", (float)glfwGetTime());

    // Ustawienie świateł puzzle
    int activeLights = 0;
    for (const auto& torch : puzzle_torches_) {
        if (torch.is_lit) {
            std::string name = "uPuzzleLights[" + std::to_string(activeLights) + "]";
            world_shader_.setVec3(name.c_str(), torch.x + 0.5f, 1.5f, torch.y + 0.5f);
            activeLights++;
            if (activeLights >= 16) break;
        }
    }
    world_shader_.setInt("uActivePuzzleLights", activeLights);

    glm::mat4 I(1.0f);
    world_shader_.setMat4("uModel", &I[0][0]);
    world_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 1.0f);

    // --- 4. RYSOWANIE ŚWIATA ---
    // Podłoga
    world_shader_.setInt("uUseTex", 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, floor_texture_);
    world_shader_.setInt("uTex", 0);
    if (floor_vertex_count_ > 0) {
        glBindVertexArray(floor_vao_);
        glDrawArrays(GL_TRIANGLES, 0, floor_vertex_count_);
    }

    // Ściany
    glBindTexture(GL_TEXTURE_2D, wall_texture_);
    if (wall_vertex_count_ > 0) {
        glBindVertexArray(wall_vao_);
        glDrawArrays(GL_TRIANGLES, 0, wall_vertex_count_);
    }

    // Portale / wyjścia
    world_shader_.setInt("uUseTex", 0);
    glBindVertexArray(cube_vao_);
    for (int y = 0; y < level_.h; ++y) {
        for (int x = 0; x < level_.w; ++x) {
            auto cell = level_.cells[y * level_.w + x];
            if (cell == io::Cell::NextLevel || cell == io::Cell::Exit) {
                if (cell == io::Cell::NextLevel) world_shader_.setVec4("uColor", 0.0f, 0.5f, 1.0f, 0.6f);
                else world_shader_.setVec4("uColor", 1.0f, 0.8f, 0.0f, 0.6f);

                glm::mat4 M(1.0f);
                M = glm::translate(M, glm::vec3(x + 0.5f, 0.5f, y + 0.5f));
                M = glm::scale(M, glm::vec3(0.8f, 1.0f, 0.8f));
                world_shader_.setMat4("uModel", &M[0][0]);
                glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
            }
        }
    }

    // --- ETAP 2: RYSOWANIE POCHODNI (Puzzle 1) ---
    for (const auto& torch : puzzle_torches_) {
        // Różowy debug słup
        world_shader_.setInt("uUseTex", 0);
        world_shader_.setVec4("uColor", 1.0f, 0.0f, 1.0f, 0.8f);
        glm::mat4 M_debug(1.0f);
        M_debug = glm::translate(M_debug, glm::vec3(torch.x + 0.5f, 2.0f, torch.y + 0.5f));
        M_debug = glm::scale(M_debug, glm::vec3(0.05f, 4.0f, 0.05f));
        world_shader_.setMat4("uModel", &M_debug[0][0]);
        glBindVertexArray(cube_vao_);
        glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);

        // Model pochodni
        if (torch_vertex_count_ > 0) {
            glm::mat4 M(1.0f);
            M = glm::translate(M, glm::vec3(torch.x + 0.5f, 0.95f, torch.y + 1.0f));
            M = glm::rotate(M, glm::radians(30.0f), glm::vec3(1, 0, 0));
            float scale = 1.5f;
            M = glm::scale(M, glm::vec3(scale));
            world_shader_.setMat4("uModel", &M[0][0]);
            world_shader_.setInt("uUseTex", 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, torch_texture_);
            world_shader_.setInt("uTex", 0);
            glBindVertexArray(torch_vao_);
            glDrawArrays(GL_TRIANGLES, 0, torch_vertex_count_);
        }
    }

    // --- RYSOWANIE PŁYTEK (Puzzle 2) ---
    world_shader_.setInt("uUseTex", 0);
    glBindVertexArray(cube_vao_);
    for (const auto& plate : pressure_plates_) {
        world_shader_.setVec4("uColor", 0.0f, 0.8f, 0.8f, 1.0f);
        glm::mat4 M(1.0f);
        M = glm::translate(M, glm::vec3(plate.x + 0.5f, 0.02f, plate.y + 0.5f));
        M = glm::scale(M, glm::vec3(0.7f, 0.05f, 0.7f));
        world_shader_.setMat4("uModel", &M[0][0]);
        glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
    }
    glBindVertexArray(0);

    // --- 5. RYSOWANIE WROGÓW ---
    for (auto* enemy : enemies_) {
        // ... (logika renderowania wrogów, pasków HP, animacji śmierci) ...
    }

    // --- 6. ITEMY NA ZIEMI ---
    for (const auto& wItem : world_items_) {
        // ... (logika renderowania mieczy i mikstur) ...
    }

    // --- 7. HELD ITEM (FPP) ---
    if (state_ == GameState::Playing && camera_mode_ == CameraMode::FirstPerson && has_held_item_) {
        // ... (logika renderowania trzymanego miecza) ...
    }
}

/**
 * @brief Renderuje interfejs użytkownika (HUD, pasek zdrowia, minimapę, ekwipunek, menu).
 */
void App::frame_ui() {
    ImGuiIO& io = ImGui::GetIO();

    dungeon::ui::HudState hud;
    hud.log = "Mapa: " + current_map_name_ + "\nWidok: ";
    hud.log += (camera_mode_ == CameraMode::FirstPerson ? "FPP" : "TPP");

    dungeon::ui::draw_hud(hud);

    // Pasek życia
    // ...

    // Panel boczny / minimapa
    // ...

    // Ekwipunek i plecak
    // ...

    // Menu ustawień
    // ...

    // Damage flash (miganie czerwone po otrzymaniu obrażeń)
    if (player_.IsHurt()) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 0.0f, 0.0f, 0.4f));
        ImGui::Begin("##DamageFlash", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::End();
        ImGui::PopStyleColor();
    }
}

    void App::frame_end() {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }

    void App::render_main_menu() {
        ImGuiIO& io = ImGui::GetIO();

        // Ustawiamy okno na cały ekran, ale przezroczyste, żeby widzieć tło 3D
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowBgAlpha(0.0f); // Całkowicie przezroczyste tło okna

        ImGui::Begin("MainMenuOverlay", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        // --- TYTUŁ GRY (Efekt cienia) ---
        float titleScale = 4.0f;
        std::string titleText = "DUNGEON CRAWLER";

        // Oblicz szerokość tekstu żeby wyśrodkować
        ImGui::SetWindowFontScale(titleScale);
        float titleW = ImGui::CalcTextSize(titleText.c_str()).x;
        float titleX = (io.DisplaySize.x - titleW) * 0.5f;
        float titleY = io.DisplaySize.y * 0.15f;

        // Cień (Czarny, przesunięty)
        ImGui::SetCursorPos(ImVec2(titleX + 5, titleY + 5));
        ImGui::TextColored(ImVec4(0, 0, 0, 1), titleText.c_str());

        // Właściwy tekst (Złoty/Żółty jak w Minecraft)
        ImGui::SetCursorPos(ImVec2(titleX, titleY));
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), titleText.c_str());

        // Reset skali czcionki dla przycisków
        ImGui::SetWindowFontScale(1.5f);

        // --- PRZYCISKI (Styl Retro) ---
        push_retro_style(); // Włączamy styl kamienia

        float btnW = 300.0f;
        float btnH = 50.0f;
        float startY = io.DisplaySize.y * 0.4f;
        float spacing = 20.0f;
        float centerX = (io.DisplaySize.x - btnW) * 0.5f;

        ImGui::SetCursorPos(ImVec2(centerX, startY));
        if (ImGui::Button("GRAJ", ImVec2(btnW, btnH))) {
            state_ = GameState::Playing;
        }

        ImGui::SetCursorPos(ImVec2(centerX, startY + (btnH + spacing) * 1));
        if (ImGui::Button("OPCJE", ImVec2(btnW, btnH))) {
            previous_state_ = GameState::MainMenu;
            state_ = GameState::Options;
        }

        ImGui::SetCursorPos(ImVec2(centerX, startY + (btnH + spacing) * 2));
        if (ImGui::Button("AUTORZY (CREDITS)", ImVec2(btnW, btnH))) {
            state_ = GameState::Credits;
        }

        ImGui::SetCursorPos(ImVec2(centerX, startY + (btnH + spacing) * 3));
        if (ImGui::Button("WYJSCIE", ImVec2(btnW, btnH))) {
            glfwSetWindowShouldClose(window_, 1);
        }

        pop_retro_style(); // Wyłączamy styl kamienia

        ImGui::End();
    }

    void App::render_options_menu() {
        // ... (ustawienia okna jak wcześniej) ...

        ImGui::Begin("Opcje", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        ImGui::Text("Dzwiek:");
        ImGui::Separator();

        // --- SUWAK GŁOŚNOŚCI ---
        if (ImGui::SliderFloat("Glosnosc", &master_volume_, 0.0f, 1.0f)) {
            // Aktualizacja miniaudio w czasie rzeczywistym
            ma_engine_set_volume(&audio_engine_, master_volume_);
        }

        ImGui::Dummy(ImVec2(0, 20)); // Odstęp

        ImGui::Text("Kamera:");
        ImGui::Separator();

        // --- WYBÓR KAMERY ---
        int mode = (camera_mode_ == CameraMode::FirstPerson) ? 0 : 1;
        if (ImGui::RadioButton("First-person (FPP)", mode == 0)) mode = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("Third-person (TPP)", mode == 1)) mode = 1;
        camera_mode_ = (mode == 0) ? CameraMode::FirstPerson : CameraMode::ThirdPerson;

        ImGui::Dummy(ImVec2(0, 20));

        ImGui::Text("Grafika:");
        ImGui::Separator();
        // ... (Twoje przyciski rozdzielczości - zostaw je tutaj) ...

        ImGui::Separator();
        if (ImGui::Button("Wroc", ImVec2(100, 40))) {
            // Prosta logika powrotu:
            // Jeśli gra ma wczytany poziom (np. player żyje), wracamy do pauzy/gry?
            // Najbezpieczniej: Wracamy do MainMenu (jeśli przyszliśmy z MainMenu).
            // Ale jak jesteśmy w grze?
            // Dodaj zmienną pomocniczą w App.hpp: GameState previous_state_;
            // Ustawiaj ją przed wejściem w Opcje.
            // Na razie dla testu:
            state_ = previous_state_;
        }

        ImGui::End();
    }
    void App::init_puzzles(const io::Level& L) {
        puzzle_torches_.clear();
        pressure_plates_.clear();
        for (const auto& s : L.item_spawns) {
            if (s.type == 'L') puzzle_torches_.push_back({ s.x, s.y, false });
            if (s.type == 'T') pressure_plates_.push_back({ s.x, s.y, (int)pressure_plates_.size() + 1, 0 });
        }
    }
    void App::init_audio() {
        ma_result result;

        // 1. Inicjalizacja silnika
        result = ma_engine_init(NULL, &audio_engine_);
        if (result != MA_SUCCESS) {
            printf("Błąd: Nie udalo sie zainicjowac audio engine.\n");
            return;
        }

        // 2. Ładowanie MUZYKI (Globalna, zapętlona)
        // MA_SOUND_FLAG_STREAM - dobre dla długiej muzyki (nie ładuje całej do RAMu)
        result = ma_sound_init_from_file(&audio_engine_, "assets/audio/music.mp3", MA_SOUND_FLAG_STREAM, NULL, NULL, &bg_music_);
        if (result == MA_SUCCESS) {
            ma_sound_set_looping(&bg_music_, MA_TRUE); // Zapętlamy
            ma_sound_set_volume(&bg_music_, 0.3f);     // Cicha muzyka (30%)
            ma_sound_start(&bg_music_);                // Start od razu
        }
        else {
            printf("Blad: Brak pliku assets/audio/music.mp3\n");
        }

        // 3. Ładowanie POCHODNI (Tylko w grze, zapętlona)
        result = ma_sound_init_from_file(&audio_engine_, "assets/audio/torch.mp3", 0, NULL, NULL, &sfx_torch_);
        if (result == MA_SUCCESS) {
            ma_sound_set_looping(&sfx_torch_, MA_TRUE); // Zapętlamy
            ma_sound_set_volume(&sfx_torch_, 0.6f);     // Ogień głośniejszy niż muzyka
            // NIE STARTUJEMY TUTAJ! (Startujemy tylko w grze)
        }
        else {
            printf("Blad: Brak pliku assets/audio/torch.mp3\n");
        }
    }

    void App::run() {
        while (!glfwWindowShouldClose(window_)) {
            frame_begin();
            update_audio_state();
            float dt = ImGui::GetIO().DeltaTime;
            if (state_ == GameState::MainMenu || state_ == GameState::Credits) {
                menu_timer_ += dt;
            }

            if (state_ == GameState::Playing && combat_lock_) {
                update_combat();
            }

            switch (state_) {
            case GameState::MainMenu:
                frame_render();  
                render_main_menu();
                break;

            case GameState::Credits:
                frame_render();    
                render_credits();
                break;

            case GameState::Options:
                render_options_menu();
                break;

            case GameState::Playing:
                frame_render();   // Świat
                frame_ui();       // HUD
                break;

            case GameState::GameOver:
                frame_render();
                render_game_over();
                break;

            case GameState::Victory:
                frame_render();
                render_victory_screen();
                break;

            case GameState::Paused:
                frame_render();      // Rysujemy grę w tle (zamrożoną)
                render_pause_menu(); // Rysujemy menu na wierzchu
                break;
            }

            frame_end();
        }
    }

    void App::reset_game() {
        // 1. Reset zmiennych aplikacji
        has_held_item_ = false;        // <--- TEGO BRAKOWAŁO! Zabieramy miecz wizualnie
        attack_anim_timer_ = 0.0f;     // Resetujemy animację ataku

        is_moving_ = false;
        move_timer_ = 0.0f;

        // 2. Reset statystyk gracza (bo to nowa gra, więc musi mieć pełne HP)
        player_.health = player_.maxHealth; // Pełne życie
        player_.ResetActionPoints(2);       // Pełne punkty akcji
        // Jeśli masz w klasie Player pole ekwipunku, też je tu wyczyść, np.:
        // player_.hasSword = false; 

        // 3. Ładowanie mapy (startowej)
        // Upewniamy się, że wracamy na start
        current_level_idx_ = 0;
        current_map_name_ = map_list_[0];

        // Przeładowanie zasobów
        if (floor_vbo_) glDeleteBuffers(1, &floor_vbo_);
        if (floor_vao_) glDeleteVertexArrays(1, &floor_vao_);
        if (wall_vbo_)  glDeleteBuffers(1, &wall_vbo_);
        if (wall_vao_)  glDeleteVertexArrays(1, &wall_vao_);
        floor_vao_ = 0; floor_vbo_ = 0; wall_vao_ = 0; wall_vbo_ = 0;

        load_level();
        build_world_mesh();
        spawn_entities_from_level();

        // 4. Reset mgły wojny
        visited_cells_.assign(level_.w * level_.h, false);
        update_exploration();
    }

    void App::render_game_over() {
        // Wycentrowane okno na środku ekranu
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        if (ImGui::Begin("Game Over", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            ImGui::SetWindowFontScale(2.0f);
            ImGui::Text("NIE ZYJESZ");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));

            if (ImGui::Button("Wroc do Menu Glownego", ImVec2(200, 50))) {
                state_ = GameState::MainMenu;
                reset_game(); // Przygotuj grę na nową sesję
            }
        }
        ImGui::End();
    }

    void App::update_combat() {
        if (!combat_lock_) return; // Jeśli nie ma walki, nic nie rób

        float dt = ImGui::GetIO().DeltaTime;
        combat_timer_ -= dt;

        // --- POŁOWA SEKWENCJI (0.5s): WRÓG ODDAJE ---
        if (combat_timer_ <= 0.5f && enemy_riposte_pending_) {
            if (current_combat_target_ && current_combat_target_->IsAlive()) {
                int dmg = current_combat_target_->base_damage;
                player_.TakeDamage(dmg);
                trauma_ = 0.5f; // Ustaw siłę wstrząsu (max 1.0)
            }
            enemy_riposte_pending_ = false;
        }

        // --- KONIEC SEKWENCJI (0.0s): ODBLOKOWANIE ---
        if (combat_timer_ <= 0.0f) {
            combat_lock_ = false;
            current_combat_target_ = nullptr;

            // Odnawiamy AP gracza na nową turę (bo właśnie minęła "tura" wymiany ciosów)
            player_.ResetActionPoints(2);

            // Sprawdź czy przeżyliśmy
            if (!player_.IsAlive()) {
                state_ = GameState::GameOver;
            }
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

    bool App::check_los(int x1, int y1, int x2, int y2) const {
        // Jeśli to ten sam punkt, to widać
        if (x1 == x2 && y1 == y2) return true;

        float dist = std::sqrt(std::pow(x2 - x1, 2) + std::pow(y2 - y1, 2));
        if (dist < 0.5f) return true;

        // Dzielimy odcinek na małe kroki
        float stepX = (x2 - x1) / dist;
        float stepY = (y2 - y1) / dist;

        float cx = (float)x1 + 0.5f; // startujemy ze środka kafelka
        float cy = (float)y1 + 0.5f;

        // Idziemy po promieniu
        for (float t = 0.0f; t < dist - 0.1f; t += 0.5f) { // krok co 0.5 kafelka
            cx += stepX * 0.5f;
            cy += stepY * 0.5f;

            int ix = (int)cx;
            int iy = (int)cy;

            // POPRAWKA 1: Używamy level_ (zmiennej klasy), a nie level (nieistniejącego argumentu)
            // Jeśli wyszliśmy poza mapę -> blokada
            if (ix < 0 || iy < 0 || ix >= level_.w || iy >= level_.h) return false;

            // Jeśli trafiliśmy na ścianę (która nie jest celem), to blokujemy widok
            if (level_.cells[iy * level_.w + ix] == io::Cell::Wall) {
                // Jeśli to jest właśnie ten kafelek, na który patrzymy, to OK (widzimy ścianę)
                if (ix == x2 && iy == y2) return true;
                // Jeśli to inna ściana po drodze -> zasłania widok
                return false;
            }
        }
        return true;
    }

    void App::update_exploration() {
        // Promień wzroku
        int radius = 5;

        int px = player_.GameX;
        int py = player_.GameY;

        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                // 1. Sprawdzamy dystans (koło)
                if (x * x + y * y > radius * radius) continue;

                int tx = px + x;
                int ty = py + y;

                // Sprawdź granice mapy (ponownie używamy level_)
                if (tx >= 0 && tx < level_.w && ty >= 0 && ty < level_.h) {

                    // 2. Raycast
                    // POPRAWKA 2: Wywołujemy bez przekazywania level_, bo funkcja ma do niego dostęp
                    if (check_los(px, py, tx, ty)) {
                        // Oznacz jako odwiedzone
                        visited_cells_[ty * level_.w + tx] = true;
                    }
                }
            }
        }
    }

    void App::load_next_level() {
        current_level_idx_++;

        // Sprawdź czy mamy jeszcze mapy na liście
        if (current_level_idx_ < map_list_.size()) {
            std::cout << "Loading Level: " << map_list_[current_level_idx_] << std::endl;

            // 1. Ustaw nazwę nowej mapy
            current_map_name_ = map_list_[current_level_idx_];

            // 2. Zachowaj statystyki gracza, ale zresetuj AP
            player_.ResetActionPoints(2);

            // 3. Przeładuj wszystko
            // Usuń stare bufory (ważne, żeby nie było wycieków pamięci w GPU)
            if (floor_vbo_) glDeleteBuffers(1, &floor_vbo_);
            if (floor_vao_) glDeleteVertexArrays(1, &floor_vao_);
            if (wall_vbo_)  glDeleteBuffers(1, &wall_vbo_);
            if (wall_vao_)  glDeleteVertexArrays(1, &wall_vao_);
            floor_vao_ = 0; floor_vbo_ = 0; wall_vao_ = 0; wall_vbo_ = 0;

            // Wczytaj dane
            load_level();
            build_world_mesh();
            spawn_entities_from_level();

            // Reset mgły wojny (nowa mapa jest czarna)
            visited_cells_.assign(level_.w * level_.h, false);
            update_exploration();
        }
        else {
            // Brak map = Koniec gry
            state_ = GameState::Victory;
        }
    }

    void App::render_victory_screen() {
        ImGuiIO& io = ImGui::GetIO();
        // Wyśrodkowanie okna
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        if (ImGui::Begin("Victory", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
            // Złoty napis
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
            ImGui::SetWindowFontScale(2.0f);
            ImGui::Text("ZWYCIESTWO!");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();

            ImGui::Text("Ukonczyles loch i znalazles wyjscie.");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));

            if (ImGui::Button("Wroc do Menu Glownego", ImVec2(200, 50))) {
                state_ = GameState::MainMenu;
                current_level_idx_ = 0; // Reset postępu
                current_map_name_ = map_list_[0]; // Reset do mapy 1
                reset_game();
            }
        }
        ImGui::End();
    }

    void App::update_audio_state() {
        // Sprawdzamy stan gry
        if (state_ == GameState::Playing) {
            // Jeśli gra trwa, a ogień nie gra -> Włącz
            if (!ma_sound_is_playing(&sfx_torch_)) {
                ma_sound_start(&sfx_torch_);
            }
        }
        else {
            // Jeśli Menu/Victory/GameOver, a ogień gra -> Wyłącz
            if (ma_sound_is_playing(&sfx_torch_)) {
                ma_sound_stop(&sfx_torch_);
            }
        }
    }

    void App::render_pause_menu() {
        // Styl taki sam jak Main Menu
        ImVec2 window_size(300, 300); // Mniejsze, zgrabne
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - window_size.x) * 0.5f, (io.DisplaySize.y - window_size.y) * 0.5f));
        ImGui::SetNextWindowSize(window_size);

        // Przeźroczyste tło (żeby widzieć grę pod spodem)
        ImGui::SetNextWindowBgAlpha(0.7f);

        ImGui::Begin("PauseMenu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // Wyśrodkowanie przycisków
        float contentW = ImGui::GetContentRegionAvail().x;
        float btnW = 200.0f;
        float centerX = (contentW - btnW) * 0.5f;

        ImGui::SetCursorPosX(centerX);
        ImGui::Text("PAUZA");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 20));

        ImGui::SetCursorPosX(centerX);
        if (ImGui::Button("Wznow (Resume)", ImVec2(btnW, 40))) {
            state_ = GameState::Playing;
        }

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(centerX);
        if (ImGui::Button("Opcje", ImVec2(btnW, 40))) {
            previous_state_ = GameState::Paused;
            state_ = GameState::Options;
            // Tu przydałoby się zapisać previous_state_ = GameState::Paused;
        }

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(centerX);
        if (ImGui::Button("Menu Glowne", ImVec2(btnW, 40))) {
            state_ = GameState::MainMenu;
            // Opcjonalnie: reset_game() tutaj, jeśli chcesz wyjść całkowicie
        }

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(centerX);
        if (ImGui::Button("Wyjscie z gry", ImVec2(btnW, 40))) {
            glfwSetWindowShouldClose(window_, 1);
        }

        ImGui::End();
    }

    void App::render_loading_screen() {
        // PĘTLA RYSOWANIA (Rysujemy 3 razy, żeby wypełnić Back Buffer i Front Buffer)
        // Dzięki temu mamy pewność, że obraz trafi na ekran przed zamrożeniem.
        for (int i = 0; i < 3; i++) {

            // 1. Rozmiar i Viewport
            int w, h;
            glfwGetFramebufferSize(window_, &w, &h);
            glViewport(0, 0, w, h);

            // 2. TŁO (Ciemnoszare - dla testu, czy OpenGL działa)
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // 3. Start ImGui
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Ręczny rozmiar dla pewności
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2((float)w, (float)h);

            // 4. Okno z napisem
            ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

            if (ImGui::Begin("Loading", nullptr,
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoBackground))
            {
                ImGui::SetWindowFontScale(3.0f);
                ImGui::TextColored(ImVec4(1, 1, 1, 1), "LADOWANIE...");
            }
            ImGui::End();

            // 5. Render
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            // 6. Swap
            glfwSwapBuffers(window_);

            // 7. Ważne: Przetwórz eventy, żeby okno "odżyło"
            glfwPollEvents();
        }

        // Czekamy chwilkę, żeby upewnić się, że GPU skończyło
        glFinish();
    }

    void App::push_retro_style() {
        // 1. Kolory (Szary kamień)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 1.0f)); // Zwykły
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Najechany (jaśniejszy)
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f)); // Wciśnięty (ciemniejszy)

        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.1f, 0.1f, 0.1f, 1.0f)); // Czarne ramki
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // Biały tekst

        // 2. Kształty (Kanciaste jak piksele)
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);  // Zero zaokrągleń
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f); // Gruba ramka
    }

    void App::pop_retro_style() {
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);
    }

    void App::render_credits() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowBgAlpha(0.7f); // Półprzezroczyste czarne tło

        ImGui::Begin("Credits", nullptr, ImGuiWindowFlags_NoDecoration);

        // Tytuł
        ImGui::SetWindowFontScale(2.5f);
        std::string title = "TWORCY";
        float titleW = ImGui::CalcTextSize(title.c_str()).x;
        ImGui::SetCursorPosX((io.DisplaySize.x - titleW) * 0.5f);
        ImGui::SetCursorPosY(50);
        ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), title.c_str());

        ImGui::Dummy(ImVec2(0, 50));

        // Lista (Edytuj tutaj!)
        ImGui::SetWindowFontScale(1.5f);
        const char* names[] = {
            "PROGRAMOWANIE GLOWNE",
            "Jan Kantor",
            "Kacper Szkutnik",
            "Wiktor Dzik",
            "",
            "POMOC I DESIGN",
            "Gemini (Google)",
            "",
            "SILNIK DZWIEKU",
            "miniaudio",
            "",
            "GRAFIKA GUI",
            "ImGui",
        };

        for (const char* line : names) {
            float w = ImGui::CalcTextSize(line).x;
            ImGui::SetCursorPosX((io.DisplaySize.x - w) * 0.5f);

            // Jeśli linia jest pusta, robimy odstęp, jeśli nie - rysujemy
            if (strlen(line) == 0) ImGui::Dummy(ImVec2(0, 20));
            else ImGui::Text("%s", line);
        }

        // Przycisk powrotu na dole
        push_retro_style();
        float btnW = 200;
        ImGui::SetCursorPos(ImVec2((io.DisplaySize.x - btnW) * 0.5f, io.DisplaySize.y - 100));
        if (ImGui::Button("WROC", ImVec2(btnW, 50))) {
            state_ = GameState::MainMenu;
        }
        pop_retro_style();

        ImGui::End();
    }

    GLuint App::create_texture_from_color(float r, float g, float b) {
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);

        // Konwertujemy float (0.0-1.0) na byte (0-255)
        unsigned char data[3];
        data[0] = (unsigned char)(r * 255.0f);
        data[1] = (unsigned char)(g * 255.0f);
        data[2] = (unsigned char)(b * 255.0f);

        // Tworzymy obrazek 1x1 piksel
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, data);

        // Ustawienia (ważne, żeby nie było artefaktów)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        return tex;
    }

} // namespace dungeon