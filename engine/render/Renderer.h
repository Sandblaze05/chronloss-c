#pragma once

#include <cstddef>
#include <glad/glad.h>
#include "engine/world/Chunk.h"
#include "engine/world/ChunkStreamer.h"

class Renderer {
public:
    struct CameraFrameData {
        float eye[3] = {0.0f, 0.0f, 0.0f};
        float center[3] = {0.0f, 0.0f, 0.0f};
        float up[3] = {0.0f, 1.0f, 0.0f};
        float fovRadians = 0.0f;
        float aspect = 1.0f;
        int viewportWidth = 0;
        int viewportHeight = 0;
    };

    Renderer();
    ~Renderer();

    void beginFrame(float playerX, float playerY, float playerZ,
                    float camOffsetX, float camOffsetY, float camOffsetZ,
                    float deltaTime);
    void endFrame();

private:
    void init();
    void shutdown();
    void queryAnisotropySupport();
    void applyAtlasSamplingSettings();

    GLuint m_VAO = 0;
    GLuint m_VBO = 0;
    GLuint m_EBO = 0;
    GLuint m_Shader = 0;
    // screen-quad for shader grid
    GLuint m_QuadVAO = 0;
    GLuint m_QuadVBO = 0;

    GLuint m_GridShader = 0;
    GLuint m_SkyShader = 0;
    GLuint m_PlayerShader = 0;
    GLuint m_BlockAtlasTexture = 0;
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

    // world chunk streaming (seed, horizontal radius, vertical radius)
    ChunkStreamer m_Streamer{12345u, 4, 0};

    // Keep track of the camera's actual smoothed height (for lerping)
    float m_SmoothCameraY = 1.0f;
    float m_SmoothPlayerY = 0.0f;

    // Camera values from the last frame so simulation systems can derive world interactions.
    CameraFrameData m_LastCameraFrame;

    // Optional world-space highlight block controlled by external systems.
    bool m_HasHighlightBlock = false;
    int m_HighlightBlockX = 0;
    int m_HighlightBlockY = 0;
    int m_HighlightBlockZ = 0;

    std::size_t m_LastDrawCallCount = 0;
    std::size_t m_LastVertexCount = 0;

    bool m_MsaaEnabled = true;
    int m_RequestedMsaaSamples = 4;
    bool m_AnisotropySupported = false;
    bool m_AnisotropicFilteringEnabled = true;
    float m_MaxSupportedAnisotropy = 1.0f;
    float m_AnisotropyLevel = 8.0f;

    
public:
    size_t getLoadedChunkCount() {
        return m_Streamer.getLoadedChunkCount();
    }
    // input
    void onScroll(double xoffset, double yoffset);
    void onMouseButton(int button, int action, int mods);
    void onCursorPos(double xpos, double ypos);

    // Expose the internal chunk streamer so other systems (e.g., player) can query world blocks
    ChunkStreamer& getStreamer() { return m_Streamer; }

    // movement axes projected on the XZ ground plane
    void getGroundAxes(float& forwardX, float& forwardZ,
                       float& rightX, float& rightZ) const;

    const CameraFrameData& getLastCameraFrameData() const { return m_LastCameraFrame; }

    void setHighlightBlock(bool hasBlock, int x, int y, int z) {
        m_HasHighlightBlock = hasBlock;
        m_HighlightBlockX = x;
        m_HighlightBlockY = y;
        m_HighlightBlockZ = z;
    }

    bool isOrbiting() const { return m_Orbiting; }

    std::size_t getLastDrawCallCount() const { return m_LastDrawCallCount; }
    std::size_t getLastVertexCount() const { return m_LastVertexCount; }

    bool getMsaaEnabled() const { return m_MsaaEnabled; }
    void setMsaaEnabled(bool enabled);
    int getRequestedMsaaSamples() const { return m_RequestedMsaaSamples; }
    void setRequestedMsaaSamples(int samples);

    bool isAnisotropySupported() const { return m_AnisotropySupported; }
    bool getAnisotropicFilteringEnabled() const { return m_AnisotropicFilteringEnabled; }
    void setAnisotropicFilteringEnabled(bool enabled);
    float getMaxSupportedAnisotropy() const { return m_MaxSupportedAnisotropy; }
    float getAnisotropyLevel() const { return m_AnisotropyLevel; }
    void setAnisotropyLevel(float level);

};
