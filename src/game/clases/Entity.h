#ifndef GRA_DUNGEON_CRAWLER_ENTITY_H
#define GRA_DUNGEON_CRAWLER_ENTITY_H

#include "Object.h"
#include <GLFW/glfw3.h>
#define MAP_WIDTH_MAX  20
#define MAP_HEIGHT_MAX 20

/**
 * @class Entity
 * @brief Klasa bazowa dla wszystkich ¿ywych obiektów w grze (Gracz, Wrogowie).
 * * Odpowiada za:
 * - Zarz¹dzanie ¿yciem (HP) i œmierci¹
 * - System Punktów Akcji (Action Points)
 * - Pozycjonowanie na siatce (Grid)
 * - Odbieranie obra¿eñ i animacjê "Hurt"
 */
class Entity : public Object {
public:
    int maxHealth;
    int health;
    int ActionPoints;
    int base_damage;
    bool colision = true;

    // --- ZMIENNE ANIMACJI ---
    float lastHitTime = -100.0f;    ///< Znacznik czasu ostatniego otrzymania obra¿eñ (do migania na czerwono)
    float deathTimer = 0.0f;        ///< Licznik czasu animacji œmierci
    bool deathAnimFinished = false; ///< Czy animacja œmierci dobieg³a koñca?

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

    // --- LOGIKA SIATKI (GRID) ---

    /**
     * @brief Oblicza wspó³rzêdne s¹siedniego pola w danym kierunku.
     * @param offsetSteps Przesuniêcie w tablicy kierunków (np. 0=przód, 2=prawo, 4=ty³).
     */
    glm::ivec2 GetMoveTarget(int offsetSteps = 0) const {
        int dirIndex = (GetDirIndex() + offsetSteps) % 8;
        return {
            GameX + DIR8[dirIndex].x,
            GameY + DIR8[dirIndex].y
        };
    }

    glm::ivec2 GetForwardTile() const { return GetMoveTarget(0); }
    glm::ivec2 GetRightTile() const { return GetMoveTarget(2); }
    glm::ivec2 GetBackTile() const { return GetMoveTarget(4); }
    glm::ivec2 GetLeftTile() const { return GetMoveTarget(6); }

    /**
     * @brief Zadaje obra¿enia tej jednostce.
     * Rejestruje czas trafienia, aby wywo³aæ efekt wizualny (flash).
     */
    void TakeDamage(int amount) {
        health -= amount;
        if (health < 0) health = 0;
        lastHitTime = (float)glfwGetTime();
    }

    /**
     * @brief Sprawdza, czy jednostka jest w trakcie animacji otrzymywania obra¿eñ.
     * @return true jeœli minê³o mniej ni¿ 0.2s od uderzenia.
     */
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

    /**
     * @brief Aktualizuje licznik animacji œmierci.
     * @param dt Czas klatki (delta time).
     */
    void UpdateDeath(float dt) {
        if (!IsAlive()) {
            deathTimer += dt;
            if (deathTimer > 1.0f) { // Animacja trwa 1 sekundê
                deathAnimFinished = true;
            }
        }
    }

    /**
     * @brief Próbuje zu¿yæ punkty akcji.
     * @return true jeœli akcja jest mo¿liwa (mia³ wystarczaj¹co AP).
     */
    bool UseActionPoints(int ap_used = 1) {
        if (ActionPoints - ap_used >= 0) {
            ActionPoints -= ap_used;
            return true;
        }
        return false;
    }
    void ResetActionPoints(int ap) { ActionPoints = ap; }

    /**
     * @brief Atakuje cel stoj¹cy na wprost.
     */
    bool  Attack(Entity* target) {
        if (!target || !UseActionPoints(1)) return false;

        glm::ivec2 front = GetForwardTile();
        if (target->GameX == front.x && target->GameY == front.y) {
            target->TakeDamage(base_damage);
            return true;
        }
        return false;
    }

    void TurnLeft() { yaw = (yaw - 90 + 360) % 360; UpdateOrientation(); }
    void TurnRight() { yaw = (yaw + 90) % 360; UpdateOrientation(); }

private:
    // Prywatna metoda pomocnicza do ruchu (nieu¿ywana w obecnej logice App)
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