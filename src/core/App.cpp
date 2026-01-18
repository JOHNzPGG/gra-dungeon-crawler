#include "dungeon/core/App.hpp"
#include "dungeon/ui/Hud.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

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

static const char* kWorldVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoord;

out vec2 vTexCoord;
out vec3 vFragPos; // NOWE: Pozycja punktu w świecie 3D

uniform mat4 uProj;
uniform mat4 uView;
uniform mat4 uModel;

void main() {
  // Obliczamy pozycję w świecie (World Space)
  vec4 worldPos = uModel * vec4(aPos, 1.0);
  vFragPos = vec3(worldPos);

  gl_Position = uProj * uView * worldPos;
  vTexCoord = aTexCoord;
}
)";

// Fragment shader – ZMIENIONY NA WERSJĘ Z KODU DRUGIEGO (obsługa uUseTex)
static const char* kWorldFS = R"(#version 330 core
out vec4 FragColor;

in vec2 vTexCoord;
in vec3 vFragPos;

uniform sampler2D uTex; 
uniform int uUseTex;
uniform vec4 uColor;
uniform vec3 uCamPos; 
uniform float uTime; // NOWE: Czas gry do animacji

void main() {
    vec4 baseColor;

    if (uUseTex == 1) {
        baseColor = texture(uTex, vTexCoord) * uColor;
    } else {
        baseColor = uColor;
    }
    
    if(baseColor.a < 0.1) discard;

    // --- OBLICZANIE POCHODNI ---
    float dist = distance(vFragPos, uCamPos);

    // 1. Efekt Migotania (Flicker)
    // Łączymy dwa sinusy o różnych prędkościach, żeby ruch był nieregularny
    float flicker = sin(uTime * 10.0) * 0.05 + sin(uTime * 23.0) * 0.02;
    
    // Dodajemy migotanie do zasięgu światła
    float lightStart = 2.5 + flicker; 
    float lightEnd = 8.0 + flicker * 2.0;

    float lightFactor = (lightEnd - dist) / (lightEnd - lightStart);
    lightFactor = clamp(lightFactor, 0.0, 1.0);

    // 2. Kolor Światła (Ciepły Pomarańcz)
    // Zamiast białego (1.0, 1.0, 1.0) dajemy ogień
    vec3 torchColor = vec3(1.0, 0.85, 0.6); 

    // Ambient (światło otoczenia) - lekko niebieskawy dla kontrastu
    vec3 ambient = vec3(0.05, 0.05, 0.1); 

    // Łączymy światło pochodni z ambientem
    vec3 finalLight = (torchColor * lightFactor) + ambient;

    // 3. Wynik
    FragColor = vec4(baseColor.rgb * finalLight, baseColor.a);
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
        init_audio();
        render_loading_screen();
        load_level();
        build_world_mesh(); 
        build_cube_mesh();
        build_weapon_mesh();
        build_enemy_mesh();
        spawn_entities_from_level();
        // Dodaj to, żeby gracz miał czym walczyć!
        // Skill(nazwa, koszt AP, obrażenia)
        player_.LearnSkill(new Skill("Strong Hit", 1, 15));
        // Domyślny skill ma offset (0,0) czyli bije w miejscu stania? 
        // Zwykle trzeba zdefiniować "offsets" w skillu, np. pole przed graczem:
        if (!player_.skills.empty()) {
            player_.skills[0]->offsets.push_back({ 0, -1 }); // przykładowy offset "przed siebie"
        }
    }

    App::~App() {
        shutdown_imgui();

        for (auto* e : enemies_) {
            delete e;
        }
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

        ma_sound_uninit(&bg_music_);
        ma_sound_uninit(&sfx_torch_);
        ma_engine_uninit(&audio_engine_);

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
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }

    void App::shutdown_imgui() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void App::load_level() {
        namespace fs = std::filesystem;

        // Zabezpieczenie na pustą nazwę mapy
        if (current_map_name_.empty()) {
            current_map_name_ = "assets/maps/test.map";
        }

        std::string path = current_map_name_;

        // Sprawdź czy plik istnieje
        if (!fs::exists(path)) {
            // Jeśli nie ma mapy, spróbuj wczytać domyślną, żeby gra nie crashowała
            printf("Brak mapy: %s. Próba wczytania test.map\n", path.c_str());
            path = "assets/maps/test.map";
            if (!fs::exists(path)) throw std::runtime_error("BRAK ZADNYCH MAP!");
        }

        // 1. Ładowanie danych
        level_ = io::load_map_ascii(path);

        // 2. Ustawianie LOGIKI (Gdzie jestem na siatce)
        player_.GameX = level_.player_x;
        player_.GameY = level_.player_y;
        player_.yaw = level_.player_start_yaw;

        // 3. Ustawianie WIZUALIÓW (Gdzie jest kamera)
        // TO JEST KLUCZOWE: Musimy zsynchronizować RenderPosition z GameX/Y!
        player_.RenderPosition = glm::vec3(level_.player_x, 0.0f, level_.player_y);

        // 4. Resetowanie zmiennych animacji
        // Musimy powiedzieć grze: "Nie ruszamy się, stoimy w miejscu startu"
        is_moving_ = false;
        move_timer_ = 0.0f;
        move_start_pos_ = player_.RenderPosition;
        move_target_pos_ = player_.RenderPosition;

        // 5. Inicjalizacja mgły wojny (Poprawka crasha)
        visited_cells_.assign(level_.w * level_.h, false);
        update_exploration(); // Odkryj startową pozycję
    }

    void App::spawn_entities_from_level() {
        for (auto* e : enemies_) delete e;
        enemies_.clear();
        world_items_.clear();

        // 1. WROGOWIE
        for (const auto& spawn : level_.enemy_spawns) {
            Enemy* newEnemy = nullptr;

            if (newEnemy) {
                newEnemy->yaw = 0.0f;

                // --- NOWE: Inicjalizacja wizualna ---
                // Ustawiamy VisualPos tam gdzie stoi logicznie
                float vx = (float)newEnemy->GameX + 0.5f;
                float vz = (float)newEnemy->GameY + 0.5f;
                newEnemy->VisualPos = glm::vec3(vx, 0.0f, vz);
                // ------------------------------------

                enemies_.push_back(newEnemy);
            }

            if (spawn.type == 'Z') {
                newEnemy = new Enemy(spawn.x, spawn.y, 180, 150, 150, 1, 45, "Zombie");
            }
            else {
                newEnemy = new Enemy(spawn.x, spawn.y, 180, 80, 80, 2, 35, "Skeleton");
            }

            if (newEnemy) {
                // USUNĄŁEM IFY SPRAWDZAJĄCE ŚCIANY. 
                // Ustawiamy sztywno 0 (lub 180, zależnie jak wolisz).
                // Jeśli model "patrzy" tyłem, zmień to 0 na 180.
                newEnemy->yaw = 0.0f;

                enemies_.push_back(newEnemy);
            }
        }

        // 2. ITEMY (Bez zmian)
        int counter = 0;
        for (const auto& p : level_.item_spawns) {
            float x = static_cast<float>(p.x) + 0.5f;
            float z = static_cast<float>(p.y) + 0.5f;

            Item* newItem = nullptr;
            if (counter == 0) {
                ItemStats stats; stats.damage = 100;
                newItem = new Item("Rusty Sword", ItemType::Weapon, false, stats);
            }
            else {
                ItemStats stats; stats.health = 50;
                newItem = new Item("Health Potion", ItemType::Consumable, true, stats);
            }

            if (newItem) {
                world_items_.push_back({ newItem, glm::vec3(x, 0.7f, z), true });
            }
            counter++;
        }
    }

    bool App::can_move_to(int x, int y) const {
        if (x < 0 || y < 0) return false;
        if (x >= level_.w || y >= level_.h) return false;

        auto cell = level_.cells[y * level_.w + x];
        return cell != io::Cell::Wall;
    }

    // Zwraca wskaźnik do przeciwnika stojącego przed daną jednostką
    Entity* App::GetEnemyInFront(const Entity& unit) {
        glm::ivec2 front = unit.GetForwardTile();
        for (Entity* e : enemies_) {
            if (!e->IsAlive()) continue;
            if (e->GameX == front.x && e->GameY == front.y) return e;
        }
        return nullptr;
    }

    // Zwraca wektor jednostek trafionych przez dany skill
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

    void App::handle_input() {
        // ------------------------------------------------------------
        // 1. BLOKADY (Kiedy gracz nie może sterować)
        // ------------------------------------------------------------

        // Jeśli trwa animacja walki lub ruchu - czekamy
        if (combat_lock_ || is_moving_ || attack_anim_timer_ > 0.0f) {
            if (combat_lock_) update_combat(); // Upewnij się, że walka się aktualizuje
            return;
        }

        // ------------------------------------------------------------
        // 2. ODCZYT KLAWISZY (Input Polling)
        // ------------------------------------------------------------

        // Ruch (Strzałki + WSAD)
        bool left = glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS;
        bool right = glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS;
        bool up = glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS;

        // Akcje
        bool atk = glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS;
        bool esc = glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS;

        // Skille (1, 2, 3)
        bool k1 = glfwGetKey(window_, GLFW_KEY_1) == GLFW_PRESS;
        bool k2 = glfwGetKey(window_, GLFW_KEY_2) == GLFW_PRESS;
        bool k3 = glfwGetKey(window_, GLFW_KEY_3) == GLFW_PRESS;

        // ------------------------------------------------------------
        // 3. PAUZA I MENU (ESC)
        // ------------------------------------------------------------

        // Obsługa przełączania pauzy
        if (esc && !esc_was_down_) { // Pamiętaj zmienić m_was_down_ na esc_was_down_ w .hpp!
            if (state_ == GameState::Playing) {
                state_ = GameState::Paused;
            }
            else if (state_ == GameState::Paused) {
                state_ = GameState::Playing;
            }
            else if (state_ == GameState::Options) {
                // Z opcji wracamy do pauzy (jeśli byliśmy w grze)
                state_ = GameState::Paused;
            }
        }
        esc_was_down_ = esc;

        // Jeśli gra jest zapauzowana lub jesteśmy w menu - ignorujemy resztę sterowania
        if (state_ != GameState::Playing) {
            // Resetujemy flagi "was_down" dla ruchu, żeby postać nie ruszyła po odpałzowaniu
            left_was_down_ = left;
            right_was_down_ = right;
            up_was_down_ = up;
            atk_was_down_ = atk;
            return;
        }

        // ------------------------------------------------------------
        // 4. LOGIKA ROZGRYWKI (Tylko gdy GameState::Playing)
        // ------------------------------------------------------------

        // A. OBRÓT
        if (left && !left_was_down_) player_.TurnLeft();
        if (right && !right_was_down_) player_.TurnRight();

        // B. RUCH
        if (up && !up_was_down_) {
            glm::ivec2 target = player_.GetForwardTile();

            // Sprawdź czy pole jest wolne (ściana + wróg)
            if (can_move_to(target.x, target.y)) {
                if (GetEnemyInFront(player_) == nullptr) {

                    // Próba ruchu (kosztuje 1 AP)
                    if (player_.UseActionPoints(1)) {
                        // Animacja: Start
                        move_start_pos_ = glm::vec3(player_.RenderPosition.x, 0.0f, player_.RenderPosition.z);
                        move_target_pos_ = glm::vec3(target.x, 0.0f, target.y);

                        // Logika: Aktualizacja pozycji
                        player_.GameX = target.x;
                        player_.GameY = target.y;

                        // Sprawdź pola specjalne (Wyjście / Next Level)
                        int idx = target.y * level_.w + target.x;
                        if (idx >= 0 && idx < level_.cells.size()) {
                            auto cellType = level_.cells[idx];
                            if (cellType == io::Cell::NextLevel) load_next_level();
                            else if (cellType == io::Cell::Exit) state_ = GameState::Victory;
                        }

                        // Uruchomienie timera animacji
                        is_moving_ = true;
                        move_timer_ = 0.0f;
                    }
                }
                else {
                    // Opcjonalnie: Dźwięk błędu / komunikat "Wróg blokuje drogę"
                    std::cout << "Blokada: Przeciwnik na drodze!" << std::endl;
                }
            }
        }

        // C. AUTOMATYCZNE PODNOSZENIE PRZEDMIOTÓW
        // Sprawdzamy, czy stoimy na czymś po ruchu
        for (auto& wItem : world_items_) {
            if (!wItem.isAlive) continue;

            // Sprawdź kolizję (proste porównanie intów)
            if ((int)wItem.position.x == player_.GameX && (int)wItem.position.z == player_.GameY) {
                Item* item = wItem.itemData;

                if (item->type == ItemType::Weapon) {
                    // Podmiana broni (wyrzuć starą, weź nową)
                    if (player_.equippedWeapon) {
                        WorldItem dropped;
                        dropped.itemData = player_.equippedWeapon;
                        dropped.position = glm::vec3(player_.GameX + 0.5f, 0.7f, player_.GameY + 0.5f);
                        dropped.isAlive = true;
                        world_items_.push_back(dropped);
                    }
                    player_.Equip(item);
                    has_held_item_ = true;
                }
                else {
                    // Mikstura itp.
                    player_.AddToInventory(item);
                }

                // Oznacz jako zebrany
                wItem.isAlive = false;

                // Opcjonalnie: Dźwięk podniesienia
                // ma_sound_start(&sfx_pickup_); 
                break;
            }
        }

        // D. WALKA (ATAK PODSTAWOWY)
        if (atk && !atk_was_down_) {
            Entity* target = GetEnemyInFront(player_);

            // Animacja startuje zawsze, nawet jak nie trafisz (machnięcie w powietrze)
            attack_anim_timer_ = kAttackDuration_;

            // Jeśli jest cel i mamy AP
            if (player_.ActionPoints > 0) {
                if (target) {
                    // Inicjacja sekwencji walki
                    combat_lock_ = true;
                    combat_timer_ = 1.0f;
                    enemy_riposte_pending_ = true;
                    current_combat_target_ = target;

                    // Obliczanie obrażeń
                    int dmg = player_.base_damage;

                    // Backstab bonus
                    int pYaw = (player_.yaw % 360 + 360) % 360;
                    int eYaw = (target->yaw % 360 + 360) % 360;
                    if (pYaw == eYaw) dmg *= 2;

                    // Zadaj obrażenia i obróć wroga
                    target->TakeDamage(dmg);
                    target->UpdateOrientation((player_.yaw + 180) % 360);

                    // Dźwięk uderzenia
                    // ma_sound_start(&sfx_hit_);
                }
                // Koszt ataku
                player_.UseActionPoints(1);
            }
        }

        // E. SKILLE (1, 2, 3)
        if (k1 && !k1_was_down_) player_.UseSkill(0, ResolveSkillTarget(player_, player_.skills[0]));
        if (k2 && !k2_was_down_) player_.UseSkill(1, ResolveSkillTarget(player_, player_.skills[1]));
        if (k3 && !k3_was_down_) player_.UseSkill(2, ResolveSkillTarget(player_, player_.skills[2]));

        // ------------------------------------------------------------
        // 5. ZAPAMIĘTANIE STANU KLAWISZY (Na następną klatkę)
        // ------------------------------------------------------------
        left_was_down_ = left;
        right_was_down_ = right;
        up_was_down_ = up;
        atk_was_down_ = atk;
        k1_was_down_ = k1; k2_was_down_ = k2; k3_was_down_ = k3;
        // esc_was_down_ jest aktualizowane wyżej, przy obsłudze pauzy

        // ------------------------------------------------------------
        // 6. ZARZĄDZANIE TURAMI
        // ------------------------------------------------------------
        // Jeśli gracz zużył wszystkie punkty akcji -> Tura wrogów
        if (!combat_lock_ && player_.ActionPoints <= 0) {
            EnemiesTurn();
            player_.ResetActionPoints(2);
        }
    }

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
                    add_quad(floor_vertices,
                        { x + 1, 1.5f, y }, { 1.0f, 0.0f }, // Prawy-Góra
                        { x,     1.5f, y }, { 0.0f, 0.0f }, // Lewy-Góra
                        { x,     1.5f, y + 1 }, { 0.0f, 1.0f }, // Lewy-Dół
                        { x + 1, 1.5f, y + 1 }, { 1.0f, 1.0f }  // Prawy-Dół
                    );
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

    void App::build_weapon_mesh() {
        // 1. Ustawienia
        std::string modelPath = "assets/models/sword3obj.obj"; // Upewnij się, że nazwa pliku .obj jest poprawna!
        std::string baseDir = "assets/models/";

        // Używamy Speculara jako koloru, żeby miecz był srebrno-złoty
        std::string textureFilename = "Excalibur_specularGlossiness.png";

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string err;

        // 2. Ładowanie modelu
        bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, modelPath.c_str(), baseDir.c_str());

        if (!err.empty()) printf("[WEAPON LOAD INFO]: %s\n", err.c_str());
        if (!ret) return;

        // 3. RĘCZNE ŁADOWANIE TEKSTURY (Tu używamy Twojej zmiennej)
        std::string texPath = baseDir + textureFilename;
        weapon_texture_ = load_texture(texPath.c_str());

        if (weapon_texture_ == 0) {
            printf("UWAGA: Nie udalo sie zaladowac tekstury miecza: %s\n", texPath.c_str());
        }

        // 4. Przetwarzanie wierzchołków
        std::vector<float> vertices;
        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                // Pozycja
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                vertices.push_back(attrib.vertices[3 * index.vertex_index + 2]);

                // UV
                if (index.texcoord_index >= 0) {
                    vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);

                    // TU MOŻE BYĆ POTRZEBNA KOREKTA (1.0f - ...)
                    // Jeśli tekstura wygląda dziwnie, usuń "1.0f - "
                    vertices.push_back(1.0f - attrib.texcoords[2 * index.texcoord_index + 1]);
                }
                else {
                    vertices.push_back(0.0f); vertices.push_back(0.0f);
                }
            }
        }

        weapon_vertex_count_ = (int)(vertices.size() / 5);

        // 5. Przesyłanie do GPU (Standard)
        glGenVertexArrays(1, &weapon_vao_);
        glGenBuffers(1, &weapon_vbo_);
        glBindVertexArray(weapon_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, weapon_vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    void App::build_enemy_mesh() {
        // Zmienne pomocnicze (użyjemy ich dwukrotnie)
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string err;
        std::string baseDir = "assets/models/";

        // ==========================================
        // 1. ŁADOWANIE ZOMBIE
        // ==========================================
        std::string zombiePath = baseDir + "zombie.obj";
        bool retZ = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, zombiePath.c_str(), baseDir.c_str());

        if (retZ) {
            // Tekstura Zombie
            if (!materials.empty() && !materials[0].diffuse_texname.empty()) {
                zombie_texture_ = load_texture((baseDir + materials[0].diffuse_texname).c_str());
            }
            else {
                zombie_texture_ = load_texture("assets/models/zombie.png"); // Fallback
            }

            // Dane wierzchołków
            std::vector<float> vertices;
            for (const auto& shape : shapes) {
                for (const auto& index : shape.mesh.indices) {
                    vertices.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                    vertices.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                    vertices.push_back(attrib.vertices[3 * index.vertex_index + 2]);

                    if (index.texcoord_index >= 0) {
                        vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                        vertices.push_back(1.0f - attrib.texcoords[2 * index.texcoord_index + 1]);
                    }
                    else {
                        vertices.push_back(0.0f); vertices.push_back(0.0f);
                    }
                }
            }
            zombie_vertex_count_ = (int)(vertices.size() / 5);

            // Bufory Zombie
            glGenVertexArrays(1, &zombie_vao_);
            glGenBuffers(1, &zombie_vbo_);
            glBindVertexArray(zombie_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, zombie_vbo_);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        }

        // ==========================================
        // 2. ŁADOWANIE SZKIELETA (Resetujemy zmienne tinyobj)
        // ==========================================
        attrib.vertices.clear(); attrib.texcoords.clear(); shapes.clear(); materials.clear(); err.clear();

        std::string skeletonPath = baseDir + "skeleton.obj"; // UPEWNIJ SIĘ ŻE MASZ TEN PLIK!
        bool retS = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, skeletonPath.c_str(), baseDir.c_str());

        if (retS) {
            // Tekstura Szkieleta
            if (!materials.empty() && !materials[0].diffuse_texname.empty()) {
                skeleton_texture_ = load_texture((baseDir + materials[0].diffuse_texname).c_str());
            }
            else {
                skeleton_texture_ = load_texture("assets/models/skeleton.png"); // Fallback
            }

            // Dane wierzchołków
            std::vector<float> vertices;
            for (const auto& shape : shapes) {
                for (const auto& index : shape.mesh.indices) {
                    vertices.push_back(attrib.vertices[3 * index.vertex_index + 0]);
                    vertices.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                    vertices.push_back(attrib.vertices[3 * index.vertex_index + 2]);

                    if (index.texcoord_index >= 0) {
                        vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                        vertices.push_back(1.0f - attrib.texcoords[2 * index.texcoord_index + 1]);
                    }
                    else {
                        vertices.push_back(0.0f); vertices.push_back(0.0f);
                    }
                }
            }
            skeleton_vertex_count_ = (int)(vertices.size() / 5);

            // Bufory Szkieleta
            glGenVertexArrays(1, &skeleton_vao_);
            glGenBuffers(1, &skeleton_vbo_);
            glBindVertexArray(skeleton_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, skeleton_vbo_);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        }

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
        update_exploration();

        float dt = ImGui::GetIO().DeltaTime;

        // --- UPDATE LOGIKI (Wstrząsy i Animacje) ---
        if (trauma_ > 0.0f) {
            trauma_ -= dt;
            if (trauma_ < 0.0f) trauma_ = 0.0f;
        }

        float yaw = (float)player_.yaw;

        // 1. LOGIKA ANIMACJI RUCHU (LERP + HEADBOB)
        if (is_moving_) {
            move_timer_ += dt;
            float t = move_timer_ / kMoveDuration_;

            if (t >= 1.0f) {
                t = 1.0f;
                is_moving_ = false;
                player_.RenderPosition = move_target_pos_;
            }
            else {
                player_.RenderPosition = move_start_pos_ + (move_target_pos_ - move_start_pos_) * t;
                // Head Bob
                float headBob = std::sin(t * 3.14159f) * 0.08f;
                player_.RenderPosition.y = headBob;
            }
        }

        // 2. LOGIKA ANIMACJI ATAKU
        if (attack_anim_timer_ > 0.0f) {
            attack_anim_timer_ -= dt;
            if (attack_anim_timer_ < 0.0f) attack_anim_timer_ = 0.0f;
        }

        // --- 3. USTAWIENIE KAMERY ---
        glm::vec3 cam_pos;
        glm::vec3 center;
        glm::vec3 up(0.0f, 1.0f, 0.0f);

        // Sprawdzamy czy jesteśmy w menu (wtedy kamera orbituje)
        if (state_ == GameState::MainMenu || state_ == GameState::Credits || state_ == GameState::Options) {
            float radius = 6.0f; // Trochę dalej
            float camX = std::sin(menu_timer_ * 0.2f) * radius + (level_.w / 2.0f);
            float camZ = std::cos(menu_timer_ * 0.2f) * radius + (level_.h / 2.0f);

            cam_pos = glm::vec3(camX, 5.0f, camZ); // Wysoko
            center = glm::vec3(level_.w / 2.0f, 0.0f, level_.h / 2.0f);
        }
        else {
            // TRYB GRY (FPP / TPP)

            // Obliczamy forward na bazie yaw gracza
            float rad = glm::radians(yaw);
            glm::vec3 forward(std::sin(rad), 0.0f, -std::cos(rad));

            cam_pos = player_.RenderPosition;
            cam_pos += glm::vec3(0.5f, 0.0f, 0.5f); // Centrowanie

            // Screen Shake (Trzęsienie ziemi)
            if (trauma_ > 0.0f) {
                float shake = trauma_ * trauma_;
                cam_pos.x += ((float)(rand() % 100) / 50.0f - 1.0f) * 0.1f * shake;
                cam_pos.y += ((float)(rand() % 100) / 50.0f - 1.0f) * 0.1f * shake;
                cam_pos.z += ((float)(rand() % 100) / 50.0f - 1.0f) * 0.1f * shake;
            }

            if (camera_mode_ == CameraMode::FirstPerson) {
                cam_pos.y += 0.9f;
                cam_pos += forward * 0.1f;
            }
            else {
                // TPP - Znad ramienia
                cam_pos.y += 0.95f;
                cam_pos -= forward * 0.3f;
                glm::vec3 rightv = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
                cam_pos += rightv * 0.15f;
            }
            center = cam_pos + forward;
        }

        view_ = glm::lookAt(cam_pos, center, up);

        // --- USTAWIENIA SHADERA ---
        world_shader_.use();
        // Tutaj używam nazw z Twojego kodu (uProj, uView, uCamPos)
        world_shader_.setMat4("uProj", &proj_[0][0]); // Upewnij się czy masz proj_ czy projection_ w App.hpp
        world_shader_.setMat4("uView", &view_[0][0]);
        world_shader_.setVec3("uCamPos", cam_pos.x, cam_pos.y, cam_pos.z);
        world_shader_.setFloat("uTime", (float)glfwGetTime());

        // Światło (Pochodnia)
        world_shader_.setVec3("uLightPos", cam_pos.x, cam_pos.y, cam_pos.z);

        glm::mat4 I(1.0f);
        world_shader_.setMat4("uModel", &I[0][0]);
        world_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 1.0f);

        // --- RYSOWANIE ŚWIATA ---

        // 1. PODŁOGA
        world_shader_.setInt("uUseTex", 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, floor_texture_);
        world_shader_.setInt("uTex", 0);
        if (floor_vertex_count_ > 0) {
            glBindVertexArray(floor_vao_);
            glDrawArrays(GL_TRIANGLES, 0, floor_vertex_count_);
        }

        // 2. ŚCIANY
        glBindTexture(GL_TEXTURE_2D, wall_texture_);
        if (wall_vertex_count_ > 0) {
            glBindVertexArray(wall_vao_);
            glDrawArrays(GL_TRIANGLES, 0, wall_vertex_count_);
        }

        // 3. PORTALE / WYJŚCIA
        world_shader_.setInt("uUseTex", 0);
        glBindVertexArray(cube_vao_);
        for (int y = 0; y < level_.h; ++y) {
            for (int x = 0; x < level_.w; ++x) {
                auto cell = level_.cells[y * level_.w + x];
                if (cell == io::Cell::NextLevel || cell == io::Cell::Exit) {
                    if (cell == io::Cell::NextLevel)
                        world_shader_.setVec4("uColor", 0.0f, 0.5f, 1.0f, 0.6f);
                    else
                        world_shader_.setVec4("uColor", 1.0f, 0.8f, 0.0f, 0.6f);

                    glm::mat4 M(1.0f);
                    M = glm::translate(M, glm::vec3(x + 0.5f, 0.5f, y + 0.5f));
                    M = glm::scale(M, glm::vec3(0.8f, 1.0f, 0.8f));
                    world_shader_.setMat4("uModel", &M[0][0]);
                    glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
                }
            }
        }
        glBindVertexArray(0);

        // --- RYSOWANIE WROGÓW ---
        // --- RYSOWANIE WROGÓW ---
        for (auto* enemy : enemies_) {
            enemy->UpdateDeath(dt);
            enemy->UpdateAnimation(dt);

            if (!enemy->IsAlive() && enemy->deathAnimFinished) continue;

            world_shader_.use();
            world_shader_.setInt("uUseTex", 1);
            glActiveTexture(GL_TEXTURE0);

            // --- WYBÓR MODELU ---
            GLuint currentVAO = 0;
            int currentCount = 0;
            float scale = 1.0f;

            if (enemy->name == "Zombie") {
                glBindTexture(GL_TEXTURE_2D, zombie_texture_);
                currentVAO = zombie_vao_;
                currentCount = zombie_vertex_count_;
                scale = 0.40f; // Skala dla Zombie
            }
            else if (enemy->name == "Skeleton") {
                glBindTexture(GL_TEXTURE_2D, skeleton_texture_);
                currentVAO = skeleton_vao_;
                currentCount = skeleton_vertex_count_;
                scale = 0.14f; // Skala dla Szkieleta (może być inna!)
            }
            else {
                // Fallback (np. Zombie)
                glBindTexture(GL_TEXTURE_2D, zombie_texture_);
                currentVAO = zombie_vao_;
                currentCount = zombie_vertex_count_;
                scale = 0.3f;
            }

            world_shader_.setInt("uTex", 0);

            float y_offset = 0.55f;

            // Kolorowanie (Damage / Death / Normal)
            if (!enemy->IsAlive()) {
                float alpha = 1.0f - enemy->deathTimer;
                if (alpha < 0.0f) alpha = 0.0f;
                y_offset -= (enemy->deathTimer * 0.5f);
                world_shader_.setVec4("uColor", 0.5f, 0.5f, 0.5f, alpha);
            }
            else if (enemy->IsHurt()) {
                world_shader_.setVec4("uColor", 2.0f, 2.0f, 2.0f, 1.0f); // Błysk
            }
            else {
                // Normalny kolor (biały, żeby nie zmieniać barwy tekstury)
                world_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 1.0f);
            }

            // Pozycja i Obrót
            float x = enemy->VisualPos.x;
            float z = enemy->VisualPos.z;

            glm::mat4 M(1.0f);
            M = glm::translate(M, glm::vec3(x, y_offset, z));
            M = glm::rotate(M, glm::radians((float)enemy->yaw), glm::vec3(0, 1, 0));

            //if (enemy->name == "Zombie") {
            //    M = glm::rotate(M, glm::radians(180.0f), glm::vec3(0, 1, 0));
            //}

            if (!enemy->IsAlive()) {
                float deathAngle = -90.0f * enemy->deathTimer;
                if (deathAngle < -90.0f) deathAngle = -90.0f;
                M = glm::rotate(M, glm::radians(deathAngle), glm::vec3(1, 0, 0));
            }

            M = glm::scale(M, glm::vec3(scale));
            world_shader_.setMat4("uModel", &M[0][0]);

            // Faktyczne Rysowanie
            if (currentCount > 0) {
                glBindVertexArray(currentVAO);
                glDrawArrays(GL_TRIANGLES, 0, currentCount);
            }
            else {
                // Fallback do kostki jeśli model się nie załadował
                glBindVertexArray(cube_vao_);
                world_shader_.setInt("uUseTex", 0);
                glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
            }

            // Pasek HP (bez zmian, kopiuj-wklej ze starego kodu jeśli zniknął, albo zostaw ten poniżej)
            if (enemy->IsAlive() && enemy->health < enemy->maxHealth) {
                glBindVertexArray(cube_vao_);
                world_shader_.setInt("uUseTex", 0);
                // Tło
                world_shader_.setVec4("uColor", 0.5f, 0.0f, 0.0f, 1.0f);
                glm::mat4 M_bg(1.0f);
                M_bg = glm::translate(M_bg, glm::vec3(x, 1.8f, z));
                M_bg = glm::scale(M_bg, glm::vec3(0.6f, 0.05f, 0.05f));
                world_shader_.setMat4("uModel", &M_bg[0][0]);
                glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
                // HP
                float hpPercent = (float)enemy->health / (float)enemy->maxHealth;
                world_shader_.setVec4("uColor", 0.0f, 1.0f, 0.0f, 1.0f);
                glm::mat4 M_hp(1.0f);
                float offset = (1.0f - hpPercent) * 0.3f;
                M_hp = glm::translate(M_hp, glm::vec3(x - offset, 1.8f, z + 0.01f));
                M_hp = glm::scale(M_hp, glm::vec3(0.6f * hpPercent, 0.05f, 0.05f));
                world_shader_.setMat4("uModel", &M_hp[0][0]);
                glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
            }
        }
        glBindVertexArray(0);

        // --- ITEMY NA ZIEMI ---
        world_shader_.setInt("uUseTex", 0);
        glBindVertexArray(cube_vao_);
        for (const auto& wItem : world_items_) {
            if (!wItem.isAlive) continue;
            if (wItem.itemData->type == ItemType::Weapon) world_shader_.setVec4("uColor", 0.0f, 0.5f, 0.5f, 1.0f);
            else world_shader_.setVec4("uColor", 0.2f, 0.2f, 1.0f, 1.0f);

            glm::mat4 M(1.0f);
            M = glm::translate(M, wItem.position);
            M = glm::scale(M, glm::vec3(0.25f));
            world_shader_.setMat4("uModel", &M[0][0]);
            glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
        }
        glBindVertexArray(0);

        // --- HELD ITEM (TYLKO W FPP I PODCZAS GRY) ---
        // Naprawiona linijka if oraz obliczanie forward
        if (state_ == GameState::Playing && camera_mode_ == CameraMode::FirstPerson && has_held_item_) {

            // Musimy obliczyć forward tutaj, bo wewnątrz bloku if(MainMenu) go nie ma
            float rad = glm::radians((float)player_.yaw);
            glm::vec3 forward(std::sin(rad), 0.0f, -std::cos(rad));
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            glm::vec3 rightv = glm::normalize(glm::cross(forward, up));

            // Pozycjonowanie
            glm::vec3 item_pos = cam_pos + forward * 0.4f + rightv * 0.2f + up * -0.3f;

            // Animacje
            float animOffset = 0.0f;
            float animTilt = 0.0f;

            if (attack_anim_timer_ > 0.0f) {
                float progress = 1.0f - (attack_anim_timer_ / kAttackDuration_);
                float wave = std::sin(progress * 3.14159f);
                animOffset = wave * 0.5f;
                animTilt = wave * 45.0f;
            }
            else {
                float time = (float)glfwGetTime();
                float breath = std::sin(time * 2.0f) * 0.02f;
                item_pos.y += breath;
            }

            item_pos += forward * animOffset;
            item_pos += up * (-animOffset * 0.2f);

            // Renderowanie
            world_shader_.use();
            if (weapon_texture_ != 0) {
                world_shader_.setInt("uUseTex", 1);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, weapon_texture_);
                world_shader_.setInt("uTex", 0);
            }
            else {
                world_shader_.setInt("uUseTex", 0);
                world_shader_.setVec4("uColor", 0.7f, 0.7f, 0.7f, 1.0f);
            }

            glm::mat4 M(1.0f);
            M = glm::translate(M, item_pos);
            // Obracamy miecz tak, by pasował do gracza
            M = glm::rotate(M, glm::radians((float)player_.yaw), glm::vec3(0, 1, 0));
            M = glm::rotate(M, glm::radians(180.0f), glm::vec3(0, 1, 0)); // Korekta modelu
            M = glm::rotate(M, glm::radians(animTilt), glm::vec3(1, 0, 0)); // Atak

            float scale = 0.0015f; // Dopasuj skalę!
            M = glm::scale(M, glm::vec3(scale));

            world_shader_.setMat4("uModel", &M[0][0]);

            if (weapon_vertex_count_ > 0) {
                glBindVertexArray(weapon_vao_);
                glDrawArrays(GL_TRIANGLES, 0, weapon_vertex_count_);
                glBindVertexArray(0);
            }

            // Reset modelu
            glm::mat4 I(1.0f);
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

        // --- NOWY PANEL (Prawy Górny Róg) ---

        ImGuiIO& io = ImGui::GetIO();
        float panelWidth = 200.0f;
        float panelHeight = 400.0f;
        float padding = 10.0f;

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panelWidth - padding, padding));
        ImGui::SetNextWindowSize(ImVec2(panelWidth, 0));

        ImGui::Begin("SidePanel", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse);

        // A. MINIMAPA (Fog of War)
        ImGui::Text("Minimap");

        // --- POCZĄTEK RYSOWANIA MINIMAPY ---
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos(); // Lewy górny róg

        float mapSize = 180.0f;      // Rozmiar okienka
        int   viewRange = 5;         // Zasięg widoku (5 kratek w każdą stronę)
        float tileSize = mapSize / (float)(viewRange * 2 + 1);

        // 1. Tło minimapy (Czarne - obszar nieodkryty)
        drawList->AddRectFilled(p, ImVec2(p.x + mapSize, p.y + mapSize), IM_COL32(0, 0, 0, 255));

        // 2. Rysowanie kafelków (Ściany i Podłoga)
        for (int dy = -viewRange; dy <= viewRange; ++dy) {
            for (int dx = -viewRange; dx <= viewRange; ++dx) {
                int wx = player_.GameX + dx;
                int wy = player_.GameY + dy;

                // Pozycja na ekranie
                float sx = p.x + (dx + viewRange) * tileSize;
                float sy = p.y + (dy + viewRange) * tileSize;

                if (wx >= 0 && wx < level_.w && wy >= 0 && wy < level_.h) {
                    int idx = wy * level_.w + wx;

                    // Rysujemy TYLKO jeśli odwiedziliśmy to pole!
                    // UWAGA: Upewnij się, że visited_cells_ jest zainicjalizowane w App.cpp (load_level)
                    if (!visited_cells_.empty() && visited_cells_[idx]) {
                        auto cell = level_.cells[idx];
                        ImU32 color;

                        if (cell == io::Cell::Wall)
                            color = IM_COL32(100, 100, 100, 255); // Szara ściana
                        else
                            color = IM_COL32(200, 200, 200, 255); // Jasna podłoga

                        drawList->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + tileSize, sy + tileSize), color);
                    }
                }
            }
        }

        // 3. Rysowanie Wrogów (Czerwone kropki)
        for (const auto* enemy : enemies_) {
            if (!enemy->IsAlive()) continue;

            // --- POPRAWKA: ZABEZPIECZENIE PRZED CRASHEM ---
            // Jeśli wróg jest poza tablicą (np. błąd spawnu), nie rysuj go
            if (enemy->GameX < 0 || enemy->GameX >= level_.w ||
                enemy->GameY < 0 || enemy->GameY >= level_.h) {
                continue;
            }
            // ---------------------------------------------

            int dx = enemy->GameX - player_.GameX;
            int dy = enemy->GameY - player_.GameY;

            if (std::abs(dx) <= viewRange && std::abs(dy) <= viewRange) {
                int idx = enemy->GameY * level_.w + enemy->GameX;

                // Teraz to jest bezpieczne
                if (!visited_cells_.empty() && visited_cells_[idx]) {
                    float sx = p.x + (dx + viewRange) * tileSize;
                    float sy = p.y + (dy + viewRange) * tileSize;
                    drawList->AddRectFilled(ImVec2(sx + 2, sy + 2), ImVec2(sx + tileSize - 2, sy + tileSize - 2), IM_COL32(255, 0, 0, 255));
                }
            }
        }

        // 4. Gracz (Zielona kropka na środku)
        float cx = p.x + viewRange * tileSize;
        float cy = p.y + viewRange * tileSize;
        drawList->AddRectFilled(ImVec2(cx + 3, cy + 3), ImVec2(cx + tileSize - 3, cy + tileSize - 3), IM_COL32(0, 255, 0, 255));

        // Ramka dookoła minimapy
        drawList->AddRect(p, ImVec2(p.x + mapSize, p.y + mapSize), IM_COL32(255, 255, 255, 255));

        // Rezerwujemy miejsce w layoutcie ImGui (żeby kolejne teksty były POD mapą)
        ImGui::Dummy(ImVec2(mapSize, mapSize + 10.0f));
        // --- KONIEC MINIMAPY ---

        ImGui::Separator();

        // B. EKWIPUNEK
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Equipped:");
        if (player_.equippedWeapon) {
            ImGui::BulletText("%s (DMG: %d)", player_.equippedWeapon->name.c_str(), player_.equippedWeapon->stats.damage);
        }
        else {
            ImGui::TextDisabled(" [No Weapon]");
        }

        if (player_.equippedArmor) {
            ImGui::BulletText("%s (HP: %d)", player_.equippedArmor->name.c_str(), player_.equippedArmor->stats.maxHealth);
        }

        ImGui::Separator();

        // C. PLECAK
        ImGui::TextColored(ImVec4(0, 0.8f, 1, 1), "Backpack:");
        if (player_.inventory.empty()) {
            ImGui::TextDisabled(" (Empty)");
        }
        else {
            for (auto* item : player_.inventory) {
                if (item->type == ItemType::Consumable) {
                    ImGui::BulletText("%s (Heal: %d)", item->name.c_str(), item->stats.health);
                }
                else {
                    ImGui::BulletText("%s", item->name.c_str());
                }
            }
        }

        ImGui::End();

        // --- MENU (bez zmian) ---
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
                camera_mode_ = (mode == 0) ? CameraMode::FirstPerson : CameraMode::ThirdPerson;

                ImGui::Separator();
                ImGui::Text("M - zamknij menu");
            }
            ImGui::End();
        }

        // --- DAMAGE FLASH ---
        if (player_.IsHurt()) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 0.0f, 0.0f, 0.4f));
            ImGui::Begin("##DamageFlash", nullptr,
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav);
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

} // namespace dungeon