#include "ChunkStreamer.h"
#include "TerrainGenerationSystem.h"
#include "Block.h"
#include <GLFW/glfw3.h>
#include <cassert>
#include <algorithm>
#include <chrono>
#include <array>

namespace {

bool isRenderableBlockForMeshing(std::uint8_t id) {
    return BlockRegistry::get(id).isSolid() || id == 3 || id == 17;
}

bool isFaceOccludedForMeshing(std::uint8_t selfId, std::uint8_t neighborId) {
    if (selfId == 3) {
        return BlockRegistry::get(neighborId).isSolid() || neighborId == 3;
    }
    if (selfId == 17) {
        return neighborId != 0;
    }
    if (selfId == 16) {
        return BlockRegistry::get(neighborId).isSolid() && neighborId != 16;
    }
    if (neighborId == 18 || neighborId == 17) {
        return false;
    }
    return BlockRegistry::get(neighborId).isSolid();
}

bool isFaceVisibleForMeshing(std::uint8_t selfId, std::uint8_t neighborId) {
    return isRenderableBlockForMeshing(selfId) && !isFaceOccludedForMeshing(selfId, neighborId);
}

bool needsConservativeRemesh(std::uint8_t id) {
    // Cactus and tall grass use custom face emission paths; keep updates conservative nearby.
    return id == 17 || id == 18;
}

} // namespace

ChunkStreamer::ChunkStreamer(std::uint64_t seed, int radius, int verticalRadius)
    : seed_(seed), radius_(radius), vRadius_(verticalRadius) {
    running_.store(true);

    // Use half the available CPU threads, but keep at least two workers.
    const unsigned int numWorkers = std::max(2u, std::thread::hardware_concurrency() / 2);
    workerThreads_.reserve(numWorkers);
    // Reserve one dedicated remesh worker so block edits stay responsive
    // even while generation workers are busy on terrain jobs.
    workerThreads_.emplace_back(&ChunkStreamer::workerLoop, this, true);
    for (unsigned int i = 1; i < numWorkers; ++i) {
        workerThreads_.emplace_back(&ChunkStreamer::workerLoop, this, false);
    }
}

