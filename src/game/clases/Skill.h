//
// Created by wdzik on 02.12.2025.
//

#ifndef DUNGEON_SKILL_H
#define DUNGEON_SKILL_H
#include <string>
#include <vector>

#include "Entity.h"

class Skill {
public:
    std::string name;
    int apCost;
    int damage;
    bool isOffensive;

    // offsets = lista kafelków względem postaci (0,0)
    // Każdy offset to (dx, dy):
    // dx > 0 -> w prawo, dx < 0 -> w lewo
    // dy > 0 -> do przodu, dy < 0 -> do tyłu
    std::vector<glm::ivec2> offsets;

    Skill(const std::string& n, int ap, int dmg, bool offensive = true)
        : name(n), apCost(ap), damage(dmg), isOffensive(offensive) {}

    void Use(Entity* caster, const std::vector<Entity*>& allEntities) {
        if (!caster || caster->ActionPoints < apCost) return;

        caster->UseActionPoints(apCost);

        for (auto& offset : offsets) {
            glm::ivec2 rotated = RotateOffset(offset, caster->yaw);

            int tx = caster->GameX + rotated.x;
            int ty = caster->GameY + rotated.y;

            for (auto& e : allEntities) {
                if (e->GameX == tx && e->GameY == ty && e->IsAlive()) {
                    if (isOffensive) e->TakeDamage(damage);
                    else e->Heal(damage);
                }
            }
        }
    }

private:
    glm::ivec2 RotateOffset(glm::ivec2 offset, int yawDegrees) {
        float rad = yawDegrees * 3.14159265f / 180.0f;
        int x = round(offset.x * cos(rad) - offset.y * sin(rad));
        int y = round(offset.x * sin(rad) + offset.y * cos(rad));
        return glm::ivec2(x, y);
    }
};




#endif //DUNGEON_SKILL_H