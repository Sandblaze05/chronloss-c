#pragma once
#include <GLFW/glfw3.h>

class Player {
public:
    Player(float startX, float startY, float startZ);

    // The player handles movement based on camera-relative ground axes.
    void update(GLFWwindow* window, float deltaTime,
                float camForwardX, float camForwardZ,
                float camRightX, float camRightZ);

    // Getters so the camera and world know where the player is
    float getX() const { return pos_[0]; }
    float getY() const { return pos_[1]; }
    float getZ() const { return pos_[2]; }

private:
    float pos_[3];
    float speed_;
};