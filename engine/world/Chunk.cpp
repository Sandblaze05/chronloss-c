#include "Chunk.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Block.h"

#include <array>
#include <utility>

namespace {

constexpr int kAtlasTilesPerRow = 16;
constexpr float kAtlasInvTilesPerRow = 1.0f / static_cast<float>(kAtlasTilesPerRow);

std::pair<int, int> atlasTileForBlockFace(std::uint8_t blockId, FaceDirection face) {
    // atlas layout (row 0):
    // tile 0: grass top, tile 1: grass side, tile 2: dirt, tile 3: stone, tile 4: bedrock, tile 5: water, tile 6: sand
    // tile 7: sandstone top, tile 8: sandstone side, tile 9: sandstone bottom, tile 10: gravel, tile 11: clay, tile 12: snow
    // tile 13: packed ice 
    switch (blockId) {
        case 5: // Grass_Block
            if (face == FACE_TOP) {
                return {0, 0};
            }
            if (face == FACE_BOTTOM) {
                return {2, 0};
            }
            return {1, 0};
        case 2: // Dirt
            return {2, 0};
        case 1: // Stone
            return {3, 0};
        case 4: // Bedrock
            return {4, 0};
        case 3:
            return {5, 0};
        case 6:
            return {6, 0};
        case 7:
            if (face == FACE_TOP) {
                return {7, 0};
            }
            if (face == FACE_BOTTOM) {
                return {9, 0};
            }
            return {8, 0};
        case 8:
            return {10, 0};
        case 9:
            return {11, 0};
        case 10:
            return {12, 0};
        case 11:
            return {13, 0};
        case 12: // Iron_Ore
            return {14, 0};
        case 13: // Gold_Ore
            return {15, 0};
        case 14: // Diamond_Ore
            return {0, 1};
        case 15: // Oak_Log
            if (face == FACE_TOP || face == FACE_BOTTOM) return {1, 1};
            return {2, 1};
        case 16: // Oak_Leaves
            return {3, 1};
        case 17: // Tall_Grass
            return {4, 1};
        case 18: // Cactus
            if (face == FACE_TOP) return {5, 1};
            if (face == FACE_BOTTOM) return {7, 1};
            return {6, 1};
        default:
            // Fallback to stone until more block textures are defined.
            return {3, 0};
    }
}

std::array<std::array<float, 2>, 6> faceUvsForTile(int tileX, int tileY, bool rotate90CW = false) {
    const float tileSize = kAtlasInvTilesPerRow;
    const float epsilon = tileSize * 0.02f;

    const float u0 = static_cast<float>(tileX) * tileSize + epsilon;
    const float v0 = static_cast<float>(tileY) * tileSize + epsilon;
    const float u1 = static_cast<float>(tileX + 1) * tileSize - epsilon;
    const float v1 = static_cast<float>(tileY + 1) * tileSize - epsilon;

    if (rotate90CW) {
        return {{{u0, v1}, {u1, v1}, {u1, v0}, {u1, v0}, {u0, v0}, {u0, v1}}};
    }

    return {{{u0, v0}, {u0, v1}, {u1, v1}, {u1, v1}, {u1, v0}, {u0, v0}}};
}

} // namespace

Chunk::Chunk() : blocks_(kSizeX * kSizeY * kSizeZ, 0u) {
}

std::uint8_t Chunk::getBlock(int x, int y, int z) const {
    if (x < 0 || x >= kSizeX ||
        y < 0 || y >= kSizeY ||
        z < 0 || z >= kSizeZ) {
        return 0;
    }
    return blocks_[indexOf(x, y, z)];
}

void Chunk::setBlock(int x, int y, int z, std::uint8_t id) {
    if (x < 0 || x >= kSizeX ||
        y < 0 || y >= kSizeY ||
        z < 0 || z >= kSizeZ) {
        return;
    }
    blocks_[indexOf(x, y, z)] = id;
}

int Chunk::indexOf(int x, int y, int z) {
    return x + (z * kSizeX) + (y * kSizeX * kSizeZ);
}

