#pragma once

#include <cstdint>

#include "engine/Physics/PhysicsSystem.h"
#include "engine/render/Renderer.h"

class ChunkStreamer;

class WorldInteractionSystem {
public:
    void updateSelection(const Renderer::CameraFrameData& camera,
                         double mouseX,
                         double mouseY,
                         float playerX,
                         float playerY,
                         float playerZ,
                         ChunkStreamer& streamer);

    void applyMouseActions(bool isLeftMousePressed,
                           bool isRightMousePressed,
                           float deltaTime,
                           ChunkStreamer& streamer,
                           const PhysicsBody& playerBody);

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

    void setInteractionRange(float range) { m_InteractionRange = range; }
    float getInteractionRange() const { return m_InteractionRange; }

    float getMiningProgress() const { return m_MiningProgress; }
    void getMiningTarget(int& outX, int& outY, int& outZ) const {
        outX = m_MiningTargetX;
        outY = m_MiningTargetY;
        outZ = m_MiningTargetZ;
    }

private:
    bool raycastVoxel(float startX,
                      float startY,
                      float startZ,
                      float dirX,
                      float dirY,
                      float dirZ,
                      float maxDistance,
                      ChunkStreamer& streamer,
                      int& outX,
                      int& outY,
                      int& outZ,
                      int* outPlaceX = nullptr,
                      int* outPlaceY = nullptr,
                      int* outPlaceZ = nullptr);

    bool m_HasHoveredBlock = false;
    int m_HoveredBlockX = 0;
    int m_HoveredBlockY = 0;
    int m_HoveredBlockZ = 0;

    bool m_HasPlacementBlock = false;
    int m_PlacementBlockX = 0;
    int m_PlacementBlockY = 0;
    int m_PlacementBlockZ = 0;

    float m_InteractionRange = 6.0f;

    float m_BreakRepeatTimer = 0.0f;
    float m_PlaceRepeatTimer = 0.0f;

    float m_MiningProgress = 0.0f;
    int m_MiningTargetX = 0;
    int m_MiningTargetY = 0;
    int m_MiningTargetZ = 0;

    static constexpr float kBreakRepeatSeconds = 0.10f;
    static constexpr float kPlaceRepeatSeconds = 0.12f;
};
