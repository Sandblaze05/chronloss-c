#include "TerrainGenerationSystem.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "Chunk.h"

namespace {

using TerrainGenerationSystem::BiomeType;

// splitmix64 for deterministic pseudo-random from coordinates
std::uint64_t splitmix64(std::uint64_t& x) {
    std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

double coordNoise(std::uint64_t seed, std::int64_t x, std::int64_t y, std::int64_t z) {
    std::uint64_t key = static_cast<std::uint64_t>(x) * 73856093u
                      ^ static_cast<std::uint64_t>(y) * 19349663u
                      ^ static_cast<std::uint64_t>(z) * 83492791u
                      ^ seed;
    std::uint64_t k = key;
    std::uint64_t v = splitmix64(k);
    return (v >> 11) * (1.0 / 9007199254740992.0); // 53-bit precision
}

double fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

double gradientDot(std::uint64_t seed, int gridX, int gridZ, double dx, double dz) {
    constexpr double kTau = 6.28318530717958647692;
    double angle = coordNoise(seed, gridX, 0, gridZ) * kTau;
    double gx = std::cos(angle);
    double gz = std::sin(angle);
    return (gx * dx) + (gz * dz);
}

double perlin2D(std::uint64_t seed, double x, double z) {
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

double perlin3D(std::uint64_t seed, double x, double y, double z) {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int z0 = static_cast<int>(std::floor(z));
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    int z1 = z0 + 1;

    double tx = x - static_cast<double>(x0);
    double ty = y - static_cast<double>(y0);
    double tz = z - static_cast<double>(z0);

    auto grad = [&](int gx, int gy, int gz, double dx, double dy, double dz) {
        constexpr double kTau = 6.28318530717958647692;
        double angleA = coordNoise(seed, gx, gy, gz) * kTau;
        double angleB = coordNoise(seed + 0x9e3779b97f4a7c15ULL, gx, gy, gz) * kTau;

        double ux = std::cos(angleA) * std::sin(angleB);
        double uy = std::cos(angleB);
        double uz = std::sin(angleA) * std::sin(angleB);
        return (ux * dx) + (uy * dy) + (uz * dz);
    };

    double n000 = grad(x0, y0, z0, tx, ty, tz);
    double n100 = grad(x1, y0, z0, tx - 1.0, ty, tz);
    double n010 = grad(x0, y1, z0, tx, ty - 1.0, tz);
    double n110 = grad(x1, y1, z0, tx - 1.0, ty - 1.0, tz);
    double n001 = grad(x0, y0, z1, tx, ty, tz - 1.0);
    double n101 = grad(x1, y0, z1, tx - 1.0, ty, tz - 1.0);
    double n011 = grad(x0, y1, z1, tx, ty - 1.0, tz - 1.0);
    double n111 = grad(x1, y1, z1, tx - 1.0, ty - 1.0, tz - 1.0);

    double u = fade(tx);
    double v = fade(ty);
    double w = fade(tz);

    double nx00 = lerp(n000, n100, u);
    double nx10 = lerp(n010, n110, u);
    double nx01 = lerp(n001, n101, u);
    double nx11 = lerp(n011, n111, u);

    double nxy0 = lerp(nx00, nx10, v);
    double nxy1 = lerp(nx01, nx11, v);
    return lerp(nxy0, nxy1, w);
}

double fbm2D(std::uint64_t seed,
             double x,
             double z,
             int octaves,
             double lacunarity,
             double persistence) {
    double frequency = 1.0;
    double amplitude = 1.0;
    double sum = 0.0;
    double ampSum = 0.0;

    for (int i = 0; i < octaves; ++i) {
        sum += perlin2D(seed + static_cast<std::uint64_t>(i) * 1315423911ULL,
                        x * frequency,
                        z * frequency) * amplitude;
        ampSum += amplitude;
        frequency *= lacunarity;
        amplitude *= persistence;
    }

    if (ampSum <= 0.0) {
        return 0.0;
    }
    return sum / ampSum;
}

double ridgedFbm2D(std::uint64_t seed,
                   double x,
                   double z,
                   int octaves,
                   double lacunarity,
                   double gain) {
    double frequency = 1.0;
    double amplitude = 0.5;
    double sum = 0.0;
    double weight = 1.0;

    for (int i = 0; i < octaves; ++i) {
        double n = perlin2D(seed + static_cast<std::uint64_t>(i) * 1315423911ULL,
                            x * frequency,
                            z * frequency);
        n = 1.0 - std::abs(n);
        n = n * n * weight;
        weight = std::clamp(n * gain, 0.0, 1.0);
        sum += n * amplitude;
        frequency *= lacunarity;
        amplitude *= 0.5;
    }

    return sum;
}

double fbm3D(std::uint64_t seed,
             double x,
             double y,
             double z,
             int octaves,
             double lacunarity,
             double persistence) {
    double frequency = 1.0;
    double amplitude = 1.0;
    double sum = 0.0;
    double ampSum = 0.0;

    for (int i = 0; i < octaves; ++i) {
        sum += perlin3D(seed + static_cast<std::uint64_t>(i) * 1103515245ULL,
                        x * frequency,
                        y * frequency,
                        z * frequency) * amplitude;
        ampSum += amplitude;
        frequency *= lacunarity;
        amplitude *= persistence;
    }

    if (ampSum <= 0.0) {
        return 0.0;
    }
    return sum / ampSum;
}

double toUnit(double n) {
    return std::clamp((n * 0.5) + 0.5, 0.0, 1.0);
}

struct BiomeSample {
    BiomeType type = BiomeType::Plains;
    double temperature = 0.5;
    double moisture = 0.5;
};

struct TerrainProfile {
    std::uint8_t topBlock = 5;
    std::uint8_t fillerBlock = 2;
    std::uint8_t deepBlock = 1;
    int fillerDepth = 4;
    int baseHeight = 56;
    int heightAmplitude = 20;
};

TerrainProfile toTerrainProfile(const TerrainGenerationSystem::BiomeProfileConfig& cfg) {
    TerrainProfile p{};
    p.topBlock = cfg.topBlock;
    p.fillerBlock = cfg.fillerBlock;
    p.deepBlock = cfg.deepBlock;
    p.fillerDepth = cfg.fillerDepth;
    p.baseHeight = cfg.baseHeight;
    p.heightAmplitude = cfg.heightAmplitude;
    return p;
}

TerrainGenerationSystem::TerrainGenConfig makeDefaultConfig() {
    TerrainGenerationSystem::TerrainGenConfig cfg{};

    cfg.mountainTemperatureThreshold = 0.36;
    cfg.mountainMoistureThreshold = 0.40;

    cfg.desert = {6, 7, 1, 8, 56, 20};
    cfg.plains = {5, 2, 1, 4, 58, 26};
    cfg.forest = {5, 2, 1, 5, 60, 28};
    cfg.mountains = {10, 8, 1, 6, 72, 130};
    cfg.tundra = {10, 8, 1, 4, 58, 32};
    cfg.swamp = {9, 2, 1, 5, 52, 8};

    cfg.terrainWarpScale = 1.0 / 170.0;
    cfg.terrainWarpStrength = 64.0;
    cfg.macroScale = 1.0 / 700.0;
    cfg.mountainScale = 1.0 / 220.0;
    cfg.detailScale = 1.0 / 92.0;
    cfg.macroWeight = 0.52;
    cfg.ridgeWeight = 1.75;
    cfg.detailWeight = 0.28;
    cfg.nonMountainRidgeBias = 0.20;
    cfg.blendRadius = 12;
    cfg.blendWeights = {{0.70, 0.075, 0.075, 0.075, 0.075}};

    cfg.snowStartY = 158;
    cfg.mountainStoneStartY = 130;
    cfg.overhangBandHalfWidth = 36.0;
    cfg.overhangScale = 1.0 / 48.0;
    cfg.overhangStrength = 23.0;

    cfg.ores = {{
        {8, 1.0 / 6.0, 0.64, 80, -100, true},
        {11, 1.0 / 14.0, 0.62, 32, -100, true},
        {12, 1.0 / 10.0, 0.66, 16, -100, true},
        {13, 1.0 / 8.0, 0.70, 0, -100, true},
        {14, 1.0 / 7.0, 0.74, -20, -100, true},
    }};

    return cfg;
}

std::mutex gConfigMutex;
TerrainGenerationSystem::TerrainGenConfig gConfig = makeDefaultConfig();

BiomeSample sampleBiome(std::uint64_t seed,
                        int worldX,
                        int worldZ,
                        const TerrainGenerationSystem::TerrainGenConfig& cfg) {
    const double tx = static_cast<double>(worldX) * cfg.tempScale;
    const double tz = static_cast<double>(worldZ) * cfg.tempScale;
    const double mx = static_cast<double>(worldX) * cfg.moistureScale;
    const double mz = static_cast<double>(worldZ) * cfg.moistureScale;

    double temperature = toUnit(fbm2D(seed + 0xA2F13C5ULL, tx, tz, 4, 2.0, 0.5));
    double moisture = toUnit(fbm2D(seed + 0x79E4D2BULL, mx, mz, 4, 2.0, 0.5));

    const double wx = static_cast<double>(worldX);
    const double wz = static_cast<double>(worldZ);
    const double mountainMaskScale = cfg.macroScale * 1.35;
    const double mountainMaskRaw = toUnit(fbm2D(seed + 0x7C159E37ULL,
                                                wx * mountainMaskScale,
                                                wz * mountainMaskScale,
                                                3,
                                                2.0,
                                                0.5));
    const double mountainMask = std::pow(std::clamp((mountainMaskRaw - 0.34) / 0.66, 0.0, 1.0), 1.4);
    const double ridged = std::clamp(ridgedFbm2D(seed + 0xF00D1234ULL,
                                                 wx * cfg.mountainScale,
                                                 wz * cfg.mountainScale,
                                                 5,
                                                 2.1,
                                                 2.0),
                                     0.0,
                                     1.0);
    const double mountainSignal = (mountainMask * 0.6) + (std::pow(ridged, 1.7) * 0.4);

    BiomeType biome = BiomeType::Plains;
    if (temperature > cfg.desertTemperatureThreshold && moisture < cfg.desertMoistureThreshold) {
        biome = BiomeType::Desert;
    } else if (temperature < cfg.mountainTemperatureThreshold) {
        if (mountainSignal > 0.52) {
            biome = (moisture < cfg.mountainMoistureThreshold) ? BiomeType::Tundra : BiomeType::Mountains;
        } else if (moisture > cfg.forestMoistureThreshold) {
            biome = BiomeType::Forest;
        }
    } else if (moisture > cfg.swampMoistureThreshold) {
        biome = BiomeType::Swamp;
    } else if (moisture > cfg.forestMoistureThreshold) {
        biome = BiomeType::Forest;
    }

    return BiomeSample{biome, temperature, moisture};
}

TerrainProfile profileForBiome(BiomeType biome, const TerrainGenerationSystem::TerrainGenConfig& cfg) {
    switch (biome) {
        case BiomeType::Desert:
            return toTerrainProfile(cfg.desert);
        case BiomeType::Forest:
            return toTerrainProfile(cfg.forest);
        case BiomeType::Mountains:
            return toTerrainProfile(cfg.mountains);
        case BiomeType::Tundra:
            return toTerrainProfile(cfg.tundra);
        case BiomeType::Swamp:
            return toTerrainProfile(cfg.swamp);
        case BiomeType::Plains:
        default:
            return toTerrainProfile(cfg.plains);
    }
}

void domainWarp2D(std::uint64_t seed,
                  double& x,
                  double& z,
                  const TerrainGenerationSystem::TerrainGenConfig& cfg) {
    double dx = fbm2D(seed + 0x1234567ULL, x * cfg.terrainWarpScale, z * cfg.terrainWarpScale, 3, 2.0, 0.5);
    double dz = fbm2D(seed + 0x89ABCDEFULL, x * cfg.terrainWarpScale, z * cfg.terrainWarpScale, 3, 2.0, 0.5);

    x += dx * cfg.terrainWarpStrength;
    z += dz * cfg.terrainWarpStrength;
}

std::uint8_t sampleSurfaceBlock(const TerrainProfile& profile,
                                const BiomeSample& biome,
                                int terrainHeight,
                                const TerrainGenerationSystem::TerrainGenConfig& cfg) {
    if (terrainHeight >= cfg.snowStartY) {
        return 10;
    }
    if (biome.type == BiomeType::Mountains && terrainHeight >= cfg.mountainStoneStartY) {
        return 1;
    }
    return profile.topBlock;
}

int sampleTerrainHeight(std::uint64_t seed,
                        int worldX,
                        int worldZ,
                        const TerrainProfile& profile,
                        const BiomeSample& biome,
                        const TerrainGenerationSystem::TerrainGenConfig& cfg) {
    double wx = static_cast<double>(worldX);
    double wz = static_cast<double>(worldZ);

    domainWarp2D(seed, wx, wz, cfg);

    const double macro = fbm2D(seed + 0xDEADBEEFULL,
                               wx * cfg.macroScale,
                               wz * cfg.macroScale,
                               4,
                               2.0,
                               0.5);
    const double ridged = std::clamp(ridgedFbm2D(seed + 0xF00D1234ULL,
                                                 wx * cfg.mountainScale,
                                                 wz * cfg.mountainScale,
                                                 5,
                                                 2.1,
                                                 2.0),
                                     0.0,
                                     1.0);
    const double detail = fbm2D(seed + 0xC0FFEE11ULL,
                                wx * cfg.detailScale,
                                wz * cfg.detailScale,
                                5,
                                2.0,
                                0.54);

    const double mountainMaskScale = cfg.macroScale * 1.35;
    const double mountainMaskRaw = toUnit(fbm2D(seed + 0x7C159E37ULL,
                                                wx * mountainMaskScale,
                                                wz * mountainMaskScale,
                                                3,
                                                2.0,
                                                0.5));
    const double mountainMask = std::pow(std::clamp((mountainMaskRaw - 0.34) / 0.66, 0.0, 1.0), 1.4);

    // Shape ridges into sparse peak clusters so mountains form separated summits,
    // not one continuous elevated wall.
    const double ridgeSharp = std::pow(ridged, (biome.type == BiomeType::Mountains) ? 2.8 : 2.2);
    const double peakMaskRaw = toUnit(fbm2D(seed + 0x7F4A7C15ULL,
                                            wx * (cfg.macroScale * 1.35),
                                            wz * (cfg.macroScale * 1.35),
                                            3,
                                            2.0,
                                            0.5));
    const double peakMask = std::pow(std::clamp((peakMaskRaw - 0.40) / 0.60, 0.0, 1.0), 1.8);

    const double cliffNoise = std::clamp(ridgedFbm2D(seed + 0xC11F0FF0ULL,
                                                     wx * (cfg.mountainScale * 2.7),
                                                     wz * (cfg.mountainScale * 2.7),
                                                     4,
                                                     2.2,
                                                     2.0),
                                         0.0,
                                         1.0);
    const double cliffMask = std::clamp(mountainMask * 0.7 + std::max(0.0, ridgeSharp - 0.52) * 1.05, 0.0, 1.0);
    const double cliffContribution = std::pow(cliffNoise, 2.0) * cliffMask * 0.18;

    const double crackNoise = fbm2D(seed + 0x1234ABCDULL,
                                    wx * (cfg.mountainScale * 3.4),
                                    wz * (cfg.mountainScale * 3.4),
                                    3,
                                    2.0,
                                    0.5);
    const double crackSignal = std::clamp((std::abs(crackNoise) - 0.58) / 0.42, 0.0, 1.0);
    const double crackContribution = crackSignal * cliffMask * 0.12;

    const double ridgeContribution = ridgeSharp * peakMask * mountainMask;

    const double biomeMountainBias = (biome.type == BiomeType::Mountains) ? 1.0 : cfg.nonMountainRidgeBias;
    const double combined = (macro * cfg.macroWeight) +
                            (((ridgeContribution - 0.20) * cfg.ridgeWeight) * biomeMountainBias) +
                            (detail * cfg.detailWeight) +
                            (cliffContribution * 0.35) -
                            (crackContribution * 0.30);
    const double normalized = toUnit(combined);

    int height = profile.baseHeight +
                 static_cast<int>(std::round(normalized * static_cast<double>(profile.heightAmplitude)));

    if (biome.type == BiomeType::Swamp && static_cast<double>(height) > cfg.swampHeightCap) {
        height = static_cast<int>(std::round(cfg.swampHeightCap));
    }
    return height;
}

int sampleBlendedTerrainHeight(std::uint64_t seed,
                               int worldX,
                               int worldZ,
                               const TerrainGenerationSystem::TerrainGenConfig& cfg) {
    constexpr int kSamples = 5;
    const int offsets[kSamples][2] = {{0, 0}, {cfg.blendRadius, 0}, {-cfg.blendRadius, 0}, {0, cfg.blendRadius}, {0, -cfg.blendRadius}};

    double totalHeight = 0.0;
    double totalWeight = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        const int sx = worldX + offsets[i][0];
        const int sz = worldZ + offsets[i][1];
        const double weight = std::max(cfg.blendWeights[static_cast<std::size_t>(i)], 0.0);
        const BiomeSample biome = sampleBiome(seed, sx, sz, cfg);
        const TerrainProfile profile = profileForBiome(biome.type, cfg);
        const int h = sampleTerrainHeight(seed, sx, sz, profile, biome, cfg);
        totalHeight += static_cast<double>(h) * weight;
        totalWeight += weight;
    }

    if (totalWeight <= 0.0) {
        return 0;
    }
    return static_cast<int>(std::round(totalHeight / totalWeight));
}

bool shouldCarveCave(std::uint64_t seed,
                     int worldX,
                     int worldY,
                     int worldZ,
                     int terrainHeight,
                     const TerrainGenerationSystem::TerrainGenConfig& cfg) {
    if (worldY >= terrainHeight - cfg.caveSurfaceGuard) {
        return false;
    }

    const double x = static_cast<double>(worldX);
    const double y = static_cast<double>(worldY);
    const double z = static_cast<double>(worldZ);

    const double warp = fbm3D(seed + 0xBB67AE85ULL,
                              x * cfg.caveWarpScale,
                              y * cfg.caveWarpScale,
                              z * cfg.caveWarpScale,
                              3,
                              2.0,
                              0.55);

    const double wx2 = x + warp * cfg.caveWarpStrength;
    const double wz2 = z - warp * cfg.caveWarpStrength;
    const double squashedY = y * cfg.caveYSquash;

    const double n1 = fbm3D(seed + 0x3C6EF372ULL,
                            wx2 * cfg.caveScale,
                            squashedY * cfg.caveScale,
                            wz2 * cfg.caveScale,
                            4,
                            2.0,
                            0.5);
    const double n2 = fbm3D(seed + 0xA54FF53AULL,
                            wx2 * cfg.caveScale + 1.7,
                            squashedY * cfg.caveScale + 3.1,
                            wz2 * cfg.caveScale - 2.4,
                            4,
                            2.0,
                            0.5);

    const double depthFactor = std::clamp((static_cast<double>(terrainHeight - worldY) - cfg.caveDepthOffset) / cfg.caveDepthRange,
                                          0.0,
                                          1.0);
    const double threshold = cfg.caveThresholdBase + (cfg.caveThresholdDepthGain * depthFactor);

    return (std::abs(n1) < threshold && std::abs(n2) < threshold);
}

bool isAquiferCell(std::uint64_t seed,
                   int worldX,
                   int worldY,
                   int worldZ,
                   const TerrainGenerationSystem::TerrainGenConfig& cfg) {
    const double x = static_cast<double>(worldX);
    const double y = static_cast<double>(worldY);
    const double z = static_cast<double>(worldZ);

    const double density = fbm3D(seed + 0xA54FF53AULL,
                                 x * cfg.aquiferScale,
                                 y * (cfg.aquiferScale * cfg.aquiferYScaleMul),
                                 z * cfg.aquiferScale,
                                 3,
                                 2.0,
                                 0.5);
    return density > cfg.aquiferThreshold;
}

} // namespace

namespace TerrainGenerationSystem {

TerrainGenConfig getConfig() {
    std::lock_guard<std::mutex> lock(gConfigMutex);
    return gConfig;
}

void setConfig(const TerrainGenConfig& config) {
    std::lock_guard<std::mutex> lock(gConfigMutex);
    gConfig = config;
}

void resetConfig() {
    std::lock_guard<std::mutex> lock(gConfigMutex);
    gConfig = makeDefaultConfig();
}

void generateChunkTerrain(std::uint64_t seed,
                          Chunk& chunk,
                          int chunkX,
                          int chunkY,
                          int chunkZ) {
    const TerrainGenConfig cfg = getConfig();

    struct ColumnInfo {
        int terrainHeight = 0;
        BiomeSample biome{};
        TerrainProfile profile{};
    };

    ColumnInfo columns[Chunk::kSizeX][Chunk::kSizeZ];
    for (int z = 0; z < Chunk::kSizeZ; ++z) {
        for (int x = 0; x < Chunk::kSizeX; ++x) {
            const int worldX = (chunkX * Chunk::kSizeX) + x;
            const int worldZ = (chunkZ * Chunk::kSizeZ) + z;
            const BiomeSample biome = sampleBiome(seed, worldX, worldZ, cfg);
            const TerrainProfile profile = profileForBiome(biome.type, cfg);
            const int terrainHeight = sampleBlendedTerrainHeight(seed, worldX, worldZ, cfg);
            columns[x][z] = ColumnInfo{terrainHeight, biome, profile};
        }
    }

    for (int y = 0; y < Chunk::kSizeY; ++y) {
        for (int z = 0; z < Chunk::kSizeZ; ++z) {
            for (int x = 0; x < Chunk::kSizeX; ++x) {
                const int worldX = (chunkX * Chunk::kSizeX) + x;
                const int worldY = (chunkY * Chunk::kSizeY) + y;
                const int worldZ = (chunkZ * Chunk::kSizeZ) + z;

                const ColumnInfo& column = columns[x][z];
                const int terrainHeight = column.terrainHeight;
                const BiomeSample& biome = column.biome;
                const TerrainProfile& profile = column.profile;

                std::uint8_t blockId = 0; // Air

                if (worldY == cfg.bedrockY) {
                    blockId = 4; // Bedrock single layer
                } else {
                    double heightDifference = static_cast<double>(terrainHeight - worldY);
                    double density = heightDifference;

                    auto sampleDensityAt = [&](int sampleY) {
                        double sampleHeightDiff = static_cast<double>(terrainHeight - sampleY);
                        double sampleDensity = sampleHeightDiff;
                        if (std::abs(sampleHeightDiff) < cfg.overhangBandHalfWidth) {
                            double overhangNoise = fbm3D(seed + 0x98765432ULL,
                                                         worldX * cfg.overhangScale,
                                                         sampleY * cfg.overhangScale,
                                                         worldZ * cfg.overhangScale,
                                                         3,
                                                         2.0,
                                                         0.5);
                            sampleDensity += overhangNoise * cfg.overhangStrength;
                        }
                        return sampleDensity;
                    };

                    if (std::abs(heightDifference) < cfg.overhangBandHalfWidth) {
                        double overhangNoise = fbm3D(seed + 0x98765432ULL,
                                                     worldX * cfg.overhangScale,
                                                     worldY * cfg.overhangScale,
                                                     worldZ * cfg.overhangScale,
                                                     3,
                                                     2.0,
                                                     0.5);

                        density += overhangNoise * cfg.overhangStrength;
                    }

                    if (density >= 0.0) {
                        if (worldY <= cfg.deepStoneCutoffY) {
                            blockId = profile.deepBlock;
                        } else if (worldY >= terrainHeight && density < cfg.topSurfaceDensityThreshold) {
                            blockId = sampleSurfaceBlock(profile, biome, terrainHeight, cfg);
                        } else if (worldY >= terrainHeight - profile.fillerDepth) {
                            blockId = profile.fillerBlock;
                        } else {
                            blockId = profile.deepBlock;
                        }

                        if (blockId == profile.deepBlock && blockId != 0) {
                            for (const auto& ore : cfg.ores) {
                                if (!ore.enabled) {
                                    continue;
                                }
                                if (worldY > ore.maxY || worldY < ore.minY) {
                                    continue;
                                }
                                double vein = fbm3D(seed ^ static_cast<std::uint64_t>(ore.id) * 0xDEAD1337ULL,
                                                    worldX * ore.scale,
                                                    worldY * ore.scale,
                                                    worldZ * ore.scale,
                                                    2,
                                                    2.0,
                                                    0.5);
                                if (toUnit(vein) > ore.threshold) {
                                    blockId = ore.id;
                                    break;
                                }
                            }
                        }

                        // Beach transition: sand near water level on exposed surface.
                        if (blockId == profile.topBlock && blockId != 0) {
                            const int distToWater = cfg.waterLevelY - worldY;
                            if (distToWater >= 0 && distToWater <= 3) {
                                blockId = 6;
                            }
                        }

                        // Seafloor: shallow sand, mid-depth gravel, deep clay.
                        if (blockId == profile.topBlock || blockId == profile.fillerBlock) {
                            if (terrainHeight < cfg.waterLevelY && worldY == terrainHeight) {
                                const int waterDepth = cfg.waterLevelY - worldY;
                                if (waterDepth <= 3) {
                                    blockId = 6;
                                } else if (waterDepth <= 8) {
                                    blockId = 8;
                                } else {
                                    blockId = 9;
                                }
                            }
                        }

                        if (blockId != 4 && shouldCarveCave(seed, worldX, worldY, worldZ, terrainHeight, cfg)) {
                            blockId = 0;
                            if (worldY <= cfg.waterLevelY - cfg.aquiferDepthYOffset &&
                                isAquiferCell(seed, worldX, worldY, worldZ, cfg)) {
                                blockId = 3;
                            }
                        }

                        // Surface material must stay exposed: if a top block would be buried or submerged,
                        // demote it to filler so layering becomes grass -> dirt -> stone.
                        if (blockId == profile.topBlock && blockId != 0) {
                            const int aboveY = worldY + 1;
                            bool aboveIsSolid = false;
                            if (aboveY != cfg.bedrockY) {
                                const double aboveDensity = sampleDensityAt(aboveY);
                                if (aboveDensity >= 0.0 &&
                                    !shouldCarveCave(seed, worldX, aboveY, worldZ, terrainHeight, cfg)) {
                                    aboveIsSolid = true;
                                }
                            }

                            const bool aboveIsWater = (aboveY <= cfg.waterLevelY) && !aboveIsSolid;
                            if (aboveIsSolid || aboveIsWater) {
                                blockId = profile.fillerBlock;
                            }
                        }
                    } else if (worldY <= cfg.waterLevelY) {
                        blockId = 3; // water fill
                    }
                }

                // Final safety override to enforce exact bedrock layer contract.
                if (worldY == cfg.bedrockY) {
                    blockId = 4;
                }

                chunk.setBlock(x, y, z, blockId);
            }
        }
    }
}

TerrainDebugSample sampleDebugAt(std::uint64_t seed, int worldX, int worldZ) {
    const TerrainGenConfig cfg = getConfig();
    const BiomeSample biome = sampleBiome(seed, worldX, worldZ, cfg);
    const TerrainProfile profile = profileForBiome(biome.type, cfg);
    const int terrainHeight = sampleBlendedTerrainHeight(seed, worldX, worldZ, cfg);

    TerrainDebugSample sample{};
    sample.biome = biome.type;
    sample.temperature = biome.temperature;
    sample.moisture = biome.moisture;
    sample.terrainHeight = terrainHeight;
    sample.surfaceBlock = sampleSurfaceBlock(profile, biome, terrainHeight, cfg);
    return sample;
}

const char* biomeTypeToString(BiomeType biome) {
    switch (biome) {
        case BiomeType::Desert:
            return "Desert";
        case BiomeType::Forest:
            return "Forest";
        case BiomeType::Mountains:
            return "Mountains";
        case BiomeType::Tundra:
            return "Tundra";
        case BiomeType::Swamp:
            return "Swamp";
        case BiomeType::Plains:
        default:
            return "Plains";
    }
}

} // namespace TerrainGenerationSystem
