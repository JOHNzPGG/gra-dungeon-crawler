
cmake -S . -B build -G "Visual Studio 17 2022"                          # budowanie 
cmake --build build --config Release                                    # budowanie projektu
build\Release\dungeon.exe                                               # odpalanie projektu z tej ścieżki z widoku najlepiej terminala
