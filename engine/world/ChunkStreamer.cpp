#include "ChunkStreamer.h"
#include "TerrainGenerationSystem.h"
#include <GLFW/glfw3.h>
#include <cassert>
#include <algorithm>
#include <chrono>

ChunkStreamer::ChunkStreamer(std::uint64_t seed, int radius, int verticalRadius)
    : seed_(seed), radius_(radius), vRadius_(verticalRadius) {
    running_.store(true);
    workerThread_ = std::thread(&ChunkStreamer::workerLoop, this);
}

ChunkStreamer::~ChunkStreamer() {
    running_.store(false);
    queueCv_.notify_all();
    if (workerThread_.joinable()) workerThread_.join();
}

void ChunkStreamer::enqueueIfMissing(const ChunkCoord& c) {
    // Acquire both mutexes together to avoid lock-order inversion with the
    // worker thread (which locks queueMutex_ then mapMutex_). Using
    // std::scoped_lock prevents deadlocks by locking both safely.
    std::scoped_lock lock(mapMutex_, queueMutex_);
    auto it = chunks_.find(c);
    if (it == chunks_.end()) {
        // create empty slot so worker can find it
        chunks_.emplace(c, std::make_unique<Chunk>());
        // push request
        queue_.push_back(c);
        queueCv_.notify_one();
    }
}

void ChunkStreamer::tick(float playerX, float playerY, float playerZ) {
    // compute player chunk coords
    int pcx = static_cast<int>(std::floor(playerX / Chunk::kSizeX));
    int pcy = static_cast<int>(std::floor(playerY / Chunk::kSizeY));
    int pcz = static_cast<int>(std::floor(playerZ / Chunk::kSizeZ));

    // Read atomic radii once for this tick to avoid repeated loads
    int radius = radius_.load(std::memory_order_acquire);
    int vRadius = vRadius_.load(std::memory_order_acquire);

    // Load new chunks within radius
    for (int dy = -vRadius; dy <= vRadius; ++dy) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                ChunkCoord cc{pcx + dx, pcy + dy, pcz + dz};
                enqueueIfMissing(cc);
            }
        }
    }

    // Unload chunks that are too far away
    std::lock_guard<std::mutex> lock(mapMutex_);
    for (auto it = chunks_.begin(); it != chunks_.end(); ) {
        const ChunkCoord& cc = it->first;
        
        // Calculate grid distance from player
        int distX = std::abs(cc.cx - pcx);
        int distY = std::abs(cc.cy - pcy);
        int distZ = std::abs(cc.cz - pcz);

        // If the chunk is outside our radius + 1 buffer, delete it
        if (distX > radius + 1 || distY > vRadius + 1 || distZ > radius + 1) {
            it = chunks_.erase(it); // Erase frees the memory and returns the next iterator
        } else {
            ++it;
        }
    }

    {
        std::lock_guard<std::mutex> qlock(queueMutex_);
        std::sort(queue_.begin(), queue_.end(), [&](const ChunkCoord& a, const ChunkCoord& b){
            int distA = (a.cx - pcx) * (a.cx - pcx) +
                        (a.cy - pcy) * (a.cy - pcy) +
                        (a.cz - pcz) * (a.cz - pcz);
            
            int distB = (b.cx - pcx) * (b.cx - pcx) +
                        (b.cy - pcy) * (b.cy - pcy) +
                        (b.cz - pcz) * (b.cz - pcz);

            return distA < distB;
        });
    }
}

void ChunkStreamer::renderAll() {
    std::lock_guard<std::mutex> lock(mapMutex_);
    for (auto& kv : chunks_) {
        Chunk* c = kv.second.get();
        if (c->hasPendingMesh()) c->uploadPendingMesh();
        c->render();
    }
}

void ChunkStreamer::renderAll(const float baseMVP[16], unsigned int shaderProgram,
                              std::size_t* outDrawCalls,
                              std::size_t* outVertexCount) {
    auto mulMat = [](const float a[16], const float b[16], float out[16]) {
        // out = a * b (column-major)
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int i = 0; i < 4; ++i) {
                    sum += a[i*4 + row] * b[col*4 + i];
                }
                out[col*4 + row] = sum;
            }
        }
    };

    std::size_t drawCalls = 0;
    std::size_t vertexCount = 0;

    std::lock_guard<std::mutex> lock(mapMutex_);
    for (auto& kv : chunks_) {
        ChunkCoord coord = kv.first;
        Chunk* c = kv.second.get();
        if (c->hasPendingMesh()) c->uploadPendingMesh();

        // compute model translation for this chunk in world units
        float tx = static_cast<float>(coord.cx * Chunk::kSizeX);
        float ty = static_cast<float>(coord.cy * Chunk::kSizeY);
        float tz = static_cast<float>(coord.cz * Chunk::kSizeZ);

        // build translation matrix (column-major)
        float T[16] = {0};
        T[0] = 1.0f; T[5] = 1.0f; T[10] = 1.0f; T[15] = 1.0f;
        T[12] = tx; T[13] = ty; T[14] = tz;

        float finalMVP[16];
        mulMat(baseMVP, T, finalMVP);

        // set MVP uniform on provided shader program
        GLint loc = glGetUniformLocation(static_cast<GLuint>(shaderProgram), "MVP");
        if (loc >= 0) {
            glUseProgram(shaderProgram);
            glUniformMatrix4fv(loc, 1, GL_FALSE, finalMVP);
        }

        if (c->vertexCount_ > 0) {
            ++drawCalls;
            vertexCount += static_cast<std::size_t>(c->vertexCount_);
        }
        c->render();
    }

    if (outDrawCalls) {
        *outDrawCalls = drawCalls;
    }
    if (outVertexCount) {
        *outVertexCount = vertexCount;
    }
}

