#ifndef GRA_DUNGEON_CRAWLER_ENTITY_H
#define GRA_DUNGEON_CRAWLER_ENTITY_H

#include "Object.h"
#define MAP_WIDTH_MAX  20
#define MAP_HEIGHT_MAX 20

class Entity : public Object {
public:
    int maxHealth;
    int health;
    int ActionPoints;
    int base_damage;
    bool colision = true;


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

    void MoveForward(int map[MAP_HEIGHT_MAX][MAP_WIDTH_MAX]) {
        MoveInDirection(map, 0);
    }

    void MoveBackward(int map[MAP_HEIGHT_MAX][MAP_WIDTH_MAX]) {
        MoveInDirection(map, 4);
    }

    void MoveRight(int map[MAP_HEIGHT_MAX][MAP_WIDTH_MAX]) {
        MoveInDirection(map, 2);
    }

    void MoveLeft(int map[MAP_HEIGHT_MAX][MAP_WIDTH_MAX]) {
        MoveInDirection(map, 6);
    }
    void TakeDamage(int dmg) {
        health -= dmg;
        if (health < 0) health = 0;
    }
    bool DealDamageTo(Entity* target, int dmg) {
        target->TakeDamage(dmg);
    }

    void Heal(int amount) {
        if (IsAlive()) {
            health += amount;
            if (health > maxHealth) health = maxHealth;
        }
    }

    bool IsAlive() const { return health > 0; }

    bool UseActionPoints(int ap_used=1) {
        if (ActionPoints-ap_used >= 0) {
            ActionPoints-=ap_used;
            return true;
        }
        return false;
    }
    void ResetActionPoints(int ap) { ActionPoints = ap; }

    void Attack(Entity* target) {
        UseActionPoints(1);
        glm::ivec2 frontTile = GetTileInDirection(0);
        if (target->GameX == frontTile.x && target->GameY == frontTile.y) {
            target->TakeDamage(base_damage);
        }
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
