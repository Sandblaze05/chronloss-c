#include "Chunk.h"
#include <GLFW/glfw3.h>

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

void Chunk::addFace(std::vector<float>& vertices, int x, int y, int z, FaceDirection face) {
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);
    float fz = static_cast<float>(z);

    // standard UVs for a square
    float uvs[6][2] = {
        {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f},
        {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}
    };

    switch (face) {
        case FACE_TOP: // Y + 1
            vertices.insert(vertices.end(), {fx, fy + 1.0f, fz,          uvs[0][0], uvs[0][1]});
            vertices.insert(vertices.end(), {fx, fy + 1.0f, fz + 1.0f,   uvs[1][0], uvs[1][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy + 1.0f, fz + 1.0f, uvs[2][0], uvs[2][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy + 1.0f, fz + 1.0f, uvs[3][0], uvs[3][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy + 1.0f, fz,   uvs[4][0], uvs[4][1]});
            vertices.insert(vertices.end(), {fx, fy + 1.0f, fz,          uvs[5][0], uvs[5][1]});
            break;

        case FACE_BOTTOM: // Y (Base)
            vertices.insert(vertices.end(), {fx, fy, fz + 1.0f,          uvs[0][0], uvs[0][1]});
            vertices.insert(vertices.end(), {fx, fy, fz,                 uvs[1][0], uvs[1][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy, fz,          uvs[2][0], uvs[2][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy, fz,          uvs[3][0], uvs[3][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy, fz + 1.0f,   uvs[4][0], uvs[4][1]});
            vertices.insert(vertices.end(), {fx, fy, fz + 1.0f,          uvs[5][0], uvs[5][1]});
            break;

        case FACE_LEFT: // X (Base)
            vertices.insert(vertices.end(), {fx, fy, fz,                 uvs[0][0], uvs[0][1]});
            vertices.insert(vertices.end(), {fx, fy, fz + 1.0f,          uvs[1][0], uvs[1][1]});
            vertices.insert(vertices.end(), {fx, fy + 1.0f, fz + 1.0f,   uvs[2][0], uvs[2][1]});
            vertices.insert(vertices.end(), {fx, fy + 1.0f, fz + 1.0f,   uvs[3][0], uvs[3][1]});
            vertices.insert(vertices.end(), {fx, fy + 1.0f, fz,          uvs[4][0], uvs[4][1]});
            vertices.insert(vertices.end(), {fx, fy, fz,                 uvs[5][0], uvs[5][1]});
            break;

        case FACE_RIGHT: // X + 1
            vertices.insert(vertices.end(), {fx + 1.0f, fy, fz + 1.0f,   uvs[0][0], uvs[0][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy, fz,          uvs[1][0], uvs[1][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy + 1.0f, fz,   uvs[2][0], uvs[2][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy + 1.0f, fz,   uvs[3][0], uvs[3][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy + 1.0f, fz + 1.0f, uvs[4][0], uvs[4][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy, fz + 1.0f,   uvs[5][0], uvs[5][1]});
            break;

        case FACE_FRONT: // Z + 1
            vertices.insert(vertices.end(), {fx, fy, fz + 1.0f,          uvs[0][0], uvs[0][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy, fz + 1.0f,   uvs[1][0], uvs[1][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy + 1.0f, fz + 1.0f, uvs[2][0], uvs[2][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy + 1.0f, fz + 1.0f, uvs[3][0], uvs[3][1]});
            vertices.insert(vertices.end(), {fx, fy + 1.0f, fz + 1.0f,   uvs[4][0], uvs[4][1]});
            vertices.insert(vertices.end(), {fx, fy, fz + 1.0f,          uvs[5][0], uvs[5][1]});
            break;

        case FACE_BACK: // Z (Base)
            vertices.insert(vertices.end(), {fx + 1.0f, fy, fz,          uvs[0][0], uvs[0][1]});
            vertices.insert(vertices.end(), {fx, fy, fz,                 uvs[1][0], uvs[1][1]});
            vertices.insert(vertices.end(), {fx, fy + 1.0f, fz,          uvs[2][0], uvs[2][1]});
            vertices.insert(vertices.end(), {fx, fy + 1.0f, fz,          uvs[3][0], uvs[3][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy + 1.0f, fz,   uvs[4][0], uvs[4][1]});
            vertices.insert(vertices.end(), {fx + 1.0f, fy, fz,          uvs[5][0], uvs[5][1]});
            break;
    }
}

std::vector<float> Chunk::generateMesh() {
    std::vector<float> vertices;

    for (int y = 0; y < kSizeY; y++) {
        for (int z = 0; z < kSizeZ; z++) {
            for (int x = 0; x < kSizeX; x++) {
                std::uint8_t blockId = getBlock(x, y, z);
                if (blockId == 0) { // ignore air
                    continue;
                }

                // right face (x + 1)
                if (getBlock(x + 1, y, z) == 0) {
                    addFace(vertices, x, y, z, FACE_RIGHT);
                }
                // left face (x - 1)
                if (getBlock(x - 1, y, z) == 0) {
                    addFace(vertices, x, y, z, FACE_LEFT);
                }
                // top face (y + 1)
                if (getBlock(x, y + 1, z) == 0) {
                    addFace(vertices, x, y, z, FACE_TOP);
                }
                // top face (y - 1)
                if (getBlock(x, y - 1, z) == 0) {
                    addFace(vertices, x, y, z, FACE_BOTTOM);
                }
                // front face (z + 1)
                if (getBlock(x, y, z + 1) == 0) {
                    addFace(vertices, x, y, z, FACE_FRONT);
                }
                // back face (z - 1)
                if (getBlock(x, y, z - 1) == 0) {
                    addFace(vertices, x, y, z, FACE_BACK);
                }
            }
        }
    }
    return vertices;
}

void Chunk::updateMesh() {
    std::vector<float> vertices = generateMesh();

    vertexCount_ = vertices.size() / 5;

    if (vao_ == 0) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    // The size of one full vertex is now 5 floats
    GLsizei stride = 5 * sizeof(float);

    // position attribute 
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    
    // uv attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void Chunk::render() {
    if (vertexCount_ == 0) { // its air
        return;
    }

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    glBindVertexArray(0);
}
