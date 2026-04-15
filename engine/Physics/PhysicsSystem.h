#pragma once
#include <engine/world/ChunkStreamer.h>

struct PhysicsBody {
    float pos[3];
    float vel[3];

    float width = 0.6f;
    float height = 1.8f;

    bool onGround = false;
};

class PhysicsSystem {
public:
    void update(PhysicsBody& body, float dt, ChunkStreamer& world);

private:
    void applyGravity(PhysicsBody& body, float dt);
    void integrate(PhysicsBody& body, float dt);

    void resolveCollision(PhysicsBody& body, ChunkStreamer& world, float dt);
    bool checkCollision(const PhysicsBody& body, ChunkStreamer& world);
};