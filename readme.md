git clone ... dungeon
cd dungeon
cmake -S . -B build -G "Visual Studio 17 2022" -DDUNGEON_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release
build\Release\dungeon.exe
