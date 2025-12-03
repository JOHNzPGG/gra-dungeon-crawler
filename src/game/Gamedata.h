//
// Created by wdzik on 02.12.2025.
//

#ifndef DUNGEON_GAMEDATA_H
#define DUNGEON_GAMEDATA_H
#ifndef GRA_DUNGEON_CRAWLER_GAMEDATA_H
#define GRA_DUNGEON_CRAWLER_GAMEDATA_H

#include "./clases/Skill.h"
#include "./clases/Item.h"
#include "./clases/Player.h"
#include "./clases/Enemy.h"
#include <vector>

namespace Skills {

inline Skill swordStrike("Sword Strike", 1, 10);
inline Skill fireBreath("Fire Breath", 2, 15);
inline Skill heal("Heal", 1, 10, false);
inline Skill crossSlash("Cross Slash", 2, 12);
inline Skill lightning("Lightning Bolt", 2, 20);

inline void Init() {
    swordStrike.offsets = { {0,1} };
    fireBreath.offsets = { {0,1},{-1,2},{0,2},{1,2},{-1,3},{0,3},{1,3} };
    heal.offsets = { {-1,-1},{0,-1},{1,-1},{-1,0},{0,0},{1,0},{-1,1},{0,1},{1,1} };
    crossSlash.offsets = { {0,1}, {0,-1}, {1,0}, {-1,0} };
    lightning.offsets = { {0,1}, {0,2}, {0,3} };
}

}

namespace Items {
inline Item sword("Iron Sword", ItemType::Weapon, false, {0,5,0});
inline Item armor("Leather Armor", ItemType::Armor, false, {20,0,0});
inline Item potion("Health Potion", ItemType::Consumable, true, {15,0,0});
inline Item amulet("Amulet of Action", ItemType::Accessory, false, {0,0,1});

inline std::vector<Item*> AllItems = { &sword, &armor, &potion, &amulet };

}

namespace Characters {

inline Player hero(3,4);

inline Enemy goblin(5,5, 0, 30, 30, 2, 5, true, "Goblin");
inline Enemy orc(7,7, 180, 50, 50, 2, 8, true, "Orc");
inline Enemy mage(10,3, 90, 25, 25, 3, 6, true, "Mage");

inline void Init() {
    hero.skills.push_back(&Skills::swordStrike);
    hero.skills.push_back(&Skills::fireBreath);
    hero.skills.push_back(&Skills::heal);

}

}

#endif //GRA_DUNGEON_CRAWLER_GAMEDATA_H

#endif //DUNGEON_GAMEDATA_H