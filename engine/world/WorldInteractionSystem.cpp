#include "WorldInteractionSystem.h"

#include <algorithm>
#include <cmath>

#include "engine/world/Block.h"
#include "engine/world/ChunkStreamer.h"

void WorldInteractionSystem::updateSelection(const Renderer::CameraFrameData& camera,
                                             double mouseX,
                                             double mouseY,
                                             float playerX,
                                             float playerY,
                                             float playerZ,
                                             ChunkStreamer& streamer) {
    m_HasHoveredBlock = false;
    m_HasPlacementBlock = false;

    if (camera.viewportWidth <= 0 || camera.viewportHeight <= 0) {
        return;
    }

    const float clampedMouseX = std::clamp(static_cast<float>(mouseX), 0.0f, static_cast<float>(camera.viewportWidth - 1));
    const float clampedMouseY = std::clamp(static_cast<float>(mouseY), 0.0f, static_cast<float>(camera.viewportHeight - 1));

    const float ndcX = (2.0f * (clampedMouseX / static_cast<float>(camera.viewportWidth))) - 1.0f;
    const float ndcY = 1.0f - (2.0f * (clampedMouseY / static_cast<float>(camera.viewportHeight)));

    const float tanHalfFov = std::tan(camera.fovRadians * 0.5f);
    float rayCamX = ndcX * camera.aspect * tanHalfFov;
    float rayCamY = ndcY * tanHalfFov;
    float rayCamZ = -1.0f;

    const float rayCamLen = std::sqrt((rayCamX * rayCamX) + (rayCamY * rayCamY) + (rayCamZ * rayCamZ));
    if (rayCamLen <= 0.0001f) {
        return;
    }

    rayCamX /= rayCamLen;
    rayCamY /= rayCamLen;
    rayCamZ /= rayCamLen;

    float fwd[3] = {
        camera.center[0] - camera.eye[0],
        camera.center[1] - camera.eye[1],
        camera.center[2] - camera.eye[2],
    };
    const float fwdLen = std::sqrt((fwd[0] * fwd[0]) + (fwd[1] * fwd[1]) + (fwd[2] * fwd[2]));
    if (fwdLen <= 0.0001f) {
        return;
    }
    fwd[0] /= fwdLen;
    fwd[1] /= fwdLen;
    fwd[2] /= fwdLen;

    float right[3] = {
        (fwd[1] * camera.up[2]) - (fwd[2] * camera.up[1]),
        (fwd[2] * camera.up[0]) - (fwd[0] * camera.up[2]),
        (fwd[0] * camera.up[1]) - (fwd[1] * camera.up[0]),
    };
    const float rightLen = std::sqrt((right[0] * right[0]) + (right[1] * right[1]) + (right[2] * right[2]));
    if (rightLen <= 0.0001f) {
        return;
    }
    right[0] /= rightLen;
    right[1] /= rightLen;
    right[2] /= rightLen;

    float upRay[3] = {
        (right[1] * fwd[2]) - (right[2] * fwd[1]),
        (right[2] * fwd[0]) - (right[0] * fwd[2]),
        (right[0] * fwd[1]) - (right[1] * fwd[0]),
    };

    float rayWorldX = (right[0] * rayCamX) + (upRay[0] * rayCamY) + (fwd[0] * (-rayCamZ));
    float rayWorldY = (right[1] * rayCamX) + (upRay[1] * rayCamY) + (fwd[1] * (-rayCamZ));
    float rayWorldZ = (right[2] * rayCamX) + (upRay[2] * rayCamY) + (fwd[2] * (-rayCamZ));

    const float rayWorldLen = std::sqrt((rayWorldX * rayWorldX) + (rayWorldY * rayWorldY) + (rayWorldZ * rayWorldZ));
    if (rayWorldLen <= 0.0001f) {
        return;
    }

    rayWorldX /= rayWorldLen;
    rayWorldY /= rayWorldLen;
    rayWorldZ /= rayWorldLen;

    const float cameraDistance = std::sqrt(
        ((camera.eye[0] - camera.center[0]) * (camera.eye[0] - camera.center[0])) +
        ((camera.eye[1] - camera.center[1]) * (camera.eye[1] - camera.center[1])) +
        ((camera.eye[2] - camera.center[2]) * (camera.eye[2] - camera.center[2])));

    const float maxRayLength = cameraDistance + m_InteractionRange + 2.0f;

    int hitX = 0;
    int hitY = 0;
    int hitZ = 0;
    int placeX = 0;
    int placeY = 0;
    int placeZ = 0;
    if (!raycastVoxel(camera.eye[0], camera.eye[1], camera.eye[2],
                      rayWorldX, rayWorldY, rayWorldZ,
                      maxRayLength, streamer,
                      hitX, hitY, hitZ,
                      &placeX, &placeY, &placeZ)) {
        return;
    }

    const float blockCenterX = static_cast<float>(hitX) + 0.5f;
    const float blockCenterY = static_cast<float>(hitY) + 0.5f;
    const float blockCenterZ = static_cast<float>(hitZ) + 0.5f;

    const float dx = blockCenterX - playerX;
    const float dy = blockCenterY - playerY;
    const float dz = blockCenterZ - playerZ;
    const float distFromPlayer = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));

    if (distFromPlayer > m_InteractionRange) {
        return;
    }

    m_HasHoveredBlock = true;
    m_HoveredBlockX = hitX;
    m_HoveredBlockY = hitY;
    m_HoveredBlockZ = hitZ;

    m_HasPlacementBlock = true;
    m_PlacementBlockX = placeX;
    m_PlacementBlockY = placeY;
    m_PlacementBlockZ = placeZ;
}

