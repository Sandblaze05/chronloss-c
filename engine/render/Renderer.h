#pragma once

#include <glad/glad.h>

class Renderer {
public:
    Renderer();
    ~Renderer();

    void beginFrame();
    void endFrame();

private:
    void init();
    void shutdown();

    GLuint m_VAO = 0;
    GLuint m_VBO = 0;
    GLuint m_EBO = 0;
    GLuint m_Shader = 0;
    // screen-quad for shader grid
    GLuint m_QuadVAO = 0;
    GLuint m_QuadVBO = 0;

    GLuint m_GridShader = 0;
    // camera params
    float m_Zoom = 1.8f; // orthographic half-extent
    float m_Yaw = 45.0f * 3.14159265f / 180.0f;   // radians
    float m_Pitch = 35.264f * 3.14159265f / 180.0f; // radians
    float m_Distance = 3.0f;

    // orbit interaction
    bool m_Orbiting = false;
    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;

    bool m_Initialized = false;

public:
    // input
    void onScroll(double xoffset, double yoffset);
    void onMouseButton(int button, int action, int mods);
    void onCursorPos(double xpos, double ypos);
};
