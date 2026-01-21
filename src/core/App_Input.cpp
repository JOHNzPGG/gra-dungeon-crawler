#include "dungeon/core/App.hpp"
#include <iostream>

namespace dungeon {

    void App::handle_input() {
        if (combat_lock_ || is_moving_ || attack_anim_timer_ > 0.0f) {
            if (combat_lock_) update_combat();
            return;
        }

        bool left = glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS;
        bool right = glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS;
        bool up = glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS;
        bool atk = glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS;
        bool esc = glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        bool k1 = glfwGetKey(window_, GLFW_KEY_1) == GLFW_PRESS;
        bool k2 = glfwGetKey(window_, GLFW_KEY_2) == GLFW_PRESS;
        bool k3 = glfwGetKey(window_, GLFW_KEY_3) == GLFW_PRESS;
        bool keyH = glfwGetKey(window_, GLFW_KEY_H) == GLFW_PRESS;

        if (esc && !esc_was_down_) {
            if (state_ == GameState::Playing) state_ = GameState::Paused;
            else if (state_ == GameState::Paused) state_ = GameState::Playing;
            else if (state_ == GameState::Options) state_ = GameState::Paused;
        }
        esc_was_down_ = esc;

        if (state_ != GameState::Playing) {
            left_was_down_ = left; right_was_down_ = right; up_was_down_ = up; atk_was_down_ = atk;
            return;
        }

        if (left && !left_was_down_) player_.TurnLeft();
        if (right && !right_was_down_) player_.TurnRight();

        if (up && !up_was_down_) {
            glm::ivec2 target = player_.GetForwardTile();
            if (can_move_to(target.x, target.y) && GetEnemyInFront(player_) == nullptr) {
                if (player_.UseActionPoints(1)) {
                    move_start_pos_ = glm::vec3(player_.RenderPosition.x, 0.0f, player_.RenderPosition.z);
                    move_target_pos_ = glm::vec3(target.x, 0.0f, target.y);
                    player_.GameX = target.x;
                    player_.GameY = target.y;
                    weapon_swap_lock_ = false;
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
            }
            else {
                std::cout << "Blokada!" << std::endl;
            }
        }

        // Auto-pickup 
        for (auto& wItem : world_items_) {
            if (!wItem.isAlive) continue;

            if ((int)wItem.position.x == player_.GameX && (int)wItem.position.z == player_.GameY) {
                Item* item = wItem.itemData;

                if (item->type == ItemType::Weapon) {
                    if (weapon_swap_lock_) continue;

                    if (player_.equippedWeapon) {
                        WorldItem dropped;
                        dropped.itemData = player_.equippedWeapon;
                        dropped.position = glm::vec3(player_.GameX + 0.5f, 0.7f, player_.GameY + 0.5f);
                        dropped.isAlive = true;
                        world_items_.push_back(dropped);
                    }
                    player_.Equip(item);
                    has_held_item_ = true;
                    weapon_swap_lock_ = true;
                }
                else {
                    player_.AddToInventory(item);
                }

                wItem.isAlive = false;
                break;
            }
        }

        // Atak / Interakcja
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
                    break;
                }
            }
        }
        h_was_down_ = keyH;

        left_was_down_ = left; right_was_down_ = right; up_was_down_ = up; atk_was_down_ = atk;
        k1_was_down_ = k1; k2_was_down_ = k2; k3_was_down_ = k3;

        if (!combat_lock_ && player_.ActionPoints <= 0) {
            EnemiesTurn();
            player_.ResetActionPoints(2);
        }
    }

}