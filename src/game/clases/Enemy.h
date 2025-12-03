//
// Created by wdzik on 29.11.2025.
//

#ifndef GRA_DUNGEON_CRAWLER_ENEMY_H
#define GRA_DUNGEON_CRAWLER_ENEMY_H
#include "Entity.h"


class Enemy : public Entity {
public:
    Enemy(int x, int y, int yaw,
          int hp, int maxHp,
          int ap, float damage,
          const std::string& name)
        : Entity(x, y, yaw, hp, maxHp, ap, damage, true, name) {}

    virtual void Skill1() {}
    virtual void Skill2() {}
    virtual void Skill3() {}
};

#endif //GRA_DUNGEON_CRAWLER_ENEMY_H