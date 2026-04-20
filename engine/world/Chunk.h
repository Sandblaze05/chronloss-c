#pragma once

#include <cstdint>
#include <vector>
#include <glad/glad.h>
#include <mutex>
#include <atomic>

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
    std::vector<float> generateMesh();
    void addFace(std::vector<float>& vertices, int x, int y, int z, std::uint8_t blockId, FaceDirection face);
    void updateMesh();
    // Copy voxel data from another chunk instance (used by streamer worker output).
    void copyBlocksFrom(const Chunk& other);
    // Thread-safe: set a mesh produced on a worker thread for later GPU upload
    void setPendingMesh(std::vector<float>&& v);
    // To be called on the main GL thread to upload any pending mesh
    void uploadPendingMesh();
    bool hasPendingMesh() const;
    bool hasVoxelData() const;
    void render();

private:
    std::vector<std::uint8_t> blocks_;

    // pending mesh data produced on worker threads
    mutable std::mutex pendingMutex_;
    std::vector<float> pendingVertices_;
    std::atomic_bool pendingUpload_{false};
    std::atomic_bool hasVoxelData_{false};

    static int indexOf(int x, int y, int z);
};
