#ifndef GRA_DUNGEON_CRAWLER_PLAYER_H
#define GRA_DUNGEON_CRAWLER_PLAYER_H
#include <iostream>
#include <vector>

#include "Entity.h"
#include "Item.h"
#include "skill.h"

/**
 * @class Player
 * @brief Reprezentuje postaæ sterowan¹ przez u¿ytkownika.
 * @details Dziedziczy po klasie Entity. Rozszerza j¹ o system ekwipunku (broñ, zbroja),
 * plecak (inventory) oraz system umiejêtnoœci (Skills).
 */
class Player : public Entity {
public:
    Item* equippedWeapon = nullptr; ///< Aktualnie trzymana broñ
    Item* equippedArmor = nullptr;  ///< Aktualnie za³o¿ona zbroja (opcjonalne)

    std::vector<Item*> inventory;   ///< Lista przedmiotów w plecaku

    /**
     * @brief Tworzy gracza w zadanej pozycji.
     * @param x Pozycja X na siatce.
     * @param y Pozycja Y na siatce.
     * @param yaw Pocz¹tkowy k¹t obrotu.
     */
    Player(int x, int y, int yaw)
        : Entity(x, y, yaw, 100, 100, 2, 10, true, "Player") {
    }

    std::vector<Skill*> skills; ///< Lista nauczonych umiejêtnoœci

    /**
     * @brief Dodaje now¹ umiejêtnoœæ do listy.
     */
    void LearnSkill(Skill* skill) {
        if (skill) skills.push_back(skill);
    }

    /**
     * @brief U¿ywa umiejêtnoœci o danym indeksie na grupie celów.
     */
    void UseSkill(int index, const std::vector<Entity*>& targets) {
        if (index < 0 || index >= skills.size()) return;
        skills[index]->Use(this, targets);
    }

    /**
     * @brief Zak³ada przedmiot i aplikuje jego statystyki.
     * * Jeœli slot jest zajêty, najpierw zdejmuje stary przedmiot.
     * Aktualizuje statystyki (HP, DMG, AP) na podstawie przedmiotu.
     * @param item WskaŸnik na przedmiot do za³o¿enia.
     */
    void Equip(Item* item) {
        if (!item) return;

        if (item->type == ItemType::Weapon && equippedWeapon) {
            Unequip(equippedWeapon);
        }
        if (item->type == ItemType::Armor && equippedArmor) {
            Unequip(equippedArmor);
        }

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

    /**
     * @brief Dodaje przedmiot do plecaka (bez zak³adania).
     */
    void AddToInventory(Item* item) {
        inventory.push_back(item);
    }

    /**
     * @brief Modyfikuje statystyki gracza o wartoœci przedmiotu.
     */
    void ApplyItemStats(Item* item) {
        if (!item) return;
        maxHealth += item->stats.maxHealth;
        health += item->stats.health;
        base_damage += item->stats.damage;
        ActionPoints += item->stats.actionPoints;
    }

    /**
     * @brief Zdejmuje przedmiot i cofa jego bonusy do statystyk.
     */
    void Unequip(Item* item) {
        if (!item) return;

        maxHealth -= item->stats.maxHealth;
        if (health > maxHealth) health = maxHealth;

        base_damage -= item->stats.damage;
        ActionPoints -= item->stats.actionPoints;

        if (item == equippedWeapon) equippedWeapon = nullptr;
        if (item == equippedArmor)  equippedArmor = nullptr;
    }
};

#endif //GRA_DUNGEON_CRAWLER_PLAYER_H