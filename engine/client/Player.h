#pragma once
#include <engine/Physics/PhysicsSystem.h>

struct GLFWwindow;
// forward declare to avoid including heavy headers in the player header
class ChunkStreamer;

class Player {
public:
    Player(float startX, float startY, float startZ);

    PhysicsBody body;

    // The player handles movement based on camera-relative ground axes.
    void update(GLFWwindow* window, float deltaTime,
                float camForwardX, float camForwardZ,
                float camRightX, float camRightZ,
                ChunkStreamer& streamer);

    // Getters so the camera and world know where the player is
    float getX() const { return body.pos[0]; }
    float getY() const { return body.pos[1]; }
    float getZ() const { return body.pos[2]; }

private:
    float speed_;
};