#include "dungeon/gfx/Shader.hpp"
#include <glad/glad.h>
#include <stdexcept>

/**
 * @brief Kompiluje pojedynczy shader (vertex lub fragment) z kodu źródłowego
 *
 * @param type Typ shadera (GL_VERTEX_SHADER lub GL_FRAGMENT_SHADER)
 * @param src Kod źródłowy shadera
 * @return unsigned ID skompilowanego shadera
 * @throws std::runtime_error Jeśli kompilacja zakończy się błędem, wyrzuca log
 */
static unsigned compile_stage(unsigned type, const char* src) {
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, 2048, nullptr, log);
        throw std::runtime_error(log);
    }

    return s;
}

namespace dungeon::gfx {

/**
 * @brief Konstruktor tworzący program shadera z vertex i fragment shader
 * @param vs_src Kod źródłowy vertex shadera
 * @param fs_src Kod źródłowy fragment shadera
 * @throws std::runtime_error Jeśli kompilacja lub linkowanie shadera się nie powiedzie
 */
Shader::Shader(const char* vs_src, const char* fs_src) {
    unsigned vs = compile_stage(GL_VERTEX_SHADER, vs_src);
    unsigned fs = compile_stage(GL_FRAGMENT_SHADER, fs_src);

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    int ok;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);

    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program_, 2048, nullptr, log);
        throw std::runtime_error(log);
    }
}

/**
 * @brief Destruktor – usuwa program shadera z GPU
 */
Shader::~Shader() {
    if (program_) glDeleteProgram(program_);
}

/**
 * @brief Konstruktor przenoszący
 * @param o Shader źródłowy do przeniesienia
 */
Shader::Shader(Shader&& o) noexcept {
    program_ = o.program_;
    o.program_ = 0;
}

/**
 * @brief Operator przypisania przenoszącego
 * @param o Shader źródłowy do przeniesienia
 * @return Shader& Referencja do tego shadera
 */
Shader& Shader::operator=(Shader&& o) noexcept {
    if (this != &o) {
        if (program_) glDeleteProgram(program_);
        program_ = o.program_;
        o.program_ = 0;
    }
    return *this;
}

/**
 * @brief Aktywuje shader w OpenGL
 */
void Shader::use() const {
    glUseProgram(program_);
}

/**
 * @brief Ustawia uniform typu mat4
 * @param name Nazwa uniformu w shaderze
 * @param m Wskaźnik do tablicy 16 floatów (4x4)
 */
void Shader::setMat4(const char* name, const float* m) const {
    int l = glGetUniformLocation(program_, name);
    if (l != -1) glUniformMatrix4fv(l, 1, GL_FALSE, m);
}

/**
 * @brief Ustawia uniform typu vec3
 * @param name Nazwa uniformu w shaderze
 * @param x Składowa X
 * @param y Składowa Y
 * @param z Składowa Z
 */
void Shader::setVec3(const char* name, float x, float y, float z) const {
    int l = glGetUniformLocation(program_, name);
    if (l != -1) glUniform3f(l, x, y, z);
}

/**
 * @brief Ustawia uniform typu int
 * @param name Nazwa uniformu w shaderze
 * @param v Wartość całkowita
 */
void Shader::setInt(const char* name, int v) const {
    int l = glGetUniformLocation(program_, name);
    if (l != -1) glUniform1i(l, v);
}

/**
 * @brief Ustawia uniform typu vec4
 * @param name Nazwa uniformu w shaderze
 * @param x Składowa X
 * @param y Składowa Y
 * @param z Składowa Z
 * @param w Składowa W
 */
void Shader::setVec4(const char* name, float x, float y, float z, float w) const {
    glUniform4f(glGetUniformLocation(program_, name), x, y, z, w);
}

} // namespace dungeon::gfx
