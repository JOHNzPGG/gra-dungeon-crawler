//
// Created by wdzik on 29.11.2025.
//

class Enemy : public Entity {
public:
    std::vector<Item> drops;
    int XPValue = 20;

    Enemy(int x, int y, int yaw,
          int hp, int maxHp,
          int ap, float damage,
          const std::string& name,
          std::initializer_list<Item> dropList = {},
          int xpValue = 20)
        : Entity(x, y, yaw, hp, maxHp, ap, damage, true, name),
          drops(dropList),
          XPValue(xpValue)
    {}

    // Zwraca dropy do świata gry
    std::vector<Item> OnDeath(Player& killer) {
        killer.AddXP(XPValue);
        return drops;
    }
};


#endif //GRA_DUNGEON_CRAWLER_ENEMY_H