ChunkStreamer::~ChunkStreamer() {
    running_.store(false);
    queueCv_.notify_all();
    for (auto& worker : workerThreads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
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
        queueCv_.notify_all();
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
    processDirtyMeshes(kMaxDirtyMeshUpdatesPerFrame);

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
    processDirtyMeshes(kMaxDirtyMeshUpdatesPerFrame);

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

void ChunkStreamer::workerLoop(bool remeshOnly) {
    while (running_.load()) {
        ChunkCoord work;
        bool isRemeshJob = false;
        {
            std::unique_lock<std::mutex> qlock(queueMutex_);
            queueCv_.wait(qlock, [&]() {
                if (!running_.load()) {
                    return true;
                }
                if (remeshOnly) {
                    return !remeshQueue_.empty();
                }
                return !queue_.empty();
            });
            if (!running_.load()) break;

            if (remeshOnly) {
                work = remeshQueue_.front();
                remeshQueue_.pop_front();
                isRemeshJob = true;
            } else {
                work = queue_.front();
                queue_.pop_front();
            }
        }

        if (isRemeshJob) {
            Chunk tempCopy;
            {
                std::lock_guard<std::mutex> lock(mapMutex_);
                auto it = chunks_.find(work);
                if (it == chunks_.end() || !it->second->hasVoxelData()) {
                    std::lock_guard<std::mutex> qlock(queueMutex_);
                    remeshSet_.erase(work);
                    continue;
                }
                tempCopy.copyBlocksFrom(*(it->second));
            }

            std::vector<float> verts = tempCopy.generateMesh();

            {
                std::lock_guard<std::mutex> lock(mapMutex_);
                auto it = chunks_.find(work);
                if (it != chunks_.end() && it->second->hasVoxelData()) {
                    it->second->setPendingMesh(std::move(verts));
                }
            }

            {
                std::lock_guard<std::mutex> qlock(queueMutex_);
                remeshSet_.erase(work);
                if (remeshRetrySet_.erase(work) > 0) {
                    if (remeshSet_.insert(work).second) {
                        remeshQueue_.push_back(work);
                        queueCv_.notify_all();
                    }
                }
            }
            continue;
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

    auto enqueueDirtyIfLoaded = [&](int chunkX, int chunkY, int chunkZ) {
        ChunkCoord cc{chunkX, chunkY, chunkZ};
        auto it = chunks_.find(cc);
        if (it == chunks_.end() || !it->second->hasVoxelData()) {
            return;
        }

        if (dirtySet_.insert(cc).second) {
            dirtyQueue_.push_back(cc);
        }
    };

    auto getLoadedBlockAtWorldUnlocked = [&](int wx, int wy, int wz) -> std::uint8_t {
        int qx = static_cast<int>(std::floor(static_cast<float>(wx) / Chunk::kSizeX));
        int qy = static_cast<int>(std::floor(static_cast<float>(wy) / Chunk::kSizeY));
        int qz = static_cast<int>(std::floor(static_cast<float>(wz) / Chunk::kSizeZ));

        ChunkCoord qcc{qx, qy, qz};
        auto qit = chunks_.find(qcc);
        if (qit == chunks_.end() || !qit->second->hasVoxelData()) {
            return 0;
        }

        int qlx = wx - (qx * Chunk::kSizeX);
        int qly = wy - (qy * Chunk::kSizeY);
        int qlz = wz - (qz * Chunk::kSizeZ);
        return qit->second->getBlock(qlx, qly, qlz);
    };

    std::lock_guard<std::mutex> lock(mapMutex_);

    ChunkCoord cc{cx, cy, cz};
    auto it = chunks_.find(cc);
    if (it == chunks_.end() || !it->second->hasVoxelData()) {
        return;
    }

    const std::uint8_t oldBlockId = it->second->getBlock(lx, ly, lz);
    if (oldBlockId == blockId) {
        return;
    }

    struct NeighborSnapshot {
        int worldX;
        int worldY;
        int worldZ;
        int chunkX;
        int chunkY;
        int chunkZ;
        std::uint8_t id;
    };

    static constexpr std::array<std::array<int, 3>, 6> kNeighbors{{
        {{ 1,  0,  0}},
        {{-1,  0,  0}},
        {{ 0,  1,  0}},
        {{ 0, -1,  0}},
        {{ 0,  0,  1}},
        {{ 0,  0, -1}},
    }};

    std::array<NeighborSnapshot, 6> neighbors{};
    for (std::size_t i = 0; i < kNeighbors.size(); ++i) {
        const int nx = worldX + kNeighbors[i][0];
        const int ny = worldY + kNeighbors[i][1];
        const int nz = worldZ + kNeighbors[i][2];

        neighbors[i].worldX = nx;
        neighbors[i].worldY = ny;
        neighbors[i].worldZ = nz;
        neighbors[i].chunkX = static_cast<int>(std::floor(static_cast<float>(nx) / Chunk::kSizeX));
        neighbors[i].chunkY = static_cast<int>(std::floor(static_cast<float>(ny) / Chunk::kSizeY));
        neighbors[i].chunkZ = static_cast<int>(std::floor(static_cast<float>(nz) / Chunk::kSizeZ));
        neighbors[i].id = getLoadedBlockAtWorldUnlocked(nx, ny, nz);
    }

    it->second->setBlock(lx, ly, lz, blockId);

    bool centerNeedsUpdate = false;
    for (const auto& n : neighbors) {
        if (isFaceVisibleForMeshing(oldBlockId, n.id) != isFaceVisibleForMeshing(blockId, n.id)) {
            centerNeedsUpdate = true;
            break;
        }
    }

    if (needsConservativeRemesh(oldBlockId) || needsConservativeRemesh(blockId)) {
        centerNeedsUpdate = true;
    }

    if (centerNeedsUpdate) {
        enqueueDirtyIfLoaded(cx, cy, cz);
    }

    for (const auto& n : neighbors) {
        bool neighborNeedsUpdate =
            isFaceVisibleForMeshing(n.id, oldBlockId) != isFaceVisibleForMeshing(n.id, blockId);

        if (needsConservativeRemesh(n.id)) {
            neighborNeedsUpdate = true;
        }

        if (neighborNeedsUpdate) {
            enqueueDirtyIfLoaded(n.chunkX, n.chunkY, n.chunkZ);
        }
    }
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

void ChunkStreamer::processDirtyMeshes(std::size_t budget) {
    std::scoped_lock lock(mapMutex_, queueMutex_);

    std::size_t updated = 0;
    while (updated < budget && !dirtyQueue_.empty()) {
        const ChunkCoord cc = dirtyQueue_.front();
        dirtyQueue_.pop_front();
        dirtySet_.erase(cc);

        auto it = chunks_.find(cc);
        if (it != chunks_.end() && it->second->hasVoxelData()) {
            if (remeshSet_.insert(cc).second) {
                remeshQueue_.push_back(cc);
            } else {
                remeshRetrySet_.insert(cc);
            }
        }

        ++updated;
    }

    if (updated > 0 && !remeshQueue_.empty()) {
        queueCv_.notify_all();
    }
}
