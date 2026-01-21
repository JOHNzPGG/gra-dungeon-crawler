/**
 * @file App_Render.cpp
 * @brief Modu³ renderuj¹cy grafikê 3D.
 * Zawiera funkcjê `frame_render`, która odpowiada za narysowanie ca³ej sceny gry:
 * mapy, wrogów, przedmiotów oraz interfejsu 3D (broñ w rêce).
 */

#include "dungeon/core/App.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/gtc/matrix_transform.hpp>

namespace dungeon {

    void App::frame_begin() {
        glfwPollEvents();
        handle_input();
        glClearColor(0.05f, 0.06f, 0.08f, 1.0f); // Ciemnoszare t³o (mg³a)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void App::frame_end() {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }

    /**
     * @brief G³ówna funkcja rysuj¹ca scenê 3D.
     * Wykonuje siê co klatkê.
     * 1. Aktualizuje logikê (puzzles, exploration).
     * 2. Oblicza pozycjê kamery i interpoluje ruch gracza.
     * 3. Konfiguruje shadery (œwiat³a, macierze).
     * 4. Rysuje obiekty w kolejnoœci: Pod³oga -> Œciany -> Portale -> Pochodnie -> Wrogowie -> Przedmioty -> Broñ w rêce.
     */
    void App::frame_render() {
        // --- 1. LOGIKA ROZGRYWKI (Update w render loop dla p³ynnoœci) ---
        update_exploration();
        update_puzzles();

        float dt = ImGui::GetIO().DeltaTime;

        // Efekt Screen Shake
        if (trauma_ > 0.0f) {
            trauma_ -= dt;
            if (trauma_ < 0.0f) trauma_ = 0.0f;
        }

        // Interpolacja ruchu gracza (p³ynne przejœcie miêdzy kratkami)
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
                float headBob = std::sin(t * 3.14159f) * 0.08f;
                player_.RenderPosition.y = headBob;
            }
        }

        // Animacja ataku
        if (attack_anim_timer_ > 0.0f) {
            attack_anim_timer_ -= dt;
            if (attack_anim_timer_ < 0.0f) attack_anim_timer_ = 0.0f;
        }

        // --- 2. KAMERA ---
        float yaw = (float)player_.yaw;
        glm::vec3 cam_pos;
        glm::vec3 center;
        glm::vec3 up(0.0f, 1.0f, 0.0f);

