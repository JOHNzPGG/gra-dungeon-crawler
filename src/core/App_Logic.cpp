#include "dungeon/core/App.hpp"
#include <imgui.h>
#include <filesystem>
#include <cmath>
#include <iostream>

namespace dungeon {

    void App::load_level() {
        namespace fs = std::filesystem;
        if (current_map_name_.empty()) current_map_name_ = map_list_[0];
        std::string path = current_map_name_;
        if (!fs::exists(path)) {
            printf("Brak mapy: %s. Wczytuje fallback.\n", path.c_str());
            path = "assets/maps/test.map";
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

    // --- SPAWNOWANIE JEDNOSTEK (Dziêki temu s¹ ikony i modele) --- 
    void App::spawn_entities_from_level() {
        for (auto* e : enemies_) delete e;
        enemies_.clear();
        world_items_.clear();
        puzzle_torches_.clear();
        pressure_plates_.clear();
        puzzles_solved_ = false;

        // 1. WROGOWIE
        for (const auto& spawn : level_.enemy_spawns) {
            Enemy* newEnemy = nullptr;
            if (spawn.type == 'Z') {
                newEnemy = new Enemy(spawn.x, spawn.y, 180, 140, 140, 1, 25, "Zombie");
            }
            else if (spawn.type == 'S') {
                newEnemy = new Enemy(spawn.x, spawn.y, 180, 60, 60, 2, 10, "Skeleton");
            }
            else {
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

        // 2. PRZEDMIOTY (Miecz, Potion, Item)
        for (const auto& spawn : level_.item_spawns) {
            float x = static_cast<float>(spawn.x) + 0.5f;
            float z = static_cast<float>(spawn.y) + 0.5f;
            Item* newItem = nullptr;
            char t = spawn.type;

            if (t == 'P') {
                ItemStats stats; stats.health = 40;
                newItem = new Item("Health Potion", ItemType::Consumable, true, stats);
            }
            else if (t == 'M') {
                // EASTER EGG: MAXWELL
                ItemStats stats; stats.damage = 150;
                newItem = new Item("MAXWELL", ItemType::Weapon, false, stats);
            }
            else if (t == '1') {
                // POZIOM 1: S³aby miecz
                ItemStats stats; stats.damage = 20;
                newItem = new Item("Rusty Sword", ItemType::Weapon, false, stats);
            }
            else if (t == '2') {
                // POZIOM 2: Solidny miecz
                ItemStats stats; stats.damage = 45;
                newItem = new Item("Iron Sword", ItemType::Weapon, false, stats);
            }
            else if (t == '3') {
				// POZIOM 3: Najlepszy miecz
                ItemStats stats; stats.damage = 80;
                newItem = new Item("GOD SLAYER", ItemType::Weapon, false, stats);
            }
            else if (t == 'I') {
                // Atefakt
                ItemStats stats; stats.damage = 100;
                newItem = new Item("Artifact", ItemType::Weapon, false, stats);
            }

            if (newItem) {
                world_items_.push_back({ newItem, glm::vec3(x, 0.8f, z), true });
            }
        }

        // 3. ZAGADKI (Pochodnie i P³yty)
        for (const auto& spawn : level_.puzzle_torches) {
            puzzle_torches_.push_back({ spawn.x, spawn.y, false });
        }
        for (const auto& spawn : level_.pressure_plates) {
            int id = (int)pressure_plates_.size() + 1;
            pressure_plates_.push_back({ spawn.x, spawn.y, id, 0 });
        }
    }

    bool App::can_move_to(int x, int y) const {
        if (x < 0 || y < 0) return false;
        if (x >= level_.w || y >= level_.h) return false;
        return level_.cells[y * level_.w + x] != io::Cell::Wall;
    }

    Entity* App::GetEnemyInFront(const Entity& unit) {
        glm::ivec2 front = unit.GetForwardTile();
        for (Entity* e : enemies_) {
            if (!e->IsAlive()) continue;
            if (e->GameX == front.x && e->GameY == front.y) return e;
        }
        return nullptr;
    }

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

    void App::EnemiesTurn() {
        for (Enemy* enemy : enemies_) {
            if (!enemy->IsAlive()) continue;
            enemy->ResetActionPoints(1);
            int oldX = enemy->GameX;
            int oldY = enemy->GameY;
            enemy->TakeTurn(&player_, level_);
            if (enemy->GameX != oldX || enemy->GameY != oldY) {
                enemy->StartMoveAnimation(oldX, oldY, enemy->GameX, enemy->GameY);
            }
        }
    }

    void App::toggle_puzzle_torch(int x, int y) {
        if (y != 0 || x < 4 || x > 9) return;
        int dx[] = { 0, -1, 1 };
        for (int i = 0; i < 3; ++i) {
            int targetX = x + dx[i];
            if (targetX >= 4 && targetX <= 9) {
                for (auto& t : puzzle_torches_) {
                    if (t.x == targetX && t.y == 0) {
                        t.is_lit = !t.is_lit;
                    }
                }
            }
        }
        int lit_count = 0;
        for (const auto& t : puzzle_torches_) if (t.y == 0 && t.is_lit) lit_count++;
        if (lit_count == 6) {
            printf("Pochodnie zapalone! Otwieram tajne przejœcie.\n");
            level_.cells[4 * level_.w + 5] = io::Cell::Floor;
            build_world_mesh(); // Przebuduj œwiat ¿eby usun¹æ œcianê
            trauma_ = 0.5f;
        }
    }

    void App::update_puzzles() {
        if (player_.GameX == last_puzzle_x_ && player_.GameY == last_puzzle_y_) return;
        last_puzzle_x_ = player_.GameX; last_puzzle_y_ = player_.GameY;

        struct Step { int x, y, goal; const char* desc; };
        const Step sequence[] = { {6, 3, 3, "SRODEK"}, {5, 4, 2, "ZACHOD"}, {7, 4, 3, "WSCHOD"}, {6, 5, 1, "POLNOC"} };
        const int num_stages = 4;

        PressurePlate* stepped = nullptr;
        for (auto& p : pressure_plates_) {
            if (p.x == last_puzzle_x_ && p.y == last_puzzle_y_) { stepped = &p; break; }
        }
        if (!stepped) return;

        const Step& target = sequence[current_stage_idx_];
        if (stepped->x == target.x && stepped->y == target.y) {
            stepped->count++;
            if (stepped->count == target.goal) {
                current_stage_idx_++;
                for (auto& rp : pressure_plates_) rp.count = 0;
            }
        }
        else {
            current_stage_idx_ = 0;
            for (auto& rp : pressure_plates_) rp.count = 0;
        }

        if (current_stage_idx_ == num_stages && !puzzles_solved_) {
            puzzles_solved_ = true;
            level_.cells[3 * level_.w + 2] = io::Cell::Floor;
            build_world_mesh();
            trauma_ = 0.6f;
        }
    }

    void App::update_combat() {
        if (!combat_lock_) return;
        float dt = ImGui::GetIO().DeltaTime;
        combat_timer_ -= dt;
        if (combat_timer_ <= 0.5f && enemy_riposte_pending_) {
            if (current_combat_target_ && current_combat_target_->IsAlive()) {
                int dmg = current_combat_target_->base_damage;
                player_.TakeDamage(dmg);
                trauma_ = 0.5f;
            }
            enemy_riposte_pending_ = false;
        }
        if (combat_timer_ <= 0.0f) {
            combat_lock_ = false;
            current_combat_target_ = nullptr;
            player_.ResetActionPoints(2);
            if (!player_.IsAlive()) state_ = GameState::GameOver;
        }
    }

    void App::update_exploration() {
        int radius = 5;
        int px = player_.GameX; int py = player_.GameY;
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x * x + y * y > radius * radius) continue;
                int tx = px + x; int ty = py + y;
                if (tx >= 0 && tx < level_.w && ty >= 0 && ty < level_.h) {
                    if (check_los(px, py, tx, ty)) visited_cells_[ty * level_.w + tx] = true;
                }
            }
        }
    }

    bool App::check_los(int x1, int y1, int x2, int y2) const {
        if (x1 == x2 && y1 == y2) return true;
        float dist = std::sqrt(std::pow(x2 - x1, 2) + std::pow(y2 - y1, 2));
        if (dist < 0.5f) return true;
        float stepX = (x2 - x1) / dist; float stepY = (y2 - y1) / dist;
        float cx = (float)x1 + 0.5f; float cy = (float)y1 + 0.5f;
        for (float t = 0.0f; t < dist - 0.1f; t += 0.5f) {
            cx += stepX * 0.5f; cy += stepY * 0.5f;
            int ix = (int)cx; int iy = (int)cy;
            if (ix < 0 || iy < 0 || ix >= level_.w || iy >= level_.h) return false;
            if (level_.cells[iy * level_.w + ix] == io::Cell::Wall) {
                if (ix == x2 && iy == y2) return true;
                return false;
            }
        }
        return true;
    }

    void App::load_next_level() {
        current_level_idx_++;
        if (current_level_idx_ < map_list_.size()) {
            current_map_name_ = map_list_[current_level_idx_];
            player_.ResetActionPoints(2);
            if (floor_vbo_) glDeleteBuffers(1, &floor_vbo_);
            if (floor_vao_) glDeleteVertexArrays(1, &floor_vao_);
            if (wall_vbo_)  glDeleteBuffers(1, &wall_vbo_);
            if (wall_vao_)  glDeleteVertexArrays(1, &wall_vao_);
            floor_vao_ = 0; floor_vbo_ = 0; wall_vao_ = 0; wall_vbo_ = 0;
            load_level();
            build_world_mesh();
            spawn_entities_from_level();
            visited_cells_.assign(level_.w * level_.h, false);
            update_exploration();
        }
        else {
            state_ = GameState::Victory;
        }
    }

    void App::reset_game() {
        has_held_item_ = false;
        attack_anim_timer_ = 0.0f;
        is_moving_ = false;
        move_timer_ = 0.0f;
        player_.health = player_.maxHealth;
        player_.ResetActionPoints(2);
        current_level_idx_ = 0;
        current_map_name_ = map_list_[0];
        if (floor_vbo_) glDeleteBuffers(1, &floor_vbo_);
        if (floor_vao_) glDeleteVertexArrays(1, &floor_vao_);
        if (wall_vbo_)  glDeleteBuffers(1, &wall_vbo_);
        if (wall_vao_)  glDeleteVertexArrays(1, &wall_vao_);
        floor_vao_ = 0; floor_vbo_ = 0; wall_vao_ = 0; wall_vbo_ = 0;
        load_level();
        build_world_mesh();
        spawn_entities_from_level();
        visited_cells_.assign(level_.w * level_.h, false);
        update_exploration();
    }

    void App::init_puzzles(const io::Level& L) {
        puzzle_torches_.clear();
        pressure_plates_.clear();
        for (const auto& s : L.item_spawns) {
            if (s.type == 'L') puzzle_torches_.push_back({ s.x, s.y, false });
            if (s.type == 'T') pressure_plates_.push_back({ s.x, s.y, (int)pressure_plates_.size() + 1, 0 });
        }
    }

    void App::update_audio_state() {
        if (state_ == GameState::Playing) {
            if (!ma_sound_is_playing(&sfx_torch_)) ma_sound_start(&sfx_torch_);
        }
        else {
            if (ma_sound_is_playing(&sfx_torch_)) ma_sound_stop(&sfx_torch_);
        }
    }

    PuzzleTorch* App::get_puzzle_torch(int x, int y) {
        for (auto& t : puzzle_torches_) {
            if (t.x == x && t.y == y) return &t;
        }
        return nullptr;
    }

}