//
// Created by wdzik on 29.11.2025.
//

#ifndef GRA_DUNGEON_CRAWLER_PLAYER_H
#define GRA_DUNGEON_CRAWLER_PLAYER_H
#include <iostream>
#include <vector>

#include "Entity.h"
#include "Item.h"
#include "skill.h"


class Player : public Entity {
public:
    Item* equippedWeapon = nullptr;
    Item* equippedArmor = nullptr;
    Player(int x, int y, int yaw)
        : Entity(x, y, yaw, 100, 100, 2, 10, true, "Player") {}
    std::vector<Skill*> skills;

    void LearnSkill(Skill* skill) {
        if (skill) skills.push_back(skill);
    }

    void UseSkill(int index, const std::vector<Entity*>& targets) {
        if (index < 0 || index >= skills.size()) return;
        skills[index]->Use(this, targets);
    }


    void Equip(Item* item) {
        if (!item) return;

        switch (item->type) {
            case ItemType::Weapon:
                equippedWeapon = item;
                ApplyItemStats(item);
                break;
            case ItemType::Armor:
                equippedArmor = item;
                ApplyItemStats(item);
                break;
            default:
                break;
        }
    }
    void ApplyItemStats(Item* item) {
        if (!item) return;

        maxHealth += item->stats.maxHealth;
        health += item->stats.health;
        base_damage += item->stats.damage;
        ActionPoints += item->stats.actionPoints;
    }
    void Unequip(Item* item) {
        if (!item) return;

        maxHealth -= item->stats.maxHealth;
        health = std::min(health, maxHealth);
        base_damage -= item->stats.damage;
        ActionPoints -= item->stats.actionPoints;

        if (item == equippedWeapon) equippedWeapon = nullptr;
        if (item == equippedArmor)  equippedArmor = nullptr;
    }

};



#endif //GRA_DUNGEON_CRAWLER_PLAYER_H