#include "Player.h"
#include <cmath>

Player::Player(float startX, float startY, float startZ) 
    : speed_(5.0f) {
    pos_[0] = startX;
    pos_[1] = startY;
    pos_[2] = startZ;
}

void Player::update(GLFWwindow* window, float deltaTime,
                    float camForwardX, float camForwardZ,
                    float camRightX, float camRightZ) {
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
}