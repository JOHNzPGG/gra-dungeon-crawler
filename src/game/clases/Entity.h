#ifndef GRA_DUNGEON_CRAWLER_ENTITY_H
#define GRA_DUNGEON_CRAWLER_ENTITY_H

#include "Object.h"
#include <GLFW/glfw3.h>
#define MAP_WIDTH_MAX  20
#define MAP_HEIGHT_MAX 20

class Entity : public Object {
public:
    int maxHealth;
    int health;
    int ActionPoints;
    int base_damage;
    bool colision = true;
    float lastHitTime = -100.0f; // Czas ostatniego otrzymania obra¿eñ
    float deathTimer = 0.0f;       // Ile czasu minê³o od œmierci
    bool deathAnimFinished = false; // Czy animacja siê zakoñczy³a?


    Entity(int x, int y, int yawAngle,
           int hp, int maxHp,
           int ap, int damage,
           bool hasCollision = true,
           const std::string& entityName = "")
        : Object(x, y, yawAngle),
          health(hp),
          maxHealth(maxHp),
          ActionPoints(ap),
          base_damage(damage),
          colision(hasCollision)
    {
        name = entityName;
        RenderPosition = glm::vec3(GameX, 0, GameY);
    }
    glm::ivec2 GetMoveTarget(int offsetSteps = 0) const {
        int dirIndex = (GetDirIndex() + offsetSteps) % 8;
        return {
            GameX + DIR8[dirIndex].x,
            GameY + DIR8[dirIndex].y
        };
    }

    glm::ivec2 GetForwardTile() const  { return GetMoveTarget(0); }
    glm::ivec2 GetRightTile() const    { return GetMoveTarget(2); }
    glm::ivec2 GetBackTile() const     { return GetMoveTarget(4); }
    glm::ivec2 GetLeftTile() const     { return GetMoveTarget(6); }

    void TakeDamage(int amount) {
        health -= amount;
        if (health < 0) health = 0;
        lastHitTime = (float)glfwGetTime(); // Zapamiêtaj czas uderzenia!
    }

    bool IsHurt() const {
        return ((float)glfwGetTime() - lastHitTime) < 0.2f;
    }

    void Heal(int amount) {
        if (IsAlive()) {
            health += amount;
            if (health > maxHealth) health = maxHealth;
        }
    }

    bool IsAlive() const { return health > 0; }

    void UpdateDeath(float dt) {
        if (!IsAlive()) {
            deathTimer += dt;
            if (deathTimer > 1.0f) { // Animacja trwa 1 sekundê
                deathAnimFinished = true;
            }
        }
    }

    bool UseActionPoints(int ap_used=1) {
        if (ActionPoints-ap_used >= 0) {
            ActionPoints-=ap_used;
            return true;
        }
        return false;
    }
    void ResetActionPoints(int ap) { ActionPoints = ap; }

    bool  Attack(Entity* target) {
        if (!target || !UseActionPoints(1)) return false;

        glm::ivec2 front = GetForwardTile();
        if (target->GameX == front.x && target->GameY == front.y) {
            target->TakeDamage(base_damage);
            return true;
        }
        return false;
    }

    void TurnLeft()  { yaw = (yaw - 90 + 360) % 360; UpdateOrientation(); }
    void TurnRight() { yaw = (yaw + 90) % 360; UpdateOrientation(); }
private:
    void MoveInDirection(int map[MAP_HEIGHT_MAX][MAP_WIDTH_MAX], int offsetSteps) {
        glm::ivec2 target = GetTileInDirection(offsetSteps);
        int newX = target.x;
        int newY = target.y;

        if (newX >= 0 && newX < MAP_WIDTH_MAX &&
            newY >= 0 && newY < MAP_HEIGHT_MAX &&
            map[newY][newX] == 0)
        {
            GameX = newX;
            GameY = newY;
            RenderPosition = glm::vec3(GameX, 0, GameY);
        }
    }

};

#endif //GRA_DUNGEON_CRAWLER_ENTITY_H