void WorldInteractionSystem::applyMouseActions(bool isLeftMousePressed,
                                               bool isRightMousePressed,
                                               float deltaTime,
                                               ChunkStreamer& streamer,
                                               const PhysicsBody& playerBody) {
    m_BreakRepeatTimer -= deltaTime;
    m_PlaceRepeatTimer -= deltaTime;

    // if (isLeftMousePressed && m_BreakRepeatTimer <= 0.0f && m_HasHoveredBlock) {
    //     streamer.setBlockAtWorld(m_HoveredBlockX, m_HoveredBlockY, m_HoveredBlockZ, 0);
    //     m_BreakRepeatTimer = kBreakRepeatSeconds;
    // }

    if (m_HasHoveredBlock) {
        // target change
        if (m_HoveredBlockX != m_MiningTargetX ||
            m_HoveredBlockY != m_MiningTargetY ||
            m_HoveredBlockZ != m_MiningTargetZ) {
                
            m_MiningTargetX = m_HoveredBlockX;
            m_MiningTargetY = m_HoveredBlockY;
            m_MiningTargetZ = m_HoveredBlockZ;

            m_MiningProgress = 0.0f; // reset
        }
    }

    if (isLeftMousePressed && m_HasHoveredBlock) {
        // process mining when not on cooldown
        if (m_BreakRepeatTimer <= 0.0f) {
            m_MiningProgress += deltaTime;

            const std::uint8_t blockId = streamer.getBlockAtWorld(m_MiningTargetX, m_MiningTargetY, m_MiningTargetZ);
            const float blockToughness = BlockRegistry::get(blockId).toughness;

            if (blockToughness >= 0.0f) {
                if (m_MiningProgress >= blockToughness) {
                    streamer.setBlockAtWorld(m_MiningTargetX, m_MiningTargetY, m_MiningTargetZ, 0); // break

                    m_MiningProgress = 0.0f;
                    m_BreakRepeatTimer = kBreakRepeatSeconds;
                }
            }
            
        }
    } else {
        if (m_MiningProgress > 0.0f) {
            const float decayRate = 2.0f;

            m_MiningProgress -= decayRate * deltaTime;

            if (m_MiningProgress < 0.0f) {
                m_MiningProgress = 0.0f;
            }
        }
    }

    if (isRightMousePressed && m_PlaceRepeatTimer <= 0.0f && m_HasPlacementBlock) {
        const std::uint8_t existingId = streamer.getBlockAtWorld(m_PlacementBlockX, m_PlacementBlockY, m_PlacementBlockZ);
        const bool isTargetSolid = BlockRegistry::get(existingId).isSolid();

        const float blockMinX = static_cast<float>(m_PlacementBlockX);
        const float blockMaxX = blockMinX + 1.0f;
        const float blockMinY = static_cast<float>(m_PlacementBlockY);
        const float blockMaxY = blockMinY + 1.0f;
        const float blockMinZ = static_cast<float>(m_PlacementBlockZ);
        const float blockMaxZ = blockMinZ + 1.0f;

        const float playerMinX = playerBody.pos[0] - (playerBody.width * 0.5f);
        const float playerMaxX = playerBody.pos[0] + (playerBody.width * 0.5f);
        const float playerMinY = playerBody.pos[1];
        const float playerMaxY = playerBody.pos[1] + playerBody.height;
        const float playerMinZ = playerBody.pos[2] - (playerBody.width * 0.5f);
        const float playerMaxZ = playerBody.pos[2] + (playerBody.width * 0.5f);

        const bool overlapsPlayer =
            (blockMinX < playerMaxX && blockMaxX > playerMinX) &&
            (blockMinY < playerMaxY && blockMaxY > playerMinY) &&
            (blockMinZ < playerMaxZ && blockMaxZ > playerMinZ);

        if (!isTargetSolid && !overlapsPlayer) {
            streamer.setBlockAtWorld(m_PlacementBlockX, m_PlacementBlockY, m_PlacementBlockZ, 1);
            m_PlaceRepeatTimer = kPlaceRepeatSeconds;
        }
    }

    if (!isLeftMousePressed) {
        m_BreakRepeatTimer = 0.0f;
    }
    if (!isRightMousePressed) {
        m_PlaceRepeatTimer = 0.0f;
    }
}

bool WorldInteractionSystem::raycastVoxel(float startX,
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
                                          int* outPlaceX,
                                          int* outPlaceY,
                                          int* outPlaceZ) {
    float len = std::sqrt((dirX * dirX) + (dirY * dirY) + (dirZ * dirZ));
    if (len < 0.0001f) {
        return false;
    }

    dirX /= len;
    dirY /= len;
    dirZ /= len;

    int mapX = static_cast<int>(std::floor(startX));
    int mapY = static_cast<int>(std::floor(startY));
    int mapZ = static_cast<int>(std::floor(startZ));

    const float deltaDistX = std::abs(1.0f / dirX);
    const float deltaDistY = std::abs(1.0f / dirY);
    const float deltaDistZ = std::abs(1.0f / dirZ);

    const int stepX = (dirX < 0.0f) ? -1 : 1;
    const int stepY = (dirY < 0.0f) ? -1 : 1;
    const int stepZ = (dirZ < 0.0f) ? -1 : 1;

    float sideDistX = (dirX < 0.0f) ? (startX - static_cast<float>(mapX)) * deltaDistX : (static_cast<float>(mapX) + 1.0f - startX) * deltaDistX;
    float sideDistY = (dirY < 0.0f) ? (startY - static_cast<float>(mapY)) * deltaDistY : (static_cast<float>(mapY) + 1.0f - startY) * deltaDistY;
    float sideDistZ = (dirZ < 0.0f) ? (startZ - static_cast<float>(mapZ)) * deltaDistZ : (static_cast<float>(mapZ) + 1.0f - startZ) * deltaDistZ;

    float currentDist = 0.0f;
    int prevX = mapX;
    int prevY = mapY;
    int prevZ = mapZ;

    while (currentDist <= maxDistance) {
        const std::uint8_t blockId = streamer.getBlockAtWorld(mapX, mapY, mapZ);
        if (BlockRegistry::get(blockId).isSolid()) {
            outX = mapX;
            outY = mapY;
            outZ = mapZ;
            if (outPlaceX != nullptr && outPlaceY != nullptr && outPlaceZ != nullptr) {
                *outPlaceX = prevX;
                *outPlaceY = prevY;
                *outPlaceZ = prevZ;
            }
            return true;
        }

        if (sideDistX < sideDistY && sideDistX < sideDistZ) {
            currentDist = sideDistX;
            sideDistX += deltaDistX;
            prevX = mapX;
            prevY = mapY;
            prevZ = mapZ;
            mapX += stepX;
        } else if (sideDistY < sideDistZ) {
            currentDist = sideDistY;
            sideDistY += deltaDistY;
            prevX = mapX;
            prevY = mapY;
            prevZ = mapZ;
            mapY += stepY;
        } else {
            currentDist = sideDistZ;
            sideDistZ += deltaDistZ;
            prevX = mapX;
            prevY = mapY;
            prevZ = mapZ;
            mapZ += stepZ;
        }
    }

    return false;
}
