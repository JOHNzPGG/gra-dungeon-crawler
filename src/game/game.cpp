//
// Created by wdzik on 29.11.2025.
//

#include "game.h"
#include "GameData.h"
#include <iostream>
#include "clases/Player.h"
// ----------------------------------------------------
// Konstruktor
// ----------------------------------------------------
Game::Game()
{
    Skill::Init();
    //LoadMap();

    // 3) Utwórz gracza na podstawie PlayerTemplate
    auto& tpl = Characters::DefaultPlayer;
    player = new Player(
            1, 1,
            0,
            tpl.maxHp,
            tpl.maxHp,
            tpl.ap,
            tpl.damage,
            tpl.name
    );

    // 4) Stwórz kilka przeciwników z template’ów
    SpawnEnemy(Characters::GoblinTemplate, 5, 5);
    SpawnEnemy(Characters::GoblinTemplate, 6, 5);
    SpawnEnemy(Characters::SkeletonTemplate, 10, 3);
    SpawnEnemy(Characters::OrcTemplate, 12, 7);

    std::cout << "Game initialized.\n";
}

Game::~Game()
{
    delete player;

    for (Enemy* e : enemies)
        delete e;
}

// ----------------------------------------------------
// Spawning
// ----------------------------------------------------
void Game::SpawnEnemy(const EnemyTemplate& t, int x, int y)
{
    Enemy* e = new Enemy(
            x, y,
            0,                 // yaw
            t.maxHp,
            t.maxHp,
            t.ap,
            t.damage,
            t.name
    );

    // ustaw dropy (kopie pointerów)
    e->drops = t.drops;

    enemies.push_back(e);
}

// ----------------------------------------------------
// Map loading – placeholder
// ----------------------------------------------------
void Game::LoadMap()
{
    for (int y = 0; y < MAP_HEIGHT_MAX; y++)
        for (int x = 0; x < MAP_WIDTH_MAX; x++)
            map[y][x] = 0; // 0 = wolne pole
}

// ----------------------------------------------------
// Logika tury
// ----------------------------------------------------
void Game::Update()
{
    // ► Regeneracja AP gracza
    player->ResetActionPoints(Characters::DefaultPlayer.ap);

    // ► Reset AP u przeciwników
    for (auto* e : enemies)
        e->ResetActionPoints(e->ActionPoints);

    // ► Tu będzie AI (osobna klasa) – później dopiszesz
}

// ----------------------------------------------------
// Ruch gracza
// ----------------------------------------------------
void Game::PlayerMoveForward()
{
    if (player->UseActionPoints())
        player->MoveForward(map);
}
void Game::PlayerMoveBackward()
{
    if (player->UseActionPoints())
        player->MoveBackward(map);
}
void Game::PlayerMoveLeft()
{
    if (player->UseActionPoints())
        player->MoveLeft(map);
}
void Game::PlayerMoveRight()
{
    if (player->UseActionPoints())
        player->MoveRight(map);
}

void Game::PlayerTurnLeft()
{
    if (player->UseActionPoints())
        player->TurnLeft();
}
void Game::PlayerTurnRight()
{
    if (player->UseActionPoints())
        player->TurnRight();
}

// ----------------------------------------------------
// Atak (przód)
// ----------------------------------------------------
void Game::PlayerAttack()
{
    if (!player->UseActionPoints())
        return;

    glm::ivec2 front = player->GetTileInDirection(0);

    for (auto* e : enemies)
    {
        if (e->GameX == front.x && e->GameY == front.y)
        {
            player->Attack(e);
            if (!e->IsAlive())
                OnEnemyKilled(e);
            return;
        }
    }
}

// ----------------------------------------------------
// Skill używany na podstawie pointera do Skill
// ----------------------------------------------------
void Game::PlayerUseSkill(const Skill& skill)
{
    if (!player->UseActionPoints(skill.cost))
        return;

    std::vector<glm::ivec2> affected = skill.GetAffectedTiles(*player);

    for (auto* e : enemies)
    {
        for (auto& tile : affected)
        {
            if (e->GameX == tile.x && e->GameY == tile.y)
            {
                if (skill.healing)
                    e->Heal(skill.power);
                else
                    e->TakeDamage(skill.power);

                if (!e->IsAlive())
                    OnEnemyKilled(e);
            }
        }
    }
}

// ----------------------------------------------------
// Śmierć przeciwnika
// ----------------------------------------------------
void Game::OnEnemyKilled(Enemy* e)
{
    // 1) Dropy
    for (Item* item : e->drops)
    {
        std::cout << e->name << " dropped: " << item->name << "\n";
        player->PickupItem(item);
    }

    // 2) XP
    player->AddXp(10);  // ustal ile XP chcesz dawać

    // 3) Usuwanie przeciwnika
    enemies.erase(std::remove(enemies.begin(), enemies.end(), e), enemies.end());
    delete e;
}

// ----------------------------------------------------
// Debug render
// ----------------------------------------------------
void Game::DebugPrint()
{
    std::cout << "Player at (" << player->GameX << "," << player->GameY << ")\n";

    for (auto* e : enemies)
        std::cout << "- " << e->name << " at (" << e->GameX << "," << e->GameY << "), HP=" << e->health << "\n";
}
