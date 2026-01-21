#include "dungeon/core/App.hpp"
#include <cstdio>
#include <exception>

int main() {
    try {
        dungeon::App app({ 1280, 720, "Dungeon Starter" });
        app.run();
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        std::getchar();
        return 1;
    }
    return 0;
}