void Chunk::addFace(std::vector<float>& vertices, int x, int y, int z, std::uint8_t blockId, FaceDirection face) {
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);
    float fz = static_cast<float>(z);

    const auto [tileX, tileY] = atlasTileForBlockFace(blockId, face);
    const bool rotateSideFace = (face == FACE_LEFT || face == FACE_RIGHT ||
                                 face == FACE_FRONT || face == FACE_BACK);
    const auto uvs = faceUvsForTile(tileX, tileY, rotateSideFace);

    // nx, ny, nz per face
    float nx = 0, ny = 0, nz = 0;
    switch (face) {
        case FACE_TOP:    ny =  1.0f; break;
        case FACE_BOTTOM: ny = -1.0f; break;
        case FACE_LEFT:   nx = -1.0f; break;
        case FACE_RIGHT:  nx =  1.0f; break;
        case FACE_FRONT:  nz =  1.0f; break;
        case FACE_BACK:   nz = -1.0f; break;
    }

    // helper to push one vertex: pos(3) + normal(3) + uv(2)
    auto push = [&](float px, float py, float pz, int uvi) {
        vertices.insert(vertices.end(), {px, py, pz, nx, ny, nz, uvs[static_cast<std::size_t>(uvi)][0], uvs[static_cast<std::size_t>(uvi)][1]});
    };

    switch (face) {
        case FACE_TOP:
            push(fx,        fy+1, fz,        0);
            push(fx,        fy+1, fz+1,      1);
            push(fx+1,      fy+1, fz+1,      2);
            push(fx+1,      fy+1, fz+1,      3);
            push(fx+1,      fy+1, fz,        4);
            push(fx,        fy+1, fz,        5);
            break;
        case FACE_BOTTOM:
            push(fx,        fy,   fz+1,      0);
            push(fx,        fy,   fz,        1);
            push(fx+1,      fy,   fz,        2);
            push(fx+1,      fy,   fz,        3);
            push(fx+1,      fy,   fz+1,      4);
            push(fx,        fy,   fz+1,      5);
            break;
        case FACE_LEFT:
            push(fx,        fy,   fz,        0);
            push(fx,        fy,   fz+1,      1);
            push(fx,        fy+1, fz+1,      2);
            push(fx,        fy+1, fz+1,      3);
            push(fx,        fy+1, fz,        4);
            push(fx,        fy,   fz,        5);
            break;
        case FACE_RIGHT:
            push(fx+1,      fy,   fz+1,      0);
            push(fx+1,      fy,   fz,        1);
            push(fx+1,      fy+1, fz,        2);
            push(fx+1,      fy+1, fz,        3);
            push(fx+1,      fy+1, fz+1,      4);
            push(fx+1,      fy,   fz+1,      5);
            break;
        case FACE_FRONT:
            push(fx,        fy,   fz+1,      0);
            push(fx+1,      fy,   fz+1,      1);
            push(fx+1,      fy+1, fz+1,      2);
            push(fx+1,      fy+1, fz+1,      3);
            push(fx,        fy+1, fz+1,      4);
            push(fx,        fy,   fz+1,      5);
            break;
        case FACE_BACK:
            push(fx+1,      fy,   fz,        0);
            push(fx,        fy,   fz,        1);
            push(fx,        fy+1, fz,        2);
            push(fx,        fy+1, fz,        3);
            push(fx+1,      fy+1, fz,        4);
            push(fx+1,      fy,   fz,        5);
            break;
    }
}

