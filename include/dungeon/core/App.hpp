#pragma once
#include <string>
#include <glm/mat4x4.hpp>

#include "dungeon/io/MapLoader.hpp"
#include "dungeon/gfx/Shader.hpp"

struct GLFWwindow;

namespace dungeon {

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
	};

} // namespace dungeon
