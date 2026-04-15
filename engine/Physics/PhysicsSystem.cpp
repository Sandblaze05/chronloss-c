#include "PhysicsSystem.h"
#include <engine/world/ChunkStreamer.h>
#include <engine/world/Block.h>

void PhysicsSystem::update(PhysicsBody& body, float dt, ChunkStreamer& world) {
    applyGravity(body, dt);
    integrate(body, dt);
    resolveCollision(body, world, dt);
}

void PhysicsSystem::applyGravity(PhysicsBody& body, float dt) {
    const float gravity = 20.0f;
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

    for (int x = floor(minX); x <= floor(maxX); x++) {
        for (int y = floor(minY); y <= floor(maxY); y++) {
            for (int z = floor(minZ); z <= floor(maxZ); z++) {
                if (BlockRegistry::get(world.getBlockAtWorld(x, y, z)).isSolid()) {
                    return true;
                }
            }
        }
    }
}

void PhysicsSystem::resolveCollision(PhysicsBody& body, ChunkStreamer& world, float dt) {
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
        body.vel[1] = 0;
    }
     // z
    body.pos[2] += body.vel[2] * dt;
    if (checkCollision(body, world)) {
        body.pos[2] -= body.vel[2] * dt;
        body.vel[2] = 0;
    }
}
