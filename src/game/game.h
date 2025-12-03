//
// Created by wdzik on 29.11.2025.
//

#ifndef GRA_DUNGEON_CRAWLER_GAME_H
#define GRA_DUNGEON_CRAWLER_GAME_H

#include "clases/Player.h"
#include "clases/Enemy.h"
#include "GameData.h"
#include <vector>
#include "clases/Player.h"
class Game {
public:
    Game();
    ~Game();

    void Update();

    void PlayerMoveForward();
    void PlayerMoveBackward();
    void PlayerMoveLeft();
    void PlayerMoveRight();
    void PlayerTurnLeft();
    void PlayerTurnRight();
    void PlayerAttack();
    void PlayerUseSkill(const Skill& skill);

    void DebugPrint();

private:
    Player* player;
    std::vector<Enemy*> enemies;

    int map[MAP_HEIGHT_MAX][MAP_WIDTH_MAX];

    void LoadMap();
    void SpawnEnemy(const EnemyTemplate& tpl, int x, int y);
    void OnEnemyKilled(Enemy* e);
};

#endif

#endif //GRA_DUNGEON_CRAWLER_GAME_H