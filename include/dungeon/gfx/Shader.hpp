#pragma once

#include <string>
#include <glad/glad.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <glm/glm.hpp>

namespace dungeon::gfx {

/**
 * @brief Klasa reprezentująca shader OpenGL (vertex + fragment)
 *
 * Umożliwia kompilację shaderów z kodu źródłowego oraz ustawianie uniformów.
 */
class Shader {
public:
    /** @brief Konstruktor domyślny */
    Shader() = default;

    /**
     * @brief Konstruktor ładujący shadery z kodu źródłowego
     * @param vs_src Kod źródłowy vertex shadera
     * @param fs_src Kod źródłowy fragment shadera
     */
    Shader(const char* vs_src, const char* fs_src);

    /** @brief Destruktor – usuwa program shadera */
    ~Shader();

    // Usunięcie kopiowania
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Przenoszenie
    Shader(Shader&&) noexcept;
    Shader& operator=(Shader&&) noexcept;

    /**
     * @brief Aktywuje shader w OpenGL
     */
    void use() const;

    /**
     * @brief Zwraca identyfikator programu shadera
     * @return unsigned ID programu
     */
    unsigned id() const { return program_; }

    /**
     * @brief Ustawia uniform typu mat4
     * @param name Nazwa uniformu w shaderze
     * @param m Wskaźnik do tablicy 16 floatów (4x4)
     */
    void setMat4(const char* name, const float* m) const;

    /**
     * @brief Ustawia uniform typu vec3
     * @param name Nazwa uniformu w shaderze
     * @param x Składowa X
     * @param y Składowa Y
     * @param z Składowa Z
     */
    void setVec3(const char* name, float x, float y, float z) const;

    /**
     * @brief Ustawia uniform typu int
     * @param name Nazwa uniformu w shaderze
     * @param v Wartość całkowita
     */
    void setInt(const char* name, int v) const;

    /**
     * @brief Ustawia uniform typu vec4
     * @param name Nazwa uniformu w shaderze
     * @param x Składowa X
     * @param y Składowa Y
     * @param z Składowa Z
     * @param w Składowa W
     */
    void setVec4(const char* name, float x, float y, float z, float w) const;

    /**
     * @brief Ustawia uniform typu float
     * @param name Nazwa uniformu w shaderze
     * @param value Wartość float
     */
    void setFloat(const std::string& name, float value) const {
        glUniform1f(glGetUniformLocation(program_, name.c_str()), value);
    }

private:
    unsigned program_ = 0; /**< ID programu shadera w OpenGL */
};

} // namespace dungeon::gfx
