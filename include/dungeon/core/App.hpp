#pragma once
#include <string>
#include <glm/mat4x4.hpp>
#include <glad/glad.h>
using GLuint = unsigned int;
#include "tiny_obj_loader.h"
#include "dungeon/io/MapLoader.hpp"
#include "dungeon/gfx/Shader.hpp"
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

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
		void frame_render() {
			// pozycja środka kratki gracza
			float px = static_cast<float>(player_.x) + 0.5f;
			float pz = static_cast<float>(player_.y) + 0.5f;

			glm::vec3 forward;
			int dx = 0;
			int dz = 0;

			switch (player_.dir) {
			case Dir::North: forward = glm::vec3(0.0f, 0.0f, -1.0f); dx = 0;  dz = -1; break;
			case Dir::South: forward = glm::vec3(0.0f, 0.0f, 1.0f); dx = 0;  dz = 1; break;
			case Dir::West:  forward = glm::vec3(-1.0f, 0.0f, 0.0f);  dx = -1; dz = 0; break;
			case Dir::East:  forward = glm::vec3(1.0f, 0.0f, 0.0f);  dx = 1;  dz = 0; break;
			}

			// sprawdzamy, co jest ZA graczem (kafelek odwrotnie do kierunku patrzenia)
			int bx = player_.x - dx;
			int by = player_.y - dz;

			// jeśli NIE możemy wejść na kafelek za nami -> traktuj jako ścianę/blokadę
			bool behind_is_blocked = !can_move_to(bx, by);

			// punkt, na który patrzymy – trochę przed graczem i lekko nad podłogą
			glm::vec3 cam_target = glm::vec3(px, 0.6f, pz) + forward * 0.35f;

			// jeśli za plecami ściana → kamera bliżej, żeby nie wlatywać w nią głową
			float cam_distance = behind_is_blocked ? 0.6f : 1.0f;
			float cam_height = 0.6f;

			glm::vec3 cam_pos = cam_target - forward * cam_distance
				+ glm::vec3(0.0f, cam_height, 0.0f);

			view_ = glm::lookAt(cam_pos, cam_target, glm::vec3(0.0f, 1.0f, 0.0f));

			// --- reszta bez zmian: rysowanie świata ---
			world_shader_.use();
			world_shader_.setMat4("uProj", &proj_[0][0]);
			world_shader_.setMat4("uView", &view_[0][0]);

			// --- RYSOWANIE PODŁOGI (z teksturą) ---
			// Włączamy tryb tekstury (uUseTex = 1)
			world_shader_.setInt("uUseTex", 1);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, floor_texture_);
			world_shader_.setInt("uTex", 0);

			glBindVertexArray(floor_vao_);
			glDrawArrays(GL_TRIANGLES, 0, floor_vertex_count_);


			// --- RYSOWANIE ŚCIAN (kolor, bez tekstury) ---
			// Wyłączamy tryb tekstury (uUseTex = 0)
			world_shader_.setInt("uUseTex", 1);
			world_shader_.setInt("uUseTex", 1); // Włącz tekstury
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, wall_texture_);

			// Ustawiamy kolor ścian (R, G, B, A). Np. szary kamienny:
			// Wartości są od 0.0 do 1.0. 
			// Tu daję jasnoszary/lekko czerwonawy.
			//world_shader_.setVec4("uColor", 0.6f, 0.6f, 0.6f, 1.0f);

			glBindVertexArray(wall_vao_);
			glDrawArrays(GL_TRIANGLES, 0, wall_vertex_count_);

			glBindVertexArray(0);
		}
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
		unsigned wall_texture_ = 0;
		unsigned floor_texture_ = 0;
		unsigned load_texture(const char* path);
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

		std::string current_map_name_;

		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		const std::string MODEL_PATH = "models/Untitled.obj";

		tinyobj::attrib_t floorAttrib;
		std::vector<tinyobj::shape_t> floorShapes;
		std::vector<tinyobj::material_t> floorMaterials;
		const std::string FLOOR_MODEL_PATH = "models/kamien1.obj";
	};
} // namespace dungeon
