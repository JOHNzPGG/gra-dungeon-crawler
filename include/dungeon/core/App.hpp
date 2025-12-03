#pragma once
#include <string>
#include <glm/mat4x4.hpp>

#include "dungeon/io/MapLoader.hpp"
#include "dungeon/gfx/Shader.hpp"

struct GLFWwindow;

namespace dungeon {

	enum class Dir { North, East, South, West };

	struct Player {
		int x = 1;
		int y = 1;
		Dir dir = Dir::North;
	};

	struct AppConfig {
		int width = 1280;
		int height = 720;
		std::string title = "Dungeon";
	};

	class App {
	public:
		explicit App(const AppConfig& cfg);
		~App();
		void run();

	private:
		void init_glfw();
		void init_gl();
		void init_imgui();
		void shutdown_imgui();

		void frame_begin();
		void frame_render();
		void frame_ui();
		void frame_end();

		void handle_input();
		bool can_move_to(int x, int y) const;


		void load_level();
		void build_world_mesh();

		GLFWwindow* window_ = nullptr;
		AppConfig cfg_{};

		// === nowoœci ===
		io::Level level_{};
		gfx::Shader world_shader_{};
		unsigned floor_vao_ = 0, floor_vbo_ = 0;
		unsigned wall_vao_ = 0, wall_vbo_ = 0;
		int floor_vertex_count_ = 0;
		int wall_vertex_count_ = 0;

		glm::mat4 proj_{ 1.0f };
		glm::mat4 view_{ 1.0f };

		Player player_{};

		bool left_was_down_ = false;
		bool right_was_down_ = false;
		bool up_was_down_ = false;
	};

} // namespace dungeon
