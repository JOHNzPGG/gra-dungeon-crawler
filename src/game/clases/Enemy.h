#ifndef GRA_DUNGEON_CRAWLER_ENEMY_H
#define GRA_DUNGEON_CRAWLER_ENEMY_H

#include "Entity.h"
#include "dungeon/io/MapLoader.hpp" // Potrzebujemy definicji Level
#include <glm/glm.hpp>
#include <glm/gtx/vector_angle.hpp> // Do obliczania k¹tów
#include <cmath>

class Enemy : public Entity {
public:
    Enemy(int x, int y, int yaw,
        int hp, int maxHp,
        int ap, float damage,
        const std::string& name)
        : Entity(x, y, yaw, hp, maxHp, ap, damage, true, name) {
    }

    // --- MÓZG PRZECIWNIKA ---
    // Funkcja wywo³ywana raz na turê przeciwnika
    void TakeTurn(Entity* target, const dungeon::io::Level& level) {
        if (!IsAlive()) return;

        // 1. SprawdŸ czy widzê gracza
        bool seesTarget = CanSee(target, level);

        if (seesTarget) {
            // A. Jeœli jestem blisko -> Atakuj
            float dist = glm::distance(glm::vec2(GameX, GameY), glm::vec2(target->GameX, target->GameY));
            if (dist <= 1.5f) { // 1.5 wystarczy by z³apaæ przek¹tn¹ (1.41)
                RotateTowards(target->GameX, target->GameY);
                Attack(target);
            }
            // B. Jeœli jestem daleko -> Goñ
            else {
                MoveTowards(target->GameX, target->GameY, level);

                // Po ruchu sprawdŸ, czy teraz mogê zaatakowaæ (jeœli podszed³em)
                dist = glm::distance(glm::vec2(GameX, GameY), glm::vec2(target->GameX, target->GameY));
                if (dist <= 1.5f) {
                    Attack(target);
                }
            }
        }
        else {
            // C. Nie widzê gracza -> IDLE (stój lub losowy obrót)
            // Opcjonalnie: Patroluj
        }
    }

    // --- ZMYS£Y ---
    bool CanSee(Entity* target, const dungeon::io::Level& level) {
        // 1. Dystans
        float dist = glm::distance(glm::vec2(GameX, GameY), glm::vec2(target->GameX, target->GameY));
        if (dist > 6.0f) return false;

        // 2. Kierunek (Dot product)
        glm::vec2 toTarget = glm::vec2(target->GameX - GameX, target->GameY - GameY);
        glm::vec2 dir = glm::vec2(orientation.x, orientation.z); // orientation z Object.h

        // Normalizacja
        if (glm::length(toTarget) > 0.1f) toTarget = glm::normalize(toTarget);
        if (glm::length(dir) > 0.1f) dir = glm::normalize(dir);

        float dot = glm::dot(dir, toTarget);
        // dot > 0.5 oznacza k¹t widzenia ok. 120 stopni (szeroki sto¿ek)
        // dot > 0.7 oznacza ok 90 stopni.
        if (dot < 0.5f) return false;

        // 3. Raycast (Œciany)
        return CheckLineOfSight(GameX, GameY, target->GameX, target->GameY, level);
    }

    // Funkcja pomocnicza - sprawdza czy miêdzy A i B s¹ œciany
    bool CheckLineOfSight(int x1, int y1, int x2, int y2, const dungeon::io::Level& level) {
        float steps = std::max(std::abs(x2 - x1), std::abs(y2 - y1));
        if (steps == 0) return true;

        float stepX = (x2 - x1) / steps;
        float stepY = (y2 - y1) / steps;

        float cx = (float)x1;
        float cy = (float)y1;

        for (int i = 0; i < (int)steps; ++i) {
            cx += stepX;
            cy += stepY;
            int checkX = (int)std::round(cx);
            int checkY = (int)std::round(cy);

            // Jeœli trafimy w cel, to OK
            if (checkX == x2 && checkY == y2) return true;

            // SprawdŸ kolizjê ze œcian¹
            if (checkX < 0 || checkY < 0 || checkX >= level.w || checkY >= level.h) return false;
            if (level.cells[checkY * level.w + checkX] == dungeon::io::Cell::Wall) return false;
        }
        return true;
    }

