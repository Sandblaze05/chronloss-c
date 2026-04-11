#pragma once

#include <cstdint>
#include <vector>
#include <glad/glad.h>

enum FaceDirection {
    FACE_TOP, FACE_BOTTOM,
    FACE_LEFT, FACE_RIGHT,
    FACE_FRONT, FACE_BACK
};

class Chunk {
public:
    static constexpr int kSizeX = 16;
    static constexpr int kSizeY = 256;
    static constexpr int kSizeZ = 16;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    int vertexCount_ = 0;

    Chunk();

    std::uint8_t getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, std::uint8_t id);
    std::vector<float> Chunk::generateMesh();
    void addFace(std::vector<float>& vertices, int x, int y, int z, FaceDirection face);
    void updateMesh();
    void render();

private:
    std::vector<std::uint8_t> blocks_;

    static int indexOf(int x, int y, int z);
};
