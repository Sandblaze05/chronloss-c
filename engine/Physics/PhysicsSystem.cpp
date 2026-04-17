#include "PhysicsSystem.h"
#include <cmath>
#include <engine/world/ChunkStreamer.h>
#include <engine/world/Block.h>

void PhysicsSystem::update(PhysicsBody& body, float dt, ChunkStreamer& world) {
    applyGravity(body, dt);
    resolveCollision(body, world, dt);
}

void PhysicsSystem::applyGravity(PhysicsBody& body, float dt) {
    const float gravity = 27.0f;
    body.vel[1] -= gravity * dt;
}

void PhysicsSystem::integrate(PhysicsBody& body, float dt) {
    body.pos[0] += body.vel[0] * dt;
    body.pos[1] += body.vel[1] * dt;
    body.pos[2] += body.vel[2] * dt;
}

bool PhysicsSystem::checkCollision(const PhysicsBody& body, ChunkStreamer& world) {
    float minX = body.pos[0] - body.width / 2;
    float maxX = body.pos[0] + body.width / 2;
    
    float minY = body.pos[1];
    float maxY = body.pos[1] + body.height;

    float minZ = body.pos[2] - body.width / 2;
    float maxZ = body.pos[2] + body.width / 2;

    const int minXi = static_cast<int>(std::floor(minX));
    const int maxXi = static_cast<int>(std::floor(maxX));
    const int minYi = static_cast<int>(std::floor(minY));
    const int maxYi = static_cast<int>(std::floor(maxY));
    const int minZi = static_cast<int>(std::floor(minZ));
    const int maxZi = static_cast<int>(std::floor(maxZ));

    for (int x = minXi; x <= maxXi; x++) {
        for (int y = minYi; y <= maxYi; y++) {
            for (int z = minZi; z <= maxZi; z++) {
                if (BlockRegistry::get(world.getBlockAtWorld(x, y, z)).isSolid()) {
                    return true;
                }
            }
        }
    }

    return false;
}

void PhysicsSystem::resolveCollision(PhysicsBody& body, ChunkStreamer& world, float dt) {
    body.onGround = false;

    // x axis
    body.pos[0] += body.vel[0] * dt;
    if (checkCollision(body, world)) {
        body.pos[0] -= body.vel[0] * dt;
        body.vel[0] = 0;
    }
    // y
    body.pos[1] += body.vel[1] * dt;
    if (checkCollision(body, world)) {
        body.pos[1] -= body.vel[1] * dt;
        if (body.vel[1] < 0) body.onGround = true; // no |y|
        body.vel[1] = 0;
    }
     // z
    body.pos[2] += body.vel[2] * dt;
    if (checkCollision(body, world)) {
        body.pos[2] -= body.vel[2] * dt;
        body.vel[2] = 0;
    }
}
