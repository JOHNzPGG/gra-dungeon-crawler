#include "dungeon/core/App.hpp"
#include "dungeon/ui/Hud.hpp"

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
        if (enemy_vao_) glDeleteVertexArrays(1, &enemy_vao_);
        if (enemy_vbo_) glDeleteBuffers(1, &enemy_vbo_);

        if (window_) {
            glfwDestroyWindow(window_);
            glfwTerminate();
        }
        if (cube_vbo_) glDeleteBuffers(1, &cube_vbo_);
        if (cube_vao_) glDeleteVertexArrays(1, &cube_vao_);

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
            throw std::runtime_error(
                "Cannot open map: " + path +
                "\nCurrent directory: " + fs::current_path().string());
        }

        level_ = io::load_map_ascii(path);
        visited_cells_.assign(level_.w * level_.h, false);

        // startowa pozycja z mapy
        player_.GameX = level_.player_x;
        player_.GameY = level_.player_y;
        player_.yaw = 0; // 0° = "północ"
        player_.RenderPosition = glm::vec3(player_.GameX, 0.0f, player_.GameY);
    }

    void App::spawn_entities_from_level() {

        for (auto* e : enemies_) delete e;
        enemies_.clear();

        // (W prawdziwym projekcie tu też przydałby się delete na itemData, jeśli nie masz managera zasobów)
        world_items_.clear();

        for (const auto& p : level_.enemy_spawns) {
            Enemy* newEnemy = nullptr;

            if (p.type == 'Z') {
                // ZOMBIE: Wolny (1 AP), dużo życia (150), mocno bije (40)
                // Możesz tu w przyszłości dać inny model/teksturę
                newEnemy = new Enemy(p.x, p.y, 180, 150, 150, 1, 40, "Zombie");
            }
            else {
                // SZKIELET (S): Standardowy
                // Tworzymy przeciwnika w miejscu p.x, p.y
                // Parametry: x, y, yaw, hp, maxHp, ap, damage, name
                //Enemy* newEnemy = new Enemy(p.x, p.y, 180, 20, 20, 2, 5, "Skeleton");

                // HP: 80 (wytrzyma ok. 4 zwykłe ataki po 20 dmg, lub 2 potężne backstaby)
                // DMG: 35 (Gracz ma 100 HP, więc 35 to ok. 1/3 życia - 3 hity i giniemy!)
                Enemy* newEnemy = new Enemy(p.x, p.y, 180, 80, 80, 2, 35, "Skeleton Warrior");
                newEnemy = new Enemy(p.x, p.y, 180, 80, 80, 2, 35, "Skeleton Warrior");
            }

            if (newEnemy) {
                enemies_.push_back(newEnemy);
            }
        }

        int counter = 0;
        for (const auto& p : level_.item_spawns) {
            float x = static_cast<float>(p.x) + 0.5f;
            float z = static_cast<float>(p.y) + 0.5f;
            float y = 0.7f;

            Item* newItem = nullptr;

            // Pierwszy spawn to broń, reszta to mikstury
            if (counter == 0) {
                ItemStats stats; stats.damage = 20;
                newItem = new Item("Rusty Sword", ItemType::Weapon, false, stats);
            }
            else {
                ItemStats stats; stats.health = 5;
                newItem = new Item("Health Potion", ItemType::Consumable, true, stats);
            }

            if (newItem) {
                world_items_.push_back({ newItem, glm::vec3(x, y, z), true });
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
        // Jeśli trwa walka, blokujemy sterowanie.
        // update_combat() wywołamy w run(), żeby działało niezależnie od inputu.
        if (combat_lock_) {
            return;
        }

        // --- Obsługa klawiszy (bez zmian) ---
        bool raw_left = glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS;
        bool raw_right = glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS;
        bool raw_up = glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS;

        bool raw_left_w = glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS;
        bool raw_right_w = glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS;
        bool raw_up_w = glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS;

        bool left = raw_left || raw_left_w;
        bool right = raw_right || raw_right_w;
        bool up = raw_up || raw_up_w;

        bool atk = glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS;
        bool k1 = glfwGetKey(window_, GLFW_KEY_1) == GLFW_PRESS;
        bool k2 = glfwGetKey(window_, GLFW_KEY_2) == GLFW_PRESS;
        bool k3 = glfwGetKey(window_, GLFW_KEY_3) == GLFW_PRESS;
        bool m = glfwGetKey(window_, GLFW_KEY_M) == GLFW_PRESS;

        // --- Menu (bez zmian) ---
        if (m && !m_was_down_) {
            show_menu_ = !show_menu_;
        }

        if (show_menu_) {
            // Zapamiętaj stany i wyjdź
            left_was_down_ = left;
            right_was_down_ = right;
            up_was_down_ = up;
            atk_was_down_ = atk;
            k1_was_down_ = k1; k2_was_down_ = k2; k3_was_down_ = k3;
            m_was_down_ = m;
            return;
        }

        // --- Obrót ---
        if (left && !left_was_down_) player_.TurnLeft();
        if (right && !right_was_down_) player_.TurnRight();

        // --- Ruch ---
        if (up && !up_was_down_) {
            glm::ivec2 target = player_.GetForwardTile();

            // 1. Sprawdź czy to nie ściana (istniejące)
            if (can_move_to(target.x, target.y)) {

                // 2. NOWOŚĆ: Sprawdź, czy na polu docelowym nie stoi wróg!
                // Jeśli GetEnemyInFront zwróci cokolwiek innego niż nullptr, to znaczy że ktoś tam stoi.
                if (GetEnemyInFront(player_) == nullptr) {

                    // 3. Dopiero teraz sprawdzamy AP i ruszamy
                    if (player_.UseActionPoints(1)) {
                        player_.GameX = target.x;
                        player_.GameY = target.y;
                        player_.RenderPosition = glm::vec3(target.x, 0.f, target.y);
                    }
                }
                else {
                    std::cout << "Blokada: Przeciwnik na drodze!" << std::endl;
                    // Opcjonalnie: Możesz tu dodać dźwięk "bump" w przyszłości
                }
            }

            if (player_.UseActionPoints(1)) {
                player_.GameX = target.x;
                player_.GameY = target.y;
                player_.RenderPosition = glm::vec3(target.x, 0.f, target.y);

                // --- NOWE: SPRAWDZANIE PÓL SPECJALNYCH ---
                int idx = target.y * level_.w + target.x;
                auto cellType = level_.cells[idx];

                if (cellType == io::Cell::NextLevel) {
                    std::cout << "Przechodze do nastepnego poziomu!" << std::endl;
                    //load_next_level(); // Funkcja którą dodałeś wcześniej
                }
                else if (cellType == io::Cell::Exit) {
                    std::cout << "Zwyciestwo!" << std::endl;
                    //state_ = GameState::Victory; // Stan zwycięstwa
                }
            }

        }

        // --- Podnoszenie Przedmiotów (Item Pickup) ---
        for (int i = 0; i < world_items_.size(); ++i) {
            if (!world_items_[i].isAlive) continue;
            int ix = (int)world_items_[i].position.x;
            int iy = (int)world_items_[i].position.z;

            if (ix == player_.GameX && iy == player_.GameY) {
                Item* foundItem = world_items_[i].itemData;
                if (foundItem->type == ItemType::Weapon) {
                    if (player_.equippedWeapon != nullptr) {
                        Item* oldWeapon = player_.equippedWeapon;
                        WorldItem droppedItem;
                        droppedItem.itemData = oldWeapon;
                        droppedItem.position = glm::vec3(player_.GameX + 0.5f, 0.7f, player_.GameY + 0.5f);
                        droppedItem.isAlive = true;
                        world_items_.push_back(droppedItem);
                    }
                    player_.Equip(foundItem);
                    has_held_item_ = true;
                }
                else {
                    player_.AddToInventory(foundItem);
                }
                world_items_[i].isAlive = false;
                break;
            }
        }

        // --- Atak (SPACE) ---
        if (atk && !atk_was_down_) {
            Entity* target = GetEnemyInFront(player_);
            attack_anim_timer_ = kAttackDuration_;

            // Jeśli mamy AP, inicjujemy sekwencję walki
            if (player_.ActionPoints > 0) {
                if (target) {
                    // START SEKWENCJI WALKI
                    combat_lock_ = true;
                    combat_timer_ = 1.0f;
                    enemy_riposte_pending_ = true;
                    current_combat_target_ = target;

                    // Zadaj obrażenia (Twoja tura)
                    int pYaw = (player_.yaw % 360 + 360) % 360;
                    int eYaw = (target->yaw % 360 + 360) % 360;
                    bool backstab = (pYaw == eYaw);

                    int dmg = player_.base_damage;
                    if (backstab) dmg *= 2;

                    target->TakeDamage(dmg);
                    target->UpdateOrientation((player_.yaw + 180) % 360);
                }

                // Zużyj AP (nawet jak nie trafisz)
                player_.UseActionPoints(1);
            }
        }

        // --- Skille ---
        if (k1 && !k1_was_down_) player_.UseSkill(0, ResolveSkillTarget(player_, player_.skills[0]));
        if (k2 && !k2_was_down_) player_.UseSkill(1, ResolveSkillTarget(player_, player_.skills[1]));
        if (k3 && !k3_was_down_) player_.UseSkill(2, ResolveSkillTarget(player_, player_.skills[2]));

        // --- Zapamiętanie stanów ---
        left_was_down_ = left;
        right_was_down_ = right;
        up_was_down_ = up;
        atk_was_down_ = atk;
        k1_was_down_ = k1; k2_was_down_ = k2; k3_was_down_ = k3;
        m_was_down_ = m;

        // --- Koniec tury Gracza ---
        // Wywołujemy turę wrogów TYLKO jeśli nie ma blokady walki
        if (!combat_lock_ && player_.ActionPoints <= 0) {
            EnemiesTurn();
            player_.ResetActionPoints(2);
        }

        // USUNIĘTO: Tutaj był kod resetujący grę przy śmierci. 
        // Teraz śmierć obsługuje update_combat i pętla run.
    }

void App::EnemiesTurn() {
    for (Enemy* enemy : enemies_) {
        if (!enemy->IsAlive()) continue;

        // --- TO JEST KLUCZOWE ---
        enemy->ResetActionPoints(1); // Odnów siły wroga na nową turę
        // -------------------------

        enemy->TakeTurn(&player_, level_);
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
        // ZMIEŃ NAZWĘ PLIKU NA SWOJĄ!
        std::string modelPath = "assets/models/skeleton2.obj";
        std::string baseDir = "assets/models/";

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string err;

        bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, modelPath.c_str(), baseDir.c_str());

        if (!err.empty()) printf("[ENEMY LOAD INFO]: %s\n", err.c_str());
        if (!ret) return;

        // Tekstura
        if (!materials.empty() && !materials[0].diffuse_texname.empty()) {
            std::string texPath = baseDir + materials[0].diffuse_texname;
            enemy_texture_ = load_texture(texPath.c_str());
        }
        else {
            // Fallback texture (opcjonalnie)
            // enemy_texture_ = load_texture("assets/models/skeleton.png");
        }

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

        enemy_vertex_count_ = (int)(vertices.size() / 5);

        glGenVertexArrays(1, &enemy_vao_);
        glGenBuffers(1, &enemy_vbo_);
        glBindVertexArray(enemy_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, enemy_vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
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
        if (attack_anim_timer_ > 0.0f) {
            attack_anim_timer_ -= dt;
            if (attack_anim_timer_ < 0.0f) attack_anim_timer_ = 0.0f;
        }

        float px = static_cast<float>(player_.GameX) + 0.5f;
        float pz = static_cast<float>(player_.GameY) + 0.5f;

        glm::vec3 forward;
        int dx = 0;
        int dz = 0;

        int yaw = ((player_.yaw % 360) + 360) % 360;

        switch (yaw) {
        case 0:   forward = { 0.0f, 0.0f, -1.0f }; dx = 0;  dz = -1; break;
        case 180: forward = { 0.0f, 0.0f,  1.0f }; dx = 0;  dz = 1; break;
        case 270: forward = { -1.0f, 0.0f,  0.0f }; dx = -1; dz = 0; break;
        case 90:  forward = { 1.0f, 0.0f,  0.0f }; dx = 1;  dz = 0; break;
        default:  forward = { 0.0f, 0.0f, -1.0f }; dx = 0;  dz = -1; break;
        }

        glm::vec3 cam_pos;
        glm::vec3 cam_target;

        if (camera_mode_ == CameraMode::FirstPerson) {
            glm::vec3 eye(px, 0.9f, pz);
            cam_pos = eye - forward * 0.05f;
            cam_target = eye + forward * 1.2f;
        }
        else {
            int bx = player_.GameX - dx;
            int by = player_.GameY - dz;
            bool behind_is_blocked = !can_move_to(bx, by);

            cam_target = glm::vec3(px, 0.7f, pz) + forward * 0.35f;

            float cam_distance = behind_is_blocked ? 0.35f : 1.0f;
            float cam_height = 0.9f;

            cam_pos = cam_target - forward * cam_distance
                + glm::vec3(0.0f, cam_height, 0.0f);
        }

        view_ = glm::lookAt(cam_pos, cam_target, glm::vec3(0.0f, 1.0f, 0.0f));

        world_shader_.use();
        world_shader_.setMat4("uProj", &proj_[0][0]);
        world_shader_.setMat4("uView", &view_[0][0]);
        world_shader_.setVec3("uCamPos", cam_pos.x, cam_pos.y, cam_pos.z);
        world_shader_.setFloat("uTime", (float)glfwGetTime());
        glm::mat4 I(1.0f);
        world_shader_.setMat4("uModel", &I[0][0]);
        world_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 1.0f);

        // WAŻNE: Włączamy teksturowanie dla nowego shadera
        world_shader_.setInt("uUseTex", 1);

        // PODŁOGA
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, floor_texture_);
        world_shader_.setInt("uTex", 0);

        if (floor_vertex_count_ > 0) {
            glBindVertexArray(floor_vao_);
            glDrawArrays(GL_TRIANGLES, 0, floor_vertex_count_);
        }

        // ŚCIANY
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, wall_texture_);
        world_shader_.setInt("uTex", 0);

        if (wall_vertex_count_ > 0) {
            glBindVertexArray(wall_vao_);
            glDrawArrays(GL_TRIANGLES, 0, wall_vertex_count_);
        }

        // --- RYSOWANIE PORTALI / WYJŚĆ ---
        world_shader_.setInt("uUseTex", 0); // Wyłączamy tekstury, używamy kolorów
        glBindVertexArray(cube_vao_);       // Używamy kostki

        for (int y = 0; y < level_.h; ++y) {
            for (int x = 0; x < level_.w; ++x) {
                auto cell = level_.cells[y * level_.w + x];

                if (cell == io::Cell::NextLevel || cell == io::Cell::Exit) {

                    if (cell == io::Cell::NextLevel)
                        world_shader_.setVec4("uColor", 0.0f, 0.5f, 1.0f, 0.6f); // Niebieski, półprzeźroczysty
                    else
                        world_shader_.setVec4("uColor", 1.0f, 0.8f, 0.0f, 0.6f); // Złoty, półprzeźroczysty

                    glm::mat4 M(1.0f);
                    // Ustawiamy w przejściu, trochę wyższy niż podłoga
                    M = glm::translate(M, glm::vec3(x + 0.5f, 0.5f, y + 0.5f));
                    M = glm::scale(M, glm::vec3(0.8f, 1.0f, 0.8f)); // Trochę węższy niż kratka

                    world_shader_.setMat4("uModel", &M[0][0]);
                    glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
                }
            }
        }
        glBindVertexArray(0);

        // --- ENEMIES ---
        // Uwaga: Nie bindujemy na sztywno cube_vao_, bo mamy teraz model!

        for (auto* enemy : enemies_) {
            float dt = ImGui::GetIO().DeltaTime;
            enemy->UpdateDeath(dt);

            if (!enemy->IsAlive() && enemy->deathAnimFinished) continue;

            // 1. Logika Koloru i Tekstury
            world_shader_.use();

            // Jeśli mamy teksturę wroga, używamy jej
            if (enemy_texture_ != 0) {
                world_shader_.setInt("uUseTex", 1);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, enemy_texture_);
                world_shader_.setInt("uTex", 0);
            }
            else {
                world_shader_.setInt("uUseTex", 0); // Brak tekstury = kolor
            }

            float y_offset = 0.55f; // Model zazwyczaj stoi na 0.0, a nie lewituje jak kostka

            if (!enemy->IsAlive()) {
                // ŚMIERĆ: Zanikanie
                float alpha = 1.0f - enemy->deathTimer;
                if (alpha < 0.0f) alpha = 0.0f;
                y_offset -= (enemy->deathTimer * 0.5f); // Zapadanie się

                // Mnożnik koloru (szary + alpha)
                // Dzięki zmianie w shaderze, tekstura stanie się ciemna i przezroczysta!
                world_shader_.setVec4("uColor", 0.5f, 0.5f, 0.5f, alpha);
            }
            else if (enemy->IsHurt()) {
                // BŁYSK: Czerwony tint
                // Tekstura * Czerwony = "Krwisty" model
                world_shader_.setVec4("uColor", 1.0f, 0.5f, 0.5f, 1.0f);
            }
            else {
                // NORMALNIE: Biały (1,1,1,1) oznacza "oryginalne kolory tekstury"
                world_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 1.0f);
            }

            // 2. Pozycja
            float x = static_cast<float>(enemy->GameX) + 0.5f; // Środek kafelka
            float z = static_cast<float>(enemy->GameY) + 0.5f;

            glm::mat4 M(1.0f);
            M = glm::translate(M, glm::vec3(x, y_offset, z));

            // Obrót (Yaw)
            M = glm::rotate(M, glm::radians((float)enemy->yaw), glm::vec3(0, 1, 0));

            // Obrót przy śmierci (przewrócenie)
            if (!enemy->IsAlive()) {
                float deathAngle = -90.0f * enemy->deathTimer;
                if (deathAngle < -90.0f) deathAngle = -90.0f;
                M = glm::rotate(M, glm::radians(deathAngle), glm::vec3(1, 0, 0));
            }

            // --- SKALOWANIE WROGA ---
            // Podobnie jak przy mieczu, musisz to dopasować do modelu!
            float enemyScale = 1.0f;
            M = glm::scale(M, glm::vec3(enemyScale));

            world_shader_.setMat4("uModel", &M[0][0]);

            // 3. Rysowanie (Model lub Sześcian)
            if (enemy_vertex_count_ > 0) {
                glBindVertexArray(enemy_vao_);
                glDrawArrays(GL_TRIANGLES, 0, enemy_vertex_count_);
            }
            else {
                // Fallback (jeśli nie udało się wczytać modelu)
                glBindVertexArray(cube_vao_);
                world_shader_.setInt("uUseTex", 0); // Kostka nie ma UV dla tekstury modelu
                world_shader_.setVec4("uColor", 1.0f, 0.0f, 0.0f, 1.0f); // Czerwony
                glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
            }

            // 4. Pasek HP (bez zmian - kopiuj/wklej ze starego kodu jeśli usunąłeś)
            if (enemy->IsAlive() && enemy->health < enemy->maxHealth) {
                glBindVertexArray(cube_vao_); // Pasek HP to zawsze kostka
                world_shader_.setInt("uUseTex", 0);

                // ... (Twoja logika paska HP) ...
                // TŁO
                world_shader_.setVec4("uColor", 0.5f, 0.0f, 0.0f, 1.0f);
                glm::mat4 M_bg(1.0f);
                // Podnieś pasek wyżej (np. y + 1.8), bo model może być wysoki
                M_bg = glm::translate(M_bg, glm::vec3(x, 1.8f, z));
                M_bg = glm::scale(M_bg, glm::vec3(0.6f, 0.05f, 0.05f));
                world_shader_.setMat4("uModel", &M_bg[0][0]);
                glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);

                // PASEK ZIELONY
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

        // --- ITEMS ---
        world_shader_.setInt("uUseTex", 0);
        world_shader_.setVec4("uColor", 0.4f, 0.8f, 0.8f, 1.0f); // Szary kolor

        glBindVertexArray(cube_vao_);
        for (const auto& wItem : world_items_) {
            if (!wItem.isAlive) continue; // Nie rysuj zebranych

            // Opcjonalnie: Różne kolory dla różnych typów
            if (wItem.itemData->type == ItemType::Weapon) {
                world_shader_.setVec4("uColor", 0.0f, 0.5f, 0.5f, 1.0f); // Pomarańczowy dla broni
            }
            else {
                world_shader_.setVec4("uColor", 0.2f, 0.2f, 1.0f, 1.0f); // Niebieski dla mikstur
            }

            glm::mat4 M(1.0f);
            M = glm::translate(M, wItem.position);
            M = glm::scale(M, glm::vec3(0.25f));
            world_shader_.setMat4("uModel", &M[0][0]);
            glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
        }
        glBindVertexArray(0);

        // --- HELD ITEM (Z ANIMACJĄ) ---
        if (has_held_item_) {

            // Pozif (has_held_item_) {
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            glm::vec3 rightv = glm::normalize(glm::cross(forward, up));

            // --- POZYCJONOWANIE BRONI ---
            // Tu będziesz musiał poeksperymentować! Modele z neta mają różne skale i środki.
            // Zacznij od tych wartości i zmieniaj je, jeśli miecz jest za duży/mały/daleko.
            glm::vec3 item_pos = cam_pos
                + forward * 0.4f    // Jak daleko przed kamerą
                + rightv * 0.2f     // Jak bardzo w prawo
                + up * -0.3f;      // Jak nisko

            // --- OBLICZANIE ANIMACJI (Bez zmian - działa super) ---
            float animOffset = 0.0f;
            float animTilt = 0.0f;

            if (attack_anim_timer_ > 0.0f) {
                float progress = 1.0f - (attack_anim_timer_ / kAttackDuration_);
                float wave = std::sin(progress * 3.14159f);
                animOffset = wave * 0.5f;
                animTilt = wave * 45.0f;
            }

            // Aplikujemy animację
            item_pos += forward * animOffset;
            item_pos += up * (-animOffset * 0.2f); // Mniejszy opad

            // --- RYSOWANIE ---
            world_shader_.use();

            // Włącz tekstury, jeśli miecz ma teksturę
            if (weapon_texture_ != 0) {
                world_shader_.setInt("uUseTex", 1);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, weapon_texture_);
                world_shader_.setInt("uTex", 0);
            }
            else {
                world_shader_.setInt("uUseTex", 0);
                world_shader_.setVec4("uColor", 0.7f, 0.7f, 0.7f, 1.0f); // Srebrny kolor bez tekstury
            }

            glm::mat4 M(1.0f);
            M = glm::translate(M, item_pos);

            // 1. Obrót gracza (żeby miecz zawsze był przed kamerą)
            // Używamy ujemnego 'yaw', bo GLM rotate działa odwrotnie niż nasz system kątów
            M = glm::rotate(M, glm::radians((float)yaw), glm::vec3(0, 1, 0));

            // 2. Obrót korekcyjny modelu (Bardzo ważne!)
            // Modele .obj często leżą na płasko albo są obrócone.
            // Tutaj obracamy go tak, żeby "stał" i celował do przodu.
            // Często trzeba obrócić o 180 lub 90 stopni w Y.
            M = glm::rotate(M, glm::radians(180.0f), glm::vec3(0, 1, 0));

            // 3. Animacja ataku (Cięcie)
            M = glm::rotate(M, glm::radians(animTilt), glm::vec3(1, 0, 0)); // Oś X lokalna

            // 4. Skala (Najważniejsze - modele bywają gigantyczne!)
            // Zmniejsz to np. do 0.01f jeśli miecz zasłania cały ekran!
            float scale = 0.0015f;
            M = glm::scale(M, glm::vec3(scale));

            world_shader_.setMat4("uModel", &M[0][0]);

            // Rysujemy nowy model
            if (weapon_vertex_count_ > 0) {
                glBindVertexArray(weapon_vao_);
                glDrawArrays(GL_TRIANGLES, 0, weapon_vertex_count_);
                glBindVertexArray(0);
            }

            // Reset macierzy (dla bezpieczeństwa)
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

            int dx = enemy->GameX - player_.GameX;
            int dy = enemy->GameY - player_.GameY;

            if (std::abs(dx) <= viewRange && std::abs(dy) <= viewRange) {
                int idx = enemy->GameY * level_.w + enemy->GameX;
                // Rysuj wroga tylko jeśli stoi na odkrytym terenie
                if (!visited_cells_.empty() && visited_cells_[idx]) {
                    float sx = p.x + (dx + viewRange) * tileSize;
                    float sy = p.y + (dy + viewRange) * tileSize;
                    // Czerwona kropka
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

    void App::render_options_menu() {

        ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(
            ImVec2(cfg_.width / 2 - 210, cfg_.height / 2 - 150),
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

            // okno jako zwykłe (monitor = nullptr), ale rozciągnięte na cały ekran i bez ramek
            glfwSetWindowMonitor(window_, nullptr, 0, 0, mode->width, mode->height, 0);
            glfwSetWindowAttrib(window_, GLFW_DECORATED, GLFW_FALSE);

            on_resize(mode->width, mode->height);
        }

        // Pelny ekran ekskluzywny (GLFW fullscreen na monitorze)
        if (ImGui::Button("Pelny ekran (exclusive)", ImVec2(250, 30))) {
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(mon);

            glfwSetWindowMonitor(window_, mon, 0, 0, mode->width, mode->height, mode->refreshRate);

            on_resize(mode->width, mode->height);
        }

        ImGui::Separator();
        ImGui::Text("Dźwięk:");
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

            if (state_ == GameState::Playing && combat_lock_) {
                update_combat();
            }

            switch (state_) {
            case GameState::MainMenu:
                render_main_menu();
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
            }

            frame_end();
        }
    }

    void App::reset_game() {
        // 1. Reset statystyk gracza
        player_.maxHealth = 100;
        player_.health = player_.maxHealth;
        player_.ActionPoints = 2;
        player_.base_damage = 10;

        // 2. Czyścimy ekwipunek (Totalny reset)
        player_.inventory.clear();
        player_.equippedWeapon = nullptr;
        player_.equippedArmor = nullptr;

        // 3. Reset pozycji (startowa z mapy)
        load_level(); // Przeładuj mapę od zera (to zresetuje pozycje spawnu)

        // 4. Respawn świata
        spawn_entities_from_level(); // Tworzy wrogów i itemy na nowo

        // Upewnij się, że pozycja gracza jest zsynchronizowana
        player_.GameX = level_.player_x;
        player_.GameY = level_.player_y;
        player_.RenderPosition = glm::vec3(player_.GameX, 0.0f, player_.GameY);

        // Reset stanów walki
        combat_lock_ = false;
        player_.IsAlive(); // Tylko check, health już ustawione
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
                // Wróg atakuje gracza (bez zużywania swoich AP, to jest riposta!)
                int dmg = current_combat_target_->base_damage;
                player_.TakeDamage(dmg);
                // To wywoła IsHurt() i czerwony błysk ekranu w frame_ui
            }
            enemy_riposte_pending_ = false; // Już oddał
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

} // namespace dungeon