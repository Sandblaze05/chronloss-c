#pragma once

#include <cstdint>
#include <unordered_map>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <cstddef>
#include <glad/glad.h>
#include "Chunk.h"

#include <cstdint>

struct ChunkCoord {
    int cx, cy, cz;
    bool operator==(const ChunkCoord& o) const {
        return cx == o.cx && cy == o.cy && cz == o.cz;
    }
};

struct ChunkCoordHash {
    std::size_t operator()(ChunkCoord const& c) const noexcept {
        // mix coordinates into a single hash
        std::uint64_t a = static_cast<std::uint32_t>(c.cx);
        std::uint64_t b = static_cast<std::uint32_t>(c.cy);
        std::uint64_t d = static_cast<std::uint32_t>(c.cz);
        std::uint64_t h = a + 0x9e3779b97f4a7c15ULL + (b<<6) + (b>>2);
        h ^= d + 0x9e3779b97f4a7c15ULL + (h<<6) + (h>>2);
        return static_cast<std::size_t>(h);
    }
};

class ChunkStreamer {
public:
    ChunkStreamer(std::uint64_t seed = 0, int radius = 6, int verticalRadius = 0);
    ~ChunkStreamer();

    // Called from main thread once per frame to ensure nearby chunks are requested
    void tick(float playerX, float playerY, float playerZ);

    // Render all loaded chunks (uploads pending meshes first)
    void renderAll();
    // Render all loaded chunks using a provided base MVP and shader program.
    // The provided MVP should be projection * view; this method will apply
    // a per-chunk model translation so chunk vertices (0..kSize) render
    // at their world positions.
    void renderAll(const float baseMVP[16], unsigned int shaderProgram,
                   std::size_t* outDrawCalls = nullptr,
                   std::size_t* outVertexCount = nullptr);

    // Fetch a block by absolute world coordinates. Returns 0 (Air) if the chunk
    // containing the position is not loaded.
    std::uint8_t getBlockAtWorld(int worldX, int worldY, int worldZ);

    void setBlockAtWorld(int worldX, int worldY, int worldZ, std::uint8_t blockId);

    // Returns true when the chunk containing this world position has generated voxel data.
    bool isBlockDataReadyAtWorld(int worldX, int worldY, int worldZ);

    size_t getLoadedChunkCount() {
        std::lock_guard<std::mutex> lock(mapMutex_);
        return chunks_.size();
    }

private:
    void workerLoop();
    void enqueueIfMissing(const ChunkCoord& c);
    void generateBlocksForChunk(Chunk& chunk, const ChunkCoord& c);

    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> chunks_;
    std::mutex mapMutex_;

    std::deque<ChunkCoord> queue_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;

    std::thread workerThread_;
    std::atomic_bool running_{false};

    std::uint64_t seed_ = 0;
    std::atomic<int> radius_ {8};
    // number of chunk layers vertically to load around player (0 = single Y layer)
    std::atomic<int> vRadius_ {0};
    // accumulated load time in nanoseconds and number of loads
    std::atomic<uint64_t> totalLoadNs_{0};
    std::atomic<uint64_t> loadCount_{0};
    // separate chunk generation and meshing timings
    std::atomic<uint64_t> totalGenNs_{0};
    std::atomic<uint64_t> genCount_{0};
    std::atomic<uint64_t> totalMeshNs_{0};
    std::atomic<uint64_t> meshCount_{0};

public:
    // Runtime accessors to allow debugging / changing render distance
    int getRadius() const { return radius_.load(std::memory_order_acquire); }
    void setRadius(int r) { radius_.store(r, std::memory_order_release); }
    int getVerticalRadius() const { return vRadius_.load(std::memory_order_acquire); }
    void setVerticalRadius(int vr) { vRadius_.store(vr, std::memory_order_release); }

    // Chunk load timing statistics (ms)
    // Returns average chunk generation+mesh time in milliseconds.
    double getAverageChunkLoadMs() const {
        uint64_t count = loadCount_.load(std::memory_order_acquire);
        if (count == 0) return 0.0;
        uint64_t totalNs = totalLoadNs_.load(std::memory_order_acquire);
        return static_cast<double>(totalNs) / 1e6 / static_cast<double>(count);
    }

    double getAverageChunkGenMs() const {
        uint64_t count = genCount_.load(std::memory_order_acquire);
        if (count == 0) return 0.0;
        uint64_t totalNs = totalGenNs_.load(std::memory_order_acquire);
        return static_cast<double>(totalNs) / 1e6 / static_cast<double>(count);
    }

    double getAverageChunkMeshMs() const {
        uint64_t count = meshCount_.load(std::memory_order_acquire);
        if (count == 0) return 0.0;
        uint64_t totalNs = totalMeshNs_.load(std::memory_order_acquire);
        return static_cast<double>(totalNs) / 1e6 / static_cast<double>(count);
    }

    // Reset accumulated timing stats
    void resetLoadStats() {
        totalLoadNs_.store(0, std::memory_order_release);
        loadCount_.store(0, std::memory_order_release);
        totalGenNs_.store(0, std::memory_order_release);
        genCount_.store(0, std::memory_order_release);
        totalMeshNs_.store(0, std::memory_order_release);
        meshCount_.store(0, std::memory_order_release);
    }
};
