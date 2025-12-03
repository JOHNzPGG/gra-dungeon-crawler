//
// Created by wdzik on 02.12.2025.
//

#ifndef DUNGEON_GAMEDATA_H
#define DUNGEON_GAMEDATA_H

#include "./clases/Skill.h"
#include "./clases/Item.h"
#include "./clases/Player.h"
#include "./clases/Enemy.h"
#include <vector>

namespace GameData {

// ------------------- SKILLS -------------------
namespace Skills {

inline Skill swordStrike("Sword Strike", 1, 10);
inline Skill fireBreath("Fire Breath", 2, 15);
inline Skill heal("Heal", 1, 10, false);
inline Skill crossSlash("Cross Slash", 2, 12);
inline Skill lightning("Lightning Bolt", 2, 20);

inline void Init() {
    // Offsety określają pola względem gracza.
    // Wyznaczanie offsetów:
    //  dx = przesunięcie X; dodatnie = prawo
    //  dy = przesunięcie Y; dodatnie = przód (według yaw)
    swordStrike.offsets = { {0,1} };
    fireBreath.offsets = { {0,1},{-1,2},{0,2},{1,2},{-1,3},{0,3},{1,3} };
    heal.offsets = { {-1,-1},{0,-1},{1,-1},{-1,0},{0,0},{1,0},{-1,1},{0,1},{1,1} };
    crossSlash.offsets = { {0,1}, {0,-1}, {1,0}, {-1,0} };
    lightning.offsets = { {0,1}, {0,2}, {0,3} };
}

} // namespace Skills

// ------------------- ITEMS -------------------
namespace Items {

inline Item sword("Iron Sword", ItemType::Weapon, false, {0, 5, 0});
inline Item armor("Leather Armor", ItemType::Armor, false, {20, 0, 0});
inline Item potion("Health Potion", ItemType::Consumable, true, {15, 0, 0});
inline Item amulet("Amulet of Action", ItemType::Accessory, false, {0, 0, 1});

inline std::vector<Item*> AllItems = {
    &sword,
    &armor,
    &potion,
    &amulet
};

} // namespace Items

// ------------------- ENEMY TEMPLATE -------------------
struct EnemyTemplate {
    std::string name;
    int maxHp;
    int ap;
    float damage;
    bool isHostile;

    std::vector<Item*> drops; // wskaźniki na itemy z Items::
};

// ------------------- CHARACTER DATA -------------------
namespace Characters {

inline EnemyTemplate GoblinTemplate{
    "Goblin",
    20,
    4,
    3.0f,
    true,
    { Items::potion }  // goblin dropi potion
};

inline EnemyTemplate SkeletonTemplate{
    "Skeleton",
    35,
    5,
    4.0f,
    true,
    { Items::armor, Items::potion }
};

inline EnemyTemplate OrcTemplate{
    "Orc",
    60,
    6,
    8.0f,
    true,
    { Items::sword }
};

struct PlayerTemplate {
    int maxHp;
    int ap;
    float damage;
    std::string name;
};

inline PlayerTemplate DefaultPlayer{
    50,
    6,
    5.0f,
    "Hero"
};

} // namespace Characters

} // namespace GameData

#endif // DUNGEON_GAMEDATA_H
