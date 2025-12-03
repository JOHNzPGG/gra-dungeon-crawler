//
// Created by wdzik on 29.11.2025.
//

#ifndef GRA_DUNGEON_CRAWLER_ITEM_H
#define GRA_DUNGEON_CRAWLER_ITEM_H
#include <string>

#include "Entity.h"

enum class ItemType {
    Weapon,
    Armor,
    Consumable,
    Misc
};
struct ItemStats {
    int health = 0;
    int maxHealth = 0;
    int actionPoints = 0;
    int damage = 0;
};


class Item {
public:
    std::string name;
    ItemType type;
    bool isUsable;
    ItemStats stats;

    Item(const std::string& itemName,
         ItemType itemType,
         bool usable,
         const ItemStats& itemStats = {})
        : name(itemName), type(itemType), isUsable(usable), stats(itemStats) {}

    void Use(Entity* target) {
        if (!isUsable || !target) return;

        if (stats.health != 0) target->Heal(stats.health);
        if (stats.actionPoints != 0) target->ActionPoints += stats.actionPoints;
        if (stats.damage != 0) target->base_damage += stats.damage;
    }

};


#endif //GRA_DUNGEON_CRAWLER_ITEM_H