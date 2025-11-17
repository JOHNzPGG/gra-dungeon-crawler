#include "dungeon/gfx/Shader.hpp"
#include <glad/glad.h>
#include <stdexcept>

static unsigned compile_stage(unsigned type, const char* src) {
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, 2048, nullptr, log);
        throw std::runtime_error(log);
    }
    return s;
}

namespace dungeon::gfx {

    Shader::Shader(const char* vs_src, const char* fs_src) {
        unsigned vs = compile_stage(GL_VERTEX_SHADER, vs_src);
        unsigned fs = compile_stage(GL_FRAGMENT_SHADER, fs_src);
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);
        int ok; glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!ok) {
            char log[2048];
            glGetProgramInfoLog(program_, 2048, nullptr, log);
            throw std::runtime_error(log);
        }
    }

    Shader::~Shader() { if (program_) glDeleteProgram(program_); }

    Shader::Shader(Shader&& o) noexcept { program_ = o.program_; o.program_ = 0; }
    Shader& Shader::operator=(Shader&& o) noexcept {
        if (this != &o) {
            if (program_) glDeleteProgram(program_);
            program_ = o.program_;
            o.program_ = 0;
        }
        return *this;
    }

    void Shader::use() const { glUseProgram(program_); }

    void Shader::setMat4(const char* name, const float* m) const {
        int l = glGetUniformLocation(program_, name);
        if (l != -1) glUniformMatrix4fv(l, 1, GL_FALSE, m);
    }
    void Shader::setVec3(const char* name, float x, float y, float z) const {
        int l = glGetUniformLocation(program_, name);
        if (l != -1) glUniform3f(l, x, y, z);
    }
    void Shader::setInt(const char* name, int v) const {
        int l = glGetUniformLocation(program_, name);
        if (l != -1) glUniform1i(l, v);
    }

} // namespace dungeon::gfx
