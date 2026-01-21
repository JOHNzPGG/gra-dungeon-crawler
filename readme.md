
cmake -S . -B build -G "Visual Studio 17 2022"   # kompilacja
cmake --build build --config Release             # budowanie
build\Release\dungeon.exe
