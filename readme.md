
cmake -S . -B build -G "Visual Studio 17 2022"                          # budowanie 
cmake --build build --config Release                                    # budowanie projektu
build\Release\dungeon.exe                                               # odpalanie projektu z tej ścieżki z widoku najlepiej terminala

Super, mogę Ci zrobić przegląd całego projektu tak, jak wygląda teraz i jak wszystko ze sobą współgra. Pokażę strukturę, zależności i przepływ działania. Dzięki temu będziesz dokładnie wiedział, co jest gotowe, a co jeszcze trzeba dodać.

---

## 1️⃣ Struktura klas

### `Object`

* Podstawa dla wszystkich jednostek na mapie.
* Pola:

    * `GameX`, `GameY` – pozycja w siatce kafelków.
    * `RenderPosition` – pozycja do renderowania w OpenGL.
    * `yaw`, `yawRad` – kierunek obiektu.
    * `orientation` – wektor kierunku (+X = prawo, +Z = przód itd.).
* Funkcje:

    * `UpdateOrientation()` – aktualizuje `orientation` po zmianie yaw.
    * `GetDirIndex()` – zwraca indeks 0–7 odpowiadający kierunkowi (8 kierunków).
    * `GetTileInDirection(offset)` / `GetMoveTarget()` – zwraca kafelek w wybranym kierunku.

---

### `Entity` : public Object

* Klasa bazowa dla wszystkich jednostek, które mogą się poruszać, atakować i mieć HP/AP.
* Pola:

    * `maxHealth`, `health`
    * `ActionPoints`, `base_damage`
    * `colision` – czy obiekt blokuje ruch
* Funkcje:

    * Ruch: `GetForwardTile()`, `GetMoveTarget(offset)`, `TurnLeft/Right()`
    * Walka: `Attack(Entity* target)`, `TakeDamage(int dmg)`
    * AP: `UseActionPoints()`, `ResetActionPoints()`
* **Zmiana w ostatniej wersji:** Entity nie sprawdza już mapy ani kolizji – to robi `App::can_move_to`.

---

### `Player` : public Entity

* Pola:

    * XP, Level
    * Ekwipunek: `equippedWeapon`, `equippedArmor`
    * Lista umiejętności: `skills`
* Funkcje:

    * Zarządzanie itemami: `Equip`, `Unequip`, `ApplyItemStats`
    * Umiejętności: `LearnSkill`, `UseSkill`

---

### `Enemy` : public Entity

* Pola:

    * `drops` – lista itemów do upuszczenia po śmierci
* Funkcje:

    * `OnDeath()` – podnosi XP gracza i dodaje dropy
    * AI nie jest jeszcze zaimplementowane (będzie w `game.cpp` / klasie AI)
* **Szablony przeciwników w `GameData`** – pozwalają tworzyć wiele instancji tego samego typu w grze.

---

### `Skill`

* Pola:

    * `name`, `level`, `mana_cost`, `consumable` (true = użyteczny)
    * `offsets` – lista kafelków wokół jednostki, które skill może trafić
* Funkcje:

    * `Use(Entity* caster, std::vector<Entity*> targets)` – użycie skilla na jednym lub wielu celach
* Obsługa zarówno **pojedynczych** jak i **wielokrotnych celów**.

---

### `Item`

* Pola:

    * `name`, `type` (`Weapon`, `Armor`, `Accessory`, `Consumable`)
    * `usable` – czy można użyć w walce
    * `stats` – struct z wartościami: health, maxHealth, damage, actionPoints
* Przechowywany w grze jako **pointery** (`Item*`) w Player i Enemy.

---

### `GameData`

* Definiuje **szablony przeciwników, gracza, itemów i skilli**.
* Dzięki temu łatwo tworzyć wiele instancji np. kilku goblinów:

  ```cpp
  Enemy* goblin1 = new Enemy(GoblinTemplate, x1, y1);
  Enemy* goblin2 = new Enemy(GoblinTemplate, x2, y2);
  ```
* Skille mają już predefiniowane `offsets`, które określają które kafelki trafiają.

---

### `App` / `App.hpp`

* Główny loop gry:

    * `run()` → obsługa inputu, ruch, walka, render
* Przechowuje:

    * Gracza: `Player player_`
    * Pozycje przeciwników: `enemies_world_pos_`
    * Pozycje itemów: `items_world_pos_`, `items_alive_`
* Input:

    * Strzałki → obrót
    * UP → ruch do przodu, sprawdzenie kolizji z mapą (`can_move_to`)
    * SPACE → atak w przed siebie (`Attack`)
    * 1,2,3 → użycie skilli (`UseSkill`)
    * M → menu

---

### `handle_input` (aktualny stan)

* Odczytuje klawisze.
* Obraca gracza.
* Ruch w przód (sprawdza mapę).
* Podnosi przedmioty.
* **Do dodań**:

    * Atak i skille
    * Reset AP po turze
    * Wywołanie ruchu przeciwników po zakończeniu tury gracza (AP = 0).

---

## 2️⃣ Przepływ gry (aktualny)

1. `App::run()` → pętla gry.
2. `handle_input()` → gracz porusza się i podnosi itemy.
3. `Render` → wyświetla mapę, jednostki i itemy.
4. `Enemy AI` – jeszcze nie zaimplementowane, ale będzie w funkcji typu `App::EnemyTurn()`.

---

## 3️⃣ Dalsze kroki / brakujące elementy

* **Atak i skille w input handler**:

    * Użycie `player_.Attack()` lub `player_.UseSkill()`
    * Możliwość wybrania celu lub obszaru (na razie tylko przed siebie)
* **Ruch przeciwników / AI** po zakończeniu tury gracza
* **System AP i tura**:

    * Gracz wykonuje akcje dopóki AP > 0
    * Po wyczerpaniu AP → tura przeciwników
* **Ekwipunek w świecie**:

    * Możliwość podnoszenia i używania itemów
* **Obsługa skilli** z różnymi `offsets` i celami
* **OnDeath dla Enemy**:

    * Dodawanie XP graczowi
    * Upuszczanie itemów
* **Zarządzanie wieloma instancjami przeciwników** z `GameData`

---

Jeżeli chcesz, mogę Ci teraz zrobić **pełny schemat klasy `App` + handle_input + ruch, ataki i skille**, pokazując jak wszystko łączyć, żeby gra była w pełni grywalna w turach, z wieloma wrogami, itemami i skillem.

Chcesz, żebym to zrobił?