std::vector<float> Chunk::generateMesh() {
    std::vector<float> vertices;

    auto isRenderableBlock = [](std::uint8_t id) {
        return BlockRegistry::get(id).isSolid() || id == 3 || id == 17;
    };

    auto isFaceOccluded = [](std::uint8_t selfId, std::uint8_t neighborId) {
        if (selfId == 3) {
            // Water should not render internal faces against solid blocks or other water.
            return BlockRegistry::get(neighborId).isSolid() || neighborId == 3;
        }
        if (selfId == 17) {
            // Tall grass only renders faces against air.
            return neighborId != 0;
        }
        if (selfId == 16) {
            // Oak leaves: don't cull faces between other leaves for visual density,
            // but do cull against other solid blocks.
            return BlockRegistry::get(neighborId).isSolid() && neighborId != 16;
        }

        if (neighborId == 18 || neighborId == 17) {
            return false;
        }
        
        return BlockRegistry::get(neighborId).isSolid();
    };

    for (int y = 0; y < kSizeY; y++) {
        for (int z = 0; z < kSizeZ; z++) {
            for (int x = 0; x < kSizeX; x++) {
                std::uint8_t blockId = getBlock(x, y, z);
                if (!isRenderableBlock(blockId)) {
                    continue;
                }

                if (blockId == 17) {
                    // Tall grass is a cross model (centered in the block)
                    float fx = static_cast<float>(x);
                    float fy = static_cast<float>(y);
                    float fz = static_cast<float>(z);
                    float cx = fx + 0.5f;  // center X
                    float cz = fz + 0.5f;  // center Z
                    float r = 0.4f;  // half-width from center
                    
                    const auto [tileX, tileY] = atlasTileForBlockFace(17, FACE_FRONT);
                    const auto uvs = faceUvsForTile(tileX, tileY, true);

                    auto push = [&](float px, float py, float pz, float nx, float ny, float nz, int uvi) {
                        vertices.insert(vertices.end(), {px, py, pz, nx, ny, nz, uvs[static_cast<std::size_t>(uvi)][0], uvs[static_cast<std::size_t>(uvi)][1]});
                    };

                    // Quad 1: left-right orientation (perpendicular to camera when looking north/south)
                    // Front face
                    push(cx - r, fy, cz, 0.0f, 0.0f, 1.0f, 0);
                    push(cx + r, fy, cz, 0.0f, 0.0f, 1.0f, 1);
                    push(cx + r, fy + 1, cz, 0.0f, 0.0f, 1.0f, 2);
                    push(cx + r, fy + 1, cz, 0.0f, 0.0f, 1.0f, 3);
                    push(cx - r, fy + 1, cz, 0.0f, 0.0f, 1.0f, 4);
                    push(cx - r, fy, cz, 0.0f, 0.0f, 1.0f, 5);
                    
                    // Back face
                    push(cx + r, fy, cz, 0.0f, 0.0f, -1.0f, 0);
                    push(cx - r, fy, cz, 0.0f, 0.0f, -1.0f, 1);
                    push(cx - r, fy + 1, cz, 0.0f, 0.0f, -1.0f, 2);
                    push(cx - r, fy + 1, cz, 0.0f, 0.0f, -1.0f, 3);
                    push(cx + r, fy + 1, cz, 0.0f, 0.0f, -1.0f, 4);
                    push(cx + r, fy, cz, 0.0f, 0.0f, -1.0f, 5);

                    // Quad 2: front-back orientation (perpendicular to camera when looking east/west)
                    // Right face
                    push(cx, fy, cz - r, 1.0f, 0.0f, 0.0f, 0);
                    push(cx, fy, cz + r, 1.0f, 0.0f, 0.0f, 1);
                    push(cx, fy + 1, cz + r, 1.0f, 0.0f, 0.0f, 2);
                    push(cx, fy + 1, cz + r, 1.0f, 0.0f, 0.0f, 3);
                    push(cx, fy + 1, cz - r, 1.0f, 0.0f, 0.0f, 4);
                    push(cx, fy, cz - r, 1.0f, 0.0f, 0.0f, 5);
                    
                    // Left face
                    push(cx, fy, cz + r, -1.0f, 0.0f, 0.0f, 0);
                    push(cx, fy, cz - r, -1.0f, 0.0f, 0.0f, 1);
                    push(cx, fy + 1, cz - r, -1.0f, 0.0f, 0.0f, 2);
                    push(cx, fy + 1, cz - r, -1.0f, 0.0f, 0.0f, 3);
                    push(cx, fy + 1, cz + r, -1.0f, 0.0f, 0.0f, 4);
                    push(cx, fy, cz + r, -1.0f, 0.0f, 0.0f, 5);
                    
                    continue;
                }

                if (blockId == 18) {
                    // Cactus is slightly inset (14/16 width/depth, full height)
                    float fx = static_cast<float>(x);
                    float fy = static_cast<float>(y);
                    float fz = static_cast<float>(z);
                    const float m = 1.0f / 16.0f; // margin
                    
                    // Decouple geometry face from texture face to allow overriding
                    auto emitCactusFace = [&](FaceDirection geoFace, FaceDirection texFace, float nx, float ny, float nz) {
                        const auto [tileX, tileY] = atlasTileForBlockFace(18, texFace);
                        const bool rotateTexture = (geoFace == FACE_LEFT || geoFace == FACE_RIGHT ||
                                                    geoFace == FACE_FRONT || geoFace == FACE_BACK);
                        const auto uvs = faceUvsForTile(tileX, tileY, rotateTexture);
                        auto push = [&](float px, float py, float pz, int uvi) {
                            vertices.insert(vertices.end(), {px, py, pz, nx, ny, nz, uvs[static_cast<std::size_t>(uvi)][0], uvs[static_cast<std::size_t>(uvi)][1]});
                        };
                        switch(geoFace) {
                            case FACE_TOP:
                                push(fx+m,   fy+1, fz+m,   0);
                                push(fx+1-m, fy+1, fz+m,   1);
                                push(fx+1-m, fy+1, fz+1-m, 2);
                                push(fx+1-m, fy+1, fz+1-m, 3);
                                push(fx+m,   fy+1, fz+1-m, 4);
                                push(fx+m,   fy+1, fz+m,   5);
                                break;
                            case FACE_BOTTOM:
                                push(fx+m,   fy,   fz+1-m, 0);
                                push(fx+m,   fy,   fz+m,   1);
                                push(fx+1-m, fy,   fz+m,   2);
                                push(fx+1-m, fy,   fz+m,   3);
                                push(fx+1-m, fy,   fz+1-m, 4);
                                push(fx+m,   fy,   fz+1-m, 5);
                                break;
                            case FACE_LEFT:
                                push(fx+m, fy,   fz+m,   0);
                                push(fx+m, fy,   fz+1-m, 1);
                                push(fx+m, fy+1, fz+1-m, 2);
                                push(fx+m, fy+1, fz+1-m, 3);
                                push(fx+m, fy+1, fz+m,   4);
                                push(fx+m, fy,   fz+m,   5);
                                break;
                            case FACE_RIGHT:
                                push(fx+1-m, fy,   fz+1-m, 0);
                                push(fx+1-m, fy,   fz+m,   1);
                                push(fx+1-m, fy+1, fz+m,   2);
                                push(fx+1-m, fy+1, fz+m,   3);
                                push(fx+1-m, fy+1, fz+1-m, 4);
                                push(fx+1-m, fy,   fz+1-m, 5);
                                break;
                            case FACE_FRONT:
                                push(fx+m,   fy,   fz+1-m, 0);
                                push(fx+1-m, fy,   fz+1-m, 1);
                                push(fx+1-m, fy+1, fz+1-m, 2);
                                push(fx+1-m, fy+1, fz+1-m, 3);
                                push(fx+m,   fy+1, fz+1-m, 4);
                                push(fx+m,   fy,   fz+1-m, 5);
                                break;
                            case FACE_BACK:
                                push(fx+1-m, fy,   fz+m,   0);
                                push(fx+m,   fy,   fz+m,   1);
                                push(fx+m,   fy+1, fz+m,   2);
                                push(fx+m,   fy+1, fz+m,   3);
                                push(fx+1-m, fy+1, fz+m,   4);
                                push(fx+1-m, fy,   fz+m,   5);
                                break;
                        }
                    };

                    // Side faces remain normal
                    emitCactusFace(FACE_LEFT,  FACE_LEFT,  -1, 0, 0);
                    emitCactusFace(FACE_RIGHT, FACE_RIGHT,  1, 0, 0);
                    emitCactusFace(FACE_FRONT, FACE_FRONT,  0, 0, 1);
                    emitCactusFace(FACE_BACK,  FACE_BACK,   0, 0, -1);
                    
                    std::uint8_t topNeighbor = getBlock(x, y + 1, z);
                    if (topNeighbor != 18) {
                        // Absolute top of the cactus: Use normal spiked top texture
                        emitCactusFace(FACE_TOP, FACE_TOP, 0, 1, 0);
                    } else {
                        // Mid/Bottom segment: Render the top face geometry to seal the view, 
                        // but paint it with the FACE_BOTTOM (cross section) texture
                        emitCactusFace(FACE_TOP, FACE_BOTTOM, 0, 1, 0);
                    }
                    
                    std::uint8_t bottomNeighbor = getBlock(x, y - 1, z);
                    if (bottomNeighbor != 18 && bottomNeighbor != 0) {
                        emitCactusFace(FACE_BOTTOM, FACE_BOTTOM, 0, -1, 0);
                    }
                    continue;
                }
                // right face (x + 1)
                if (!isFaceOccluded(blockId, getBlock(x + 1, y, z))) {
                    addFace(vertices, x, y, z, blockId, FACE_RIGHT);
                }
                // left face (x - 1)
                if (!isFaceOccluded(blockId, getBlock(x - 1, y, z))) {
                    addFace(vertices, x, y, z, blockId, FACE_LEFT);
                }
                // top face (y + 1)
                if (!isFaceOccluded(blockId, getBlock(x, y + 1, z))) {
                    addFace(vertices, x, y, z, blockId, FACE_TOP);
                }
                // top face (y - 1)
                if (!isFaceOccluded(blockId, getBlock(x, y - 1, z))) {
                    addFace(vertices, x, y, z, blockId, FACE_BOTTOM);
                }
                // front face (z + 1)
                if (!isFaceOccluded(blockId, getBlock(x, y, z + 1))) {
                    addFace(vertices, x, y, z, blockId, FACE_FRONT);
                }
                // back face (z - 1)
                if (!isFaceOccluded(blockId, getBlock(x, y, z - 1))) {
                    addFace(vertices, x, y, z, blockId, FACE_BACK);
                }
            }
        }
    }
    return vertices;
}

