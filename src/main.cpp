#include "dungeon/core/App.hpp"
#include <cstdio>
#include <exception>

/**
 * @brief Punkt wejścia do gry Dungeon Starter
 *
 * Tworzy instancję aplikacji i uruchamia główną pętlę gry.
 * W przypadku błędu wyrzuca komunikat na stderr i czeka na naciśnięcie Enter.
 *
 * @return int Kod zakończenia programu (0 – sukces, 1 – błąd)
 */
int main() {
    try {
        // Tworzymy aplikację z konfiguracją: szerokość, wysokość, tytuł
        dungeon::App app({ 1280, 720, "Dungeon Starter" });

        // Uruchamiamy główną pętlę gry
        app.run();
    }
    catch (const std::exception& e) {
        // W przypadku wyjątku wypisujemy komunikat i czekamy na Enter
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        std::getchar(); // czekaj na Enter, żeby okno konsoli nie znikło od razu
        return 1;
    }
    return 0; // Zakończenie sukcesem
}