        // Kamera w Menu (Orbituj¹ca)
        if (state_ == GameState::MainMenu || state_ == GameState::Credits || state_ == GameState::Options) {
            float radius = 6.0f;
            float camX = std::sin(menu_timer_ * 0.2f) * radius + (level_.w / 2.0f);
            float camZ = std::cos(menu_timer_ * 0.2f) * radius + (level_.h / 2.0f);
            cam_pos = glm::vec3(camX, 5.0f, camZ);
            center = glm::vec3(level_.w / 2.0f, 0.0f, level_.h / 2.0f);
        }
        // Kamera Gracza
        else {
            float rad = glm::radians(yaw);
            glm::vec3 forward(std::sin(rad), 0.0f, -std::cos(rad));
            cam_pos = player_.RenderPosition;
            cam_pos += glm::vec3(0.5f, 0.0f, 0.5f);

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
                cam_pos.y += 0.95f;
                cam_pos -= forward * 0.3f;
                glm::vec3 rightv = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
                cam_pos += rightv * 0.15f;
            }
            center = cam_pos + forward;
        }
        view_ = glm::lookAt(cam_pos, center, up);

        // --- 3. KONFIGURACJA SHADERA I OŒWIETLENIA ---
        world_shader_.use();
        world_shader_.setMat4("uProj", &proj_[0][0]);
        world_shader_.setMat4("uView", &view_[0][0]);
        world_shader_.setVec3("uCamPos", cam_pos.x, cam_pos.y, cam_pos.z);
        world_shader_.setFloat("uTime", (float)glfwGetTime());

        // Przesy³anie aktywnych œwiate³ (Pochodnie)
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

        // --- 4. RYSOWANIE ŒWIATA ---

        // A. POD£OGA
        world_shader_.setInt("uUseTex", 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, floor_texture_);
        world_shader_.setInt("uTex", 0);
        if (floor_vertex_count_ > 0) {
            glBindVertexArray(floor_vao_);
            glDrawArrays(GL_TRIANGLES, 0, floor_vertex_count_);
        }

        // B. ŒCIANY
        world_shader_.setMat4("uModel", &I[0][0]); // Reset modelu
        glBindTexture(GL_TEXTURE_2D, wall_texture_);
        if (wall_vertex_count_ > 0) {
            glBindVertexArray(wall_vao_);
            glDrawArrays(GL_TRIANGLES, 0, wall_vertex_count_);
        }

        // C. PORTALE I WYJŒCIA
        world_shader_.setInt("uUseTex", 0);
        glBindVertexArray(cube_vao_);

        for (int y = 0; y < level_.h; ++y) {
            for (int x = 0; x < level_.w; ++x) {
                auto cell = level_.cells[y * level_.w + x];

                if (cell == io::Cell::NextLevel || cell == io::Cell::Exit) {
                    // Promieñ œwiat³a (tylko dla Exit)
                    if (cell == io::Cell::Exit) {
                        world_shader_.setInt("uUseTex", 0);
                        world_shader_.setVec4("uColor", 1.0f, 0.8f, 0.0f, 0.4f); // Z³oty transparentny
                        glDepthMask(GL_FALSE);
                        glm::mat4 M_beam(1.0f);
                        M_beam = glm::translate(M_beam, glm::vec3(x + 0.5f, 2.0f, y + 0.5f));
                        M_beam = glm::scale(M_beam, glm::vec3(0.1f, 4.0f, 0.1f));
                        world_shader_.setMat4("uModel", &M_beam[0][0]);
                        glBindVertexArray(cube_vao_);
                        glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
                        glDepthMask(GL_TRUE);
                    }

                    // Model Portalu
                    if (cell == io::Cell::NextLevel) world_shader_.setVec4("uColor", 0.5f, 0.8f, 1.0f, 1.0f);
                    else world_shader_.setVec4("uColor", 1.0f, 0.8f, 0.2f, 1.0f);

                    glm::mat4 M(1.0f);
                    M = glm::translate(M, glm::vec3(x + 0.5f, 0.55f, y + 0.5f));

                    if (portal_vertex_count_ > 0) {
                        float scale = 0.4f;
                        M = glm::scale(M, glm::vec3(scale));
                        world_shader_.setMat4("uModel", &M[0][0]);
                        world_shader_.setInt("uUseTex", 1);
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, portal_texture_);
                        world_shader_.setInt("uTex", 0);
                        glBindVertexArray(portal_vao_);
                        glDrawArrays(GL_TRIANGLES, 0, portal_vertex_count_);
                        glBindVertexArray(cube_vao_);
                    }
                    else {
                        // Fallback cube
                        M = glm::scale(M, glm::vec3(0.8f, 0.05f, 0.8f));
                        world_shader_.setMat4("uModel", &M[0][0]);
                        world_shader_.setInt("uUseTex", 0);
                        glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
                    }
                }
            }
        }
        glBindVertexArray(0);

        // --- 5. POCHODNIE ---
        for (const auto& torch : puzzle_torches_) {
            if (torch.is_lit) world_shader_.setVec4("uColor", 1.5f, 1.2f, 0.8f, 1.0f);
            else world_shader_.setVec4("uColor", 0.3f, 0.3f, 0.3f, 1.0f);

            glm::mat4 M(1.0f);
            M = glm::translate(M, glm::vec3(torch.x + 0.5f, 1.0f, torch.y + 1.0f));
            M = glm::rotate(M, glm::radians(30.0f), glm::vec3(1, 0, 0));

            if (torch_vertex_count_ > 0) {
                float scale = 0.5f;
                M = glm::scale(M, glm::vec3(scale));
                world_shader_.setMat4("uModel", &M[0][0]);
                world_shader_.setInt("uUseTex", 1);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, torch_texture_);
                world_shader_.setInt("uTex", 0);
                glBindVertexArray(torch_vao_);
                glDrawArrays(GL_TRIANGLES, 0, torch_vertex_count_);
            }
            else {
                M = glm::scale(M, glm::vec3(0.15f, 0.6f, 0.15f));
                world_shader_.setMat4("uModel", &M[0][0]);
                world_shader_.setInt("uUseTex", 0);
                glBindVertexArray(cube_vao_);
                glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
            }
        }

        // --- 6. P£YTY ---
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

        // --- 7. WROGOWIE ---
        for (auto* enemy : enemies_) {
            enemy->UpdateDeath(dt);
            enemy->UpdateAnimation(dt);
            if (!enemy->IsAlive() && enemy->deathAnimFinished) continue;

            world_shader_.use();
            world_shader_.setInt("uUseTex", 1);
            glActiveTexture(GL_TEXTURE0);

            GLuint currentVAO = 0;
            int currentCount = 0;
            float scale = 1.0f;

            // Wybór modelu i tekstury
            if (enemy->name == "Zombie") {
                glBindTexture(GL_TEXTURE_2D, zombie_texture_);
                currentVAO = zombie_vao_; currentCount = zombie_vertex_count_; scale = 0.40f;
            }
            else if (enemy->name == "Skeleton") {
                glBindTexture(GL_TEXTURE_2D, skeleton_texture_);
                currentVAO = skeleton_vao_; currentCount = skeleton_vertex_count_; scale = 0.14f;
            }
            else {
                glBindTexture(GL_TEXTURE_2D, zombie_texture_);
                currentVAO = zombie_vao_; currentCount = zombie_vertex_count_; scale = 0.3f;
            }

            world_shader_.setInt("uTex", 0);
            float y_offset = 0.55f;

            if (!enemy->IsAlive()) {
                float alpha = 1.0f - enemy->deathTimer;
                if (alpha < 0.0f) alpha = 0.0f;
                y_offset -= (enemy->deathTimer * 0.5f);
                world_shader_.setVec4("uColor", 0.5f, 0.5f, 0.5f, alpha);
            }
            else if (enemy->IsHurt()) {
                world_shader_.setVec4("uColor", 2.0f, 2.0f, 2.0f, 1.0f);
            }
            else {
                world_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 1.0f);
            }

            float x = enemy->VisualPos.x;
            float z = enemy->VisualPos.z;
            glm::mat4 M(1.0f);
            M = glm::translate(M, glm::vec3(x, y_offset, z));
            M = glm::rotate(M, glm::radians((float)enemy->yaw), glm::vec3(0, 1, 0));
            if (!enemy->IsAlive()) {
                float deathAngle = -90.0f * enemy->deathTimer;
                if (deathAngle < -90.0f) deathAngle = -90.0f;
                M = glm::rotate(M, glm::radians(deathAngle), glm::vec3(1, 0, 0));
            }
            M = glm::scale(M, glm::vec3(scale));
            world_shader_.setMat4("uModel", &M[0][0]);

            if (currentCount > 0) {
                glBindVertexArray(currentVAO);
                glDrawArrays(GL_TRIANGLES, 0, currentCount);
            }
            else {
                glBindVertexArray(cube_vao_);
                world_shader_.setInt("uUseTex", 0);
                glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
            }

            // Pasek HP nad g³ow¹
            if (enemy->IsAlive() && enemy->health < enemy->maxHealth) {
                glBindVertexArray(cube_vao_);
                world_shader_.setInt("uUseTex", 0);
                world_shader_.setVec4("uColor", 0.5f, 0.0f, 0.0f, 1.0f);
                glm::mat4 M_bg(1.0f);
                M_bg = glm::translate(M_bg, glm::vec3(x, 1.8f, z));
                M_bg = glm::scale(M_bg, glm::vec3(0.6f, 0.05f, 0.05f));
                world_shader_.setMat4("uModel", &M_bg[0][0]);
                glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);

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

        // --- 8. PRZEDMIOTY (DROP) ---
        world_shader_.use();
        world_shader_.setInt("uUseTex", 1);

        for (const auto& wItem : world_items_) {
            if (!wItem.isAlive) continue;
            glm::mat4 M(1.0f);

            if (wItem.itemData->type == ItemType::Weapon) {
                glm::vec3 pos = wItem.position;
                pos.y = 0.9f;
                M = glm::translate(M, pos);
                M = glm::rotate(M, glm::radians(180.0f), glm::vec3(1, 0, 0));
                M = glm::rotate(M, glm::radians(15.0f), glm::vec3(0, 0, 1));
                M = glm::rotate(M, glm::radians(180.0f), glm::vec3(0, 1, 0));
                float scale = 0.5f;
                M = glm::scale(M, glm::vec3(scale));

                world_shader_.setMat4("uModel", &M[0][0]);
                std::string name = wItem.itemData->name;

                // Color coding dla broni
                if (name == "MAXWELL")          world_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 0.9f);
                else if (name == "Rusty Sword") world_shader_.setVec4("uColor", 0.6f, 0.4f, 0.2f, 0.9f);
                else if (name == "Iron Sword")  world_shader_.setVec4("uColor", 0.8f, 0.9f, 1.0f, 0.9f);
                else if (name == "GOD SLAYER")  world_shader_.setVec4("uColor", 1.0f, 0.8f, 0.0f, 0.9f);
                else                            world_shader_.setVec4("uColor", 0.5f, 0.5f, 0.5f, 0.9f);

                world_shader_.setInt("uUseTex", 1);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, weapon_texture_);
                world_shader_.setInt("uTex", 0);
                if (weapon_vertex_count_ > 0) {
                    glDepthMask(GL_FALSE);
                    glBindVertexArray(weapon_vao_);
                    glDrawArrays(GL_TRIANGLES, 0, weapon_vertex_count_);
                    glDepthMask(GL_TRUE);
                }
            }
            else {
                // Mikstury (Rotacja)
                float time = (float)glfwGetTime();
                float floatY = 0.55f + sin(time * 3.0f) * 0.03f;
                glm::vec3 pos = wItem.position;
                pos.y = floatY;
                M = glm::translate(M, pos);
                M = glm::rotate(M, time, glm::vec3(0, 1, 0));
                M = glm::scale(M, glm::vec3(0.5f));
                world_shader_.setMat4("uModel", &M[0][0]);
                world_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 1.0f);

                glActiveTexture(GL_TEXTURE0);
                if (potion_texture_ != 0) {
                    glBindTexture(GL_TEXTURE_2D, potion_texture_);
                    world_shader_.setInt("uUseTex", 1);
                }
                else {
                    world_shader_.setInt("uUseTex", 0);
                    world_shader_.setVec4("uColor", 1.0f, 0.2f, 0.2f, 1.0f);
                }
                world_shader_.setInt("uTex", 0);

                if (potion_vertex_count_ > 0) {
                    glBindVertexArray(potion_vao_);
                    glDrawArrays(GL_TRIANGLES, 0, potion_vertex_count_);
                }
                else {
                    world_shader_.setInt("uUseTex", 0);
                    world_shader_.setVec4("uColor", 0.0f, 0.0f, 1.0f, 1.0f);
                    glBindVertexArray(cube_vao_);
                    glDrawArrays(GL_TRIANGLES, 0, cube_vertex_count_);
                }
            }
        }
        glBindVertexArray(0);

        // --- 9. BROÑ W RÊCE (FPP) ---
        if (state_ == GameState::Playing && camera_mode_ == CameraMode::FirstPerson && has_held_item_) {
            float rad = glm::radians((float)player_.yaw);
            glm::vec3 forward(std::sin(rad), 0.0f, -std::cos(rad));
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            glm::vec3 rightv = glm::normalize(glm::cross(forward, up));
            glm::vec3 item_pos = cam_pos + forward * 0.4f + rightv * 0.2f + up * -0.3f;

            // Animacja "bujania" broni¹
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
                item_pos.y += std::sin(time * 2.0f) * 0.02f;
            }
            item_pos += forward * animOffset;
            item_pos += up * (-animOffset * 0.2f);

            world_shader_.use();
            world_shader_.setInt("uUseTex", 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, weapon_texture_);
            world_shader_.setInt("uTex", 0);

            // Kolor broni trzymanej
            if (player_.equippedWeapon) {
                std::string name = player_.equippedWeapon->name;
                if (name == "MAXWELL")          world_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 0.9f);
                else if (name == "Rusty Sword") world_shader_.setVec4("uColor", 0.6f, 0.4f, 0.2f, 0.9f);
                else if (name == "Iron Sword")  world_shader_.setVec4("uColor", 0.8f, 0.9f, 1.0f, 0.9f);
                else if (name == "GOD SLAYER")  world_shader_.setVec4("uColor", 1.0f, 0.8f, 0.0f, 0.9f);
                else world_shader_.setVec4("uColor", 0.7f, 0.7f, 0.7f, 1.0f);
            }

            glm::mat4 M(1.0f);
            M = glm::translate(M, item_pos);
            M = glm::rotate(M, glm::radians((float)player_.yaw), glm::vec3(0, 1, 0));
            M = glm::rotate(M, glm::radians(180.0f), glm::vec3(0, 1, 0));
            M = glm::rotate(M, glm::radians(animTilt), glm::vec3(1, 0, 0));
            float scale = 0.4f;
            M = glm::scale(M, glm::vec3(scale));
            world_shader_.setMat4("uModel", &M[0][0]);

            if (weapon_vertex_count_ > 0) {
                glBindVertexArray(weapon_vao_);
                glDrawArrays(GL_TRIANGLES, 0, weapon_vertex_count_);
                glBindVertexArray(0);
            }
            glm::mat4 I(1.0f);
            world_shader_.setMat4("uModel", &I[0][0]);
        }
    }

} // namespace dungeon