void Chunk::updateMesh() {
    std::vector<float> vertices = generateMesh();

    vertexCount_ = static_cast<int>(vertices.size() / 8);

    if (vao_ == 0) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    // The size of one full vertex is position(3) + normal(3) + uv(2).
    GLsizei stride = 8 * sizeof(float);

    // position attribute 
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    
    // normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void Chunk::copyBlocksFrom(const Chunk& other) {
    blocks_ = other.blocks_;
    hasVoxelData_.store(true, std::memory_order_release);
}

void Chunk::render() {
    if (vertexCount_ == 0) { // its air
        return;
    }

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    glBindVertexArray(0);
}

void Chunk::setPendingMesh(std::vector<float>&& v) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingVertices_.swap(v);
    pendingUpload_.store(true, std::memory_order_release);
}

void Chunk::uploadPendingMesh() {
    if (!pendingUpload_.load(std::memory_order_acquire)) return;

    std::vector<float> verts;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        verts.swap(pendingVertices_);
        pendingUpload_.store(false, std::memory_order_release);
    }

    vertexCount_ = static_cast<int>(verts.size() / 8);

    if (vao_ == 0) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

    GLsizei stride = 8 * sizeof(float);

    // position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);

    // normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // uv attribute
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

bool Chunk::hasPendingMesh() const {
    return pendingUpload_.load(std::memory_order_acquire);
}

bool Chunk::hasVoxelData() const {
    return hasVoxelData_.load(std::memory_order_acquire);
}
