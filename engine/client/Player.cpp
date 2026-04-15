#include "Player.h"
#include <cmath>
#include "engine/world/ChunkStreamer.h"
#include <GLFW/glfw3.h>

Player::Player(float startX, float startY, float startZ) : speed_(5.0f) {
    body.pos[0] = startX;
    body.pos[1] = startY;
    body.pos[2] = startZ;
    body.vel[0] = 0.0f;
    body.vel[1] = 0.0f;
    body.vel[2] = 0.0f;
}

void Player::update(GLFWwindow* window, float deltaTime,
                    float camForwardX, float camForwardZ,
                    float camRightX, float camRightZ,
                    ChunkStreamer& streamer) {
    (void)deltaTime;
    (void)streamer;

    static bool wasJumpPressed = false;
    bool isJumpPressed = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (isJumpPressed && !wasJumpPressed && body.onGround) {
        body.vel[1] = 8.0f;
        body.onGround = false;
    }
    wasJumpPressed = isJumpPressed;
    
    float moveX = 0.0f, moveZ = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { moveX += camForwardX; moveZ += camForwardZ; }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { moveX -= camForwardX; moveZ -= camForwardZ; }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { moveX -= camRightX; moveZ -= camRightZ; }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { moveX += camRightX; moveZ += camRightZ; }

    float moveLen = std::sqrt(moveX * moveX + moveZ * moveZ);
    if (moveLen > 0.001f) {
        moveX /= moveLen; moveZ /= moveLen;
        body.vel[0] = moveX * speed_;
        body.vel[2] = moveZ * speed_;
    } else {
        body.vel[0] = 0.0f;
        body.vel[2] = 0.0f;
    }
}