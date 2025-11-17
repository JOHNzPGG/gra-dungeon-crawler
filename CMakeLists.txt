cmake_minimum_required(VERSION 3.22)
project(dungeon LANGUAGES CXX)

if(MSVC)
  # Wymuœ /MD lub /MDd dla wszystkich targetów
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(MSVC)
  set_property(TARGET glad PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DUNGEON_BUILD_TESTS "Build tests" OFF)
option(DUNGEON_ENABLE_ASAN "Enable AddressSanitizer" OFF)

include(FetchContent)

# ===== GLFW =====
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

# ===== GLM =====
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

# ===== ImGui =====
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.0
)
FetchContent_MakeAvailable(imgui)

# ===== GLAD (rêcznie dodany folder external/glad) =====
add_library(glad external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)

# ===== Biblioteka ImGui (z backendami GLFW + OpenGL3) =====
add_library(imgui_lib
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_lib PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)

# ¯eby imgui_impl_glfw.cpp znalaz³ <GLFW/glfw3.h>
target_link_libraries(imgui_lib PUBLIC glfw)


# ===== G³ówne Ÿród³a gry =====
file(GLOB_RECURSE SRC CONFIGURE_DEPENDS src/*.cpp)

add_executable(dungeon
  ${SRC}
)

target_include_directories(dungeon PUBLIC
  include
)

target_link_libraries(dungeon PRIVATE
  glfw
  glad
  imgui_lib
)

# ===== Sanitizery (opcjonalnie) =====
if (DUNGEON_ENABLE_ASAN AND MSVC)
  target_compile_options(dungeon PRIVATE /fsanitize=address)
elseif(DUNGEON_ENABLE_ASAN)
  target_compile_options(dungeon PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(dungeon PRIVATE -fsanitize=address)
endif()

# ===== Testy =====
if (DUNGEON_BUILD_TESTS)
  enable_testing()

  add_executable(tests
    tests/test_map_loader.cpp
    src/io/MapLoader.cpp      # <-- test kompiluje ten sam loader
  )

  target_include_directories(tests PUBLIC include)

  add_test(NAME unit COMMAND tests)
endif()

# ===== Kopiowanie assets do build/ =====
add_custom_target(copy_assets ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets
          ${CMAKE_BINARY_DIR}/assets
)
add_dependencies(dungeon copy_assets)
