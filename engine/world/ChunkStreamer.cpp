#include "ChunkStreamer.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cassert>
#include <algorithm>

// splitmix64 for deterministic pseudo-random from coordinates
static uint64_t splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static double coordNoise(std::uint64_t seed, int64_t x, int64_t y, int64_t z) {
    uint64_t key = static_cast<uint64_t>(x) * 73856093u
                 ^ static_cast<uint64_t>(y) * 19349663u
                 ^ static_cast<uint64_t>(z) * 83492791u
                 ^ seed;
    uint64_t k = key;
    uint64_t v = splitmix64(k);
    return (v >> 11) * (1.0 / 9007199254740992.0); // 53-bit precision
}

static double fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

static double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

static double gradientDot(std::uint64_t seed, int gridX, int gridZ, double dx, double dz) {
    constexpr double kTau = 6.28318530717958647692;
    double angle = coordNoise(seed, gridX, 0, gridZ) * kTau;
    double gx = std::cos(angle);
    double gz = std::sin(angle);
    return (gx * dx) + (gz * dz);
}

static double perlin2D(std::uint64_t seed, double x, double z) {
    int x0 = static_cast<int>(std::floor(x));
    int z0 = static_cast<int>(std::floor(z));
    int x1 = x0 + 1;
    int z1 = z0 + 1;

    double tx = x - static_cast<double>(x0);
    double tz = z - static_cast<double>(z0);

    double n00 = gradientDot(seed, x0, z0, tx, tz);
    double n10 = gradientDot(seed, x1, z0, tx - 1.0, tz);
    double n01 = gradientDot(seed, x0, z1, tx, tz - 1.0);
    double n11 = gradientDot(seed, x1, z1, tx - 1.0, tz - 1.0);

    double u = fade(tx);
    double v = fade(tz);

    double nx0 = lerp(n00, n10, u);
    double nx1 = lerp(n01, n11, u);
    return lerp(nx0, nx1, v);
}

static int sampleTerrainHeight(std::uint64_t seed, int worldX, int worldZ) {
    constexpr double kNoiseScale = 1.0 / 48.0;
    constexpr int kBaseHeight = 12;
    constexpr int kHeightAmplitude = 10;

    double noise = perlin2D(seed, worldX * kNoiseScale, worldZ * kNoiseScale);
    double normalized = (noise + 0.7) / 1.4;
    normalized = std::clamp(normalized, 0.0, 1.0);

    return kBaseHeight + static_cast<int>(std::round(normalized * kHeightAmplitude));
}

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
    std::lock_guard<std::mutex> lock(mapMutex_);
    auto it = chunks_.find(c);
    if (it == chunks_.end()) {
        // create empty slot so worker can find it
        chunks_.emplace(c, std::make_unique<Chunk>());
        // push request
        {
            std::lock_guard<std::mutex> qlock(queueMutex_);
            queue_.push_back(c);
        }
        queueCv_.notify_one();
    }
}

void ChunkStreamer::tick(float playerX, float playerY, float playerZ) {
    // compute player chunk coords
    int pcx = static_cast<int>(std::floor(playerX / Chunk::kSizeX));
    int pcy = static_cast<int>(std::floor(playerY / Chunk::kSizeY));
    int pcz = static_cast<int>(std::floor(playerZ / Chunk::kSizeZ));

    // Load new chunks within radius
    for (int dy = -vRadius_; dy <= vRadius_; ++dy) {
        for (int dz = -radius_; dz <= radius_; ++dz) {
            for (int dx = -radius_; dx <= radius_; ++dx) {
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
        if (distX > radius_ + 1 || distY > vRadius_ + 1 || distZ > radius_ + 1) {
            it = chunks_.erase(it); // Erase frees the memory and returns the next iterator
        } else {
            ++it;
        }
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

void ChunkStreamer::renderAll(const float baseMVP[16], unsigned int shaderProgram) {
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

        c->render();
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

        // generate temporary chunk data
        Chunk temp;
        generateBlocksForChunk(temp, work);
        std::vector<float> verts = temp.generateMesh();

        // deliver vertices to the main-thread-owned chunk if still present
        std::lock_guard<std::mutex> lock(mapMutex_);
        auto it = chunks_.find(work);
        if (it != chunks_.end()) {
            it->second->copyBlocksFrom(temp);
            it->second->setPendingMesh(std::move(verts));
        }
    }
}

void ChunkStreamer::generateBlocksForChunk(Chunk& chunk, const ChunkCoord& c) {
    for (int y = 0; y < Chunk::kSizeY; ++y) {
        for (int z = 0; z < Chunk::kSizeZ; ++z) {
            for (int x = 0; x < Chunk::kSizeX; ++x) {
                const int worldX = (c.cx * Chunk::kSizeX) + x;
                const int worldY = (c.cy * Chunk::kSizeY) + y;
                const int worldZ = (c.cz * Chunk::kSizeZ) + z;
                const int terrainHeight = sampleTerrainHeight(seed_, worldX, worldZ);

                if (worldY <= terrainHeight) {
                    chunk.setBlock(x, y, z, 1);
                } else {
                    chunk.setBlock(x, y, z, 0);
                }
            }
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

    // Convert World Pos to Local Chunk Pos (0 to kSize-1)
    int lx = worldX - (cx * Chunk::kSizeX);
    int ly = worldY - (cy * Chunk::kSizeY);
    int lz = worldZ - (cz * Chunk::kSizeZ);

    return it->second->getBlock(lx, ly, lz);
}
