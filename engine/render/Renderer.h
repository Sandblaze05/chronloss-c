#pragma once

#include <cstddef>
#include <glad/glad.h>
#include "engine/world/Chunk.h"
#include "engine/world/ChunkStreamer.h"

class Renderer {
public:
    Renderer();
    ~Renderer();

    void beginFrame(float playerX, float playerY, float playerZ,
                    float camOffsetX, float camOffsetY, float camOffsetZ,
                    float deltaTime);
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
    GLuint m_PlayerShader = 0;
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

    // Hovered block from mouse raycast.
    bool m_HasHoveredBlock = false;
    int m_HoveredBlockX = 0;
    int m_HoveredBlockY = 0;
    int m_HoveredBlockZ = 0;
    bool m_HasPlacementBlock = false;
    int m_PlacementBlockX = 0;
    int m_PlacementBlockY = 0;
    int m_PlacementBlockZ = 0;

    std::size_t m_LastDrawCallCount = 0;
    std::size_t m_LastVertexCount = 0;

    
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

    bool hasHoveredBlock() const { return m_HasHoveredBlock; }
    void getHoveredBlock(int& outX, int& outY, int& outZ) const {
        outX = m_HoveredBlockX;
        outY = m_HoveredBlockY;
        outZ = m_HoveredBlockZ;
    }
    bool hasPlacementBlock() const { return m_HasPlacementBlock; }
    void getPlacementBlock(int& outX, int& outY, int& outZ) const {
        outX = m_PlacementBlockX;
        outY = m_PlacementBlockY;
        outZ = m_PlacementBlockZ;
    }

    bool isOrbiting() const { return m_Orbiting; }

    std::size_t getLastDrawCallCount() const { return m_LastDrawCallCount; }
    std::size_t getLastVertexCount() const { return m_LastVertexCount; }

    bool raycastVoxel(float startX, float startY, float startZ, float dirX, float dirY, float dirZ, float maxDistance, ChunkStreamer& streamer, int& outX, int& outY, int& outZ, int* outPlaceX = nullptr, int* outPlaceY = nullptr, int* outPlaceZ = nullptr);
};