    // --- ANIMACJA RUCHU ---
    glm::vec3 VisualPos;       // Tutaj stoi model (p³ynne)
    glm::vec3 AnimStartPos;    // Sk¹d wyruszyliœmy
    glm::vec3 AnimTargetPos;   // Dok¹d idziemy (cel animacji)
    float AnimTimer = 0.0f;
    bool IsMoving = false;
    const float AnimDuration = 0.15f; // Czas trwania (bardzo szybki suw)

    // Funkcja do aktualizacji animacji (dodaj j¹ te¿ w .cpp lub zostaw w .h jak tutaj)
    void UpdateAnimation(float dt) {
        if (IsMoving) {
            AnimTimer += dt;
            float t = AnimTimer / AnimDuration;

            if (t >= 1.0f) {
                t = 1.0f;
                IsMoving = false;
                VisualPos = AnimTargetPos; // Doci¹gnij do celu
            }
            else {
                // Interpolacja (Lerp)
                VisualPos = AnimStartPos + (AnimTargetPos - AnimStartPos) * t;
            }
        }
    }

    // Funkcja startuj¹ca animacjê (wywo³amy j¹ po ruchu AI)
    void StartMoveAnimation(int oldX, int oldY, int newX, int newY) {
        // Logika 2D -> Wizualne 3D (pamiêtaj o +0.5f na œrodek kratki)
        AnimStartPos = glm::vec3(oldX + 0.5f, 0.0f, oldY + 0.5f);
        AnimTargetPos = glm::vec3(newX + 0.5f, 0.0f, newY + 0.5f);

        // Zabezpieczenie: Startujemy z obecnej wizualnej, ¿eby nie by³o skoku
        // jeœli poprzednia animacja siê nie skoñczy³a (choæ przy turach to rzadkie)
        VisualPos = AnimStartPos;

        IsMoving = true;
        AnimTimer = 0.0f;
    }

private:
    void RotateTowards(int tx, int ty) {
        if (ty < GameY) yaw = 0;
        else if (ty > GameY) yaw = 180;
        else if (tx > GameX) yaw = 90;
        else if (tx < GameX) yaw = 270;
        UpdateOrientation();
    }

    void MoveTowards(int tx, int ty, const dungeon::io::Level& level) {
        int dx = (tx > GameX) ? 1 : (tx < GameX) ? -1 : 0;
        int dy = (ty > GameY) ? 1 : (ty < GameY) ? -1 : 0;

        // Prosty ruch: najpierw próbuj jedn¹ oœ, potem drug¹
        int nextX = GameX + dx;
        int nextY = GameY;

        // Helper lokalny do sprawdzania ruchu
        auto isWalkable = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= level.w || y >= level.h) return false;
            return level.cells[y * level.w + x] != dungeon::io::Cell::Wall;
            };

        // Preferuj oœ z wiêkszym dystansem
        if (std::abs(tx - GameX) < std::abs(ty - GameY)) {
            nextX = GameX; nextY = GameY + dy;
        }

        if (!isWalkable(nextX, nextY)) {
            // Zablokowane? Spróbuj drug¹ oœ
            nextX = GameX + dx;
            nextY = GameY + dy;
            // (Tu mo¿na dodaæ lepszy pathfinding A*, ale na razie wystarczy)
        }

        if (isWalkable(nextX, nextY)) {
            // Rusz siê
            RotateTowards(nextX, nextY); // Patrz gdzie idziesz
            GameX = nextX;
            GameY = nextY;
            RenderPosition = glm::vec3(GameX, 0.f, GameY);
        }
    }
};

#endif