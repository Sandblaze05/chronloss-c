#include "Player.h"
#include <cmath>
#include "engine/world/ChunkStreamer.h"
#include "engine/world/Block.h"
#include <GLFW/glfw3.h>

Player::Player(float startX, float startY, float startZ) 
    : speed_(5.0f) {
    pos_[0] = startX;
    pos_[1] = startY;
    pos_[2] = startZ;
}

void Player::update(GLFWwindow* window, float deltaTime,
                    float camForwardX, float camForwardZ,
                    float camRightX, float camRightZ,
                    ChunkStreamer& streamer) {
    float moveX = 0.0f, moveZ = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { moveX += camForwardX; moveZ += camForwardZ; }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { moveX -= camForwardX; moveZ -= camForwardZ; }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { moveX -= camRightX; moveZ -= camRightZ; }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { moveX += camRightX; moveZ += camRightZ; }

    float moveLen = std::sqrt(moveX * moveX + moveZ * moveZ);
    if (moveLen > 0.001f) {
        moveX /= moveLen; moveZ /= moveLen;
        pos_[0] += moveX * speed_ * deltaTime;
        pos_[2] += moveZ * speed_ * deltaTime;
    }

    // --- TERRAIN SNAP / EMBEDDED RESOLUTION ---
    int px = static_cast<int>(std::floor(pos_[0]));
    int pz = static_cast<int>(std::floor(pos_[2]));

    auto blockAt = [&](int worldY) -> const BlockData& {
        std::uint8_t id = streamer.getBlockAtWorld(px, worldY, pz);
        return BlockRegistry::get(id);
    };

    // If spawn/movement places the player inside solid terrain, push upward until clear.
    int feetY = static_cast<int>(std::floor(pos_[1]));
    int pushBudget = Chunk::kSizeY * 2;
    while (pushBudget-- > 0 && blockAt(feetY).isSolid) {
        ++feetY;
    }
    pos_[1] = static_cast<float>(feetY);

    // Then settle onto the nearest walkable block below (with empty feet space).
    int surfaceFeetY = -1;
    const int probeDown = Chunk::kSizeY * 2;
    for (int y = feetY; y >= feetY - probeDown; --y) {
        const BlockData& ground = blockAt(y - 1);
        const BlockData& feet = blockAt(y);
        if (ground.isWalkable && !feet.isSolid) {
            surfaceFeetY = y;
            break;
        }
    }

    if (surfaceFeetY != -1) {
        pos_[1] = static_cast<float>(surfaceFeetY);
    }
}