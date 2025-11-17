#pragma once
#include <string>

namespace dungeon::gfx {

	class Shader {
	public:
		Shader() = default;
		Shader(const char* vs_src, const char* fs_src);
		~Shader();
		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;
		Shader(Shader&&) noexcept;
		Shader& operator=(Shader&&) noexcept;

		void use() const;
		unsigned id() const { return program_; }
		void setMat4(const char* name, const float* m) const;
		void setVec3(const char* name, float x, float y, float z) const;
		void setInt(const char* name, int v) const;

	private:
		unsigned program_ = 0;
	};

} // namespace dungeon::gfx
