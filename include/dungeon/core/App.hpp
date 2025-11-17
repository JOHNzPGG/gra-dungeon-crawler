#pragma once
#include <string>

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

		GLFWwindow* window_ = nullptr;
		AppConfig cfg_{};
	};

}