void ChunkStreamer::workerLoop() {
    while (running_.load()) {
        ChunkCoord work;
        {
            std::unique_lock<std::mutex> qlock(queueMutex_);
            queueCv_.wait(qlock, [&]() { return !queue_.empty() || !running_.load(); });
            if (!running_.load()) break;
            work = queue_.front();
            queue_.pop_front();
        }

        // generate temporary chunk data and measure generation and mesh times separately
        auto genStart = std::chrono::steady_clock::now();
        Chunk temp;
        TerrainGenerationSystem::generateChunkTerrain(seed_, temp, work.cx, work.cy, work.cz);
        auto genEnd = std::chrono::steady_clock::now();

        auto meshStart = std::chrono::steady_clock::now();
        std::vector<float> verts = temp.generateMesh();
        auto meshEnd = std::chrono::steady_clock::now();

        uint64_t genNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(genEnd - genStart).count());
        uint64_t meshNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(meshEnd - meshStart).count());
        totalGenNs_.fetch_add(genNs, std::memory_order_relaxed);
        genCount_.fetch_add(1, std::memory_order_relaxed);
        totalMeshNs_.fetch_add(meshNs, std::memory_order_relaxed);
        meshCount_.fetch_add(1, std::memory_order_relaxed);
        totalLoadNs_.fetch_add(genNs + meshNs, std::memory_order_relaxed);
        loadCount_.fetch_add(1, std::memory_order_relaxed);

        // deliver vertices to the main-thread-owned chunk if still present
        std::lock_guard<std::mutex> lock(mapMutex_);
        auto it = chunks_.find(work);
        if (it != chunks_.end()) {
            it->second->copyBlocksFrom(temp);
            it->second->setPendingMesh(std::move(verts));
        }
    }
}

std::uint8_t ChunkStreamer::getBlockAtWorld(int worldX, int worldY, int worldZ) {
    // Convert World Pos to Chunk Pos
    int cx = static_cast<int>(std::floor(static_cast<float>(worldX) / Chunk::kSizeX));
    int cy = static_cast<int>(std::floor(static_cast<float>(worldY) / Chunk::kSizeY));
    int cz = static_cast<int>(std::floor(static_cast<float>(worldZ) / Chunk::kSizeZ));

    // Find the chunk
    ChunkCoord cc{cx, cy, cz};
    std::lock_guard<std::mutex> lock(mapMutex_);
    auto it = chunks_.find(cc);
    if (it == chunks_.end()) return 0; // If chunk isn't loaded, return Air
    if (!it->second->hasVoxelData()) return 0; // placeholder chunk without generated voxel data

    // Convert World Pos to Local Chunk Pos (0 to kSize-1)
    int lx = worldX - (cx * Chunk::kSizeX);
    int ly = worldY - (cy * Chunk::kSizeY);
    int lz = worldZ - (cz * Chunk::kSizeZ);

    return it->second->getBlock(lx, ly, lz);
}

void ChunkStreamer::setBlockAtWorld(int worldX, int worldY, int worldZ, std::uint8_t blockId) {
    int cx = static_cast<int>(std::floor(static_cast<float>(worldX) / Chunk::kSizeX));
    int cy = static_cast<int>(std::floor(static_cast<float>(worldY) / Chunk::kSizeY));
    int cz = static_cast<int>(std::floor(static_cast<float>(worldZ) / Chunk::kSizeZ));

    int lx = worldX - (cx * Chunk::kSizeX);
    int ly = worldY - (cy * Chunk::kSizeY);
    int lz = worldZ - (cz * Chunk::kSizeZ);

    auto updateChunkMesh = [&](int chunkX, int chunkY, int chunkZ) {
        ChunkCoord cc{chunkX, chunkY, chunkZ};
        std::lock_guard<std::mutex> lock(mapMutex_);
        auto it = chunks_.find(cc);
        if (it != chunks_.end() && it->second->hasVoxelData()) {
            it->second->updateMesh();
        }
    };

    {
        ChunkCoord cc{cx, cy, cz};
        std::lock_guard<std::mutex> lock(mapMutex_);
        auto it = chunks_.find(cc);
        if (it != chunks_.end() && it->second->hasVoxelData()) {
            it->second->setBlock(lx, ly, lz, blockId);
        } else {
            return;
        }
    }

    updateChunkMesh(cx, cy, cz);

    if (lx == 0) updateChunkMesh(cx - 1, cy, cz);
    else if (lx == Chunk::kSizeX - 1) updateChunkMesh(cx + 1, cy, cz);

    if (ly == 0) updateChunkMesh(cx, cy - 1, cz);
    else if (ly == Chunk::kSizeY - 1) updateChunkMesh(cx, cy + 1, cz);

    if (lz == 0) updateChunkMesh(cx, cy, cz - 1);
    else if (lz == Chunk::kSizeZ - 1) updateChunkMesh(cx, cy, cz + 1);
}

bool ChunkStreamer::isBlockDataReadyAtWorld(int worldX, int worldY, int worldZ) {
    int cx = static_cast<int>(std::floor(static_cast<float>(worldX) / Chunk::kSizeX));
    int cy = static_cast<int>(std::floor(static_cast<float>(worldY) / Chunk::kSizeY));
    int cz = static_cast<int>(std::floor(static_cast<float>(worldZ) / Chunk::kSizeZ));

    ChunkCoord cc{cx, cy, cz};
    std::lock_guard<std::mutex> lock(mapMutex_);
    auto it = chunks_.find(cc);
    if (it == chunks_.end()) return false;
    return it->second->hasVoxelData();
}
