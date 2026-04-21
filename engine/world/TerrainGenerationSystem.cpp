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

// Contrast-enhance a [0,1] value: power < 1 pushes values away from 0.5 (more biome variety).
double sharpen(double x, double power) {
    double centered = x - 0.5;
    double sign = (centered >= 0.0) ? 1.0 : -1.0;
    double magnitude = std::abs(centered) * 2.0;
    return 0.5 + sign * std::pow(magnitude, power) * 0.5;
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

    // Continentalness
    cfg.continentalnessScale = 1.0 / 1800.0;
    cfg.oceanThreshold = 0.40;
    cfg.beachThreshold = 0.45;

    // Widened biome thresholds for actual variety
    cfg.desertTemperatureThreshold = 0.58;
    cfg.desertMoistureThreshold = 0.38;
    cfg.mountainTemperatureThreshold = 0.42;
    cfg.mountainMoistureThreshold = 0.45;
    cfg.swampMoistureThreshold = 0.62;
    cfg.forestMoistureThreshold = 0.45;
    cfg.biomeNoiseContrast = 0.55;

    // Per-biome profiles: {topBlock, fillerBlock, deepBlock, fillerDepth, baseHeight, heightAmplitude}
    cfg.ocean     = {6,  6, 1, 4, 38, 16};   // Sand floor, deep below water
    cfg.beach     = {6,  6, 1, 6, 62,  4};   // Flat sand at water level
    cfg.desert    = {6,  7, 1, 8, 68, 18};   // Sand/sandstone dunes
    cfg.savanna   = {5,  2, 1, 5, 70, 14};   // Grass, moderate height
    cfg.plains    = {5,  2, 1, 5, 66, 10};   // Gentle rolling hills
    cfg.forest    = {5,  2, 1, 6, 68, 18};   // Moderate hills
    cfg.swamp     = {9,  2, 1, 5, 62,  5};   // Very flat, near water level
    cfg.taiga     = {5,  2, 1, 5, 68, 22};   // Rugged terrain
    cfg.tundra    = {10, 8, 1, 4, 66, 18};   // Snow-covered moderate terrain
    cfg.mountains = {10, 8, 1, 6, 72, 120};  // Dramatic peaks

    // Terrain shape
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

    // Surface and layering
    cfg.snowStartY = 158;
    cfg.mountainStoneStartY = 130;
    cfg.overhangBandHalfWidth = 18.0;
    cfg.overhangScale = 1.0 / 55.0;
    cfg.overhangStrength = 10.0;

    // Ores
    cfg.ores = {{
        {8,  1.0 / 6.0,  0.64, 80,  -100, true},
        {11, 1.0 / 14.0, 0.62, 32,  -100, true},
        {12, 1.0 / 10.0, 0.66, 16,  -100, true},
        {13, 1.0 / 8.0,  0.70, 0,   -100, true},
        {14, 1.0 / 7.0,  0.74, -20, -100, true},
    }};

    // Vegetation
    cfg.vegetation.treeDensityForest  = 0.022;
    cfg.vegetation.treeDensityTaiga   = 0.028;
    cfg.vegetation.treeDensityPlains  = 0.003;
    cfg.vegetation.treeDensitySavanna = 0.005;
    cfg.vegetation.treeDensitySwamp   = 0.012;
    cfg.vegetation.cactusDensityDesert = 0.006;

    return cfg;
}

std::mutex gConfigMutex;
TerrainGenerationSystem::TerrainGenConfig gConfig = makeDefaultConfig();

// ─── Biome Selection ────────────────────────────────────────────────

BiomeSample sampleBiome(std::uint64_t seed,
                        int worldX,
                        int worldZ,
                        const TerrainGenerationSystem::TerrainGenConfig& cfg) {
    const double wx = static_cast<double>(worldX);
    const double wz = static_cast<double>(worldZ);

    // Continentalness noise — large-scale land/ocean separation
    const double contRaw = toUnit(fbm2D(seed + 0xC047100DULL,
                                        wx * cfg.continentalnessScale,
                                        wz * cfg.continentalnessScale,
                                        3, 2.0, 0.5));

    if (contRaw < cfg.oceanThreshold) {
        return BiomeSample{BiomeType::Ocean, 0.5, 0.5};
    }
    if (contRaw < cfg.beachThreshold) {
        return BiomeSample{BiomeType::Beach, 0.5, 0.5};
    }

    // Temperature and moisture
    const double tx = wx * cfg.tempScale;
    const double tz = wz * cfg.tempScale;
    const double mx = wx * cfg.moistureScale;
    const double mz = wz * cfg.moistureScale;

    double temperature = toUnit(fbm2D(seed + 0xA2F13C5ULL, tx, tz, 4, 2.0, 0.5));
    double moisture    = toUnit(fbm2D(seed + 0x79E4D2BULL, mx, mz, 4, 2.0, 0.5));

    // Apply contrast enhancement to spread values away from 0.5
    temperature = sharpen(temperature, cfg.biomeNoiseContrast);
    moisture    = sharpen(moisture,    cfg.biomeNoiseContrast);

    // Mountain signal (shape-driven, separate from temp/moisture)
    const double mountainMaskScale = cfg.macroScale * 1.35;
    const double mountainMaskRaw = toUnit(fbm2D(seed + 0x7C159E37ULL,
                                                wx * mountainMaskScale,
                                                wz * mountainMaskScale,
                                                3, 2.0, 0.5));
    const double mountainMask = std::pow(std::clamp((mountainMaskRaw - 0.34) / 0.66, 0.0, 1.0), 1.4);
    const double ridged = std::clamp(ridgedFbm2D(seed + 0xF00D1234ULL,
                                                 wx * cfg.mountainScale,
                                                 wz * cfg.mountainScale,
                                                 5, 2.1, 2.0),
                                     0.0, 1.0);
    const double mountainSignal = (mountainMask * 0.6) + (std::pow(ridged, 1.7) * 0.4);

    // Biome selection
    BiomeType biome = BiomeType::Plains;

    if (mountainSignal > 0.52) {
        biome = BiomeType::Mountains;
    } else if (temperature > cfg.desertTemperatureThreshold) {
        // Hot biomes
        if (moisture < cfg.desertMoistureThreshold) {
            biome = BiomeType::Desert;
        } else if (moisture > cfg.swampMoistureThreshold) {
            biome = BiomeType::Swamp;
        } else {
            biome = BiomeType::Savanna;
        }
    } else if (temperature < cfg.mountainTemperatureThreshold) {
        // Cold biomes
        if (moisture < cfg.desertMoistureThreshold) {
            biome = BiomeType::Tundra;
        } else {
            biome = BiomeType::Taiga;
        }
    } else {
        // Temperate biomes
        if (moisture > cfg.swampMoistureThreshold) {
            biome = BiomeType::Swamp;
        } else if (moisture > cfg.forestMoistureThreshold) {
            biome = BiomeType::Forest;
        } else {
            biome = BiomeType::Plains;
        }
    }

    return BiomeSample{biome, temperature, moisture};
}

TerrainProfile profileForBiome(BiomeType biome, const TerrainGenerationSystem::TerrainGenConfig& cfg) {
    switch (biome) {
        case BiomeType::Ocean:     return toTerrainProfile(cfg.ocean);
        case BiomeType::Beach:     return toTerrainProfile(cfg.beach);
        case BiomeType::Desert:    return toTerrainProfile(cfg.desert);
        case BiomeType::Savanna:   return toTerrainProfile(cfg.savanna);
        case BiomeType::Forest:    return toTerrainProfile(cfg.forest);
        case BiomeType::Mountains: return toTerrainProfile(cfg.mountains);
        case BiomeType::Tundra:    return toTerrainProfile(cfg.tundra);
        case BiomeType::Swamp:     return toTerrainProfile(cfg.swamp);
        case BiomeType::Taiga:     return toTerrainProfile(cfg.taiga);
        case BiomeType::Plains:
        default:                   return toTerrainProfile(cfg.plains);
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
        return 10; // Snow
    }
    if (biome.type == BiomeType::Mountains && terrainHeight >= cfg.mountainStoneStartY) {
        return 1; // Stone
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
                               4, 2.0, 0.5);
    const double ridged = std::clamp(ridgedFbm2D(seed + 0xF00D1234ULL,
                                                 wx * cfg.mountainScale,
                                                 wz * cfg.mountainScale,
                                                 5, 2.1, 2.0),
                                     0.0, 1.0);
    const double detail = fbm2D(seed + 0xC0FFEE11ULL,
                                wx * cfg.detailScale,
                                wz * cfg.detailScale,
                                5, 2.0, 0.54);

    const double mountainMaskScale = cfg.macroScale * 1.35;
    const double mountainMaskRaw = toUnit(fbm2D(seed + 0x7C159E37ULL,
                                                wx * mountainMaskScale,
                                                wz * mountainMaskScale,
                                                3, 2.0, 0.5));
    const double mountainMask = std::pow(std::clamp((mountainMaskRaw - 0.34) / 0.66, 0.0, 1.0), 1.4);

    // Shape ridges into sparse peak clusters
    const double ridgeSharp = std::pow(ridged, (biome.type == BiomeType::Mountains) ? 2.8 : 2.2);
    const double peakMaskRaw = toUnit(fbm2D(seed + 0x7F4A7C15ULL,
                                            wx * (cfg.macroScale * 1.35),
                                            wz * (cfg.macroScale * 1.35),
                                            3, 2.0, 0.5));
    const double peakMask = std::pow(std::clamp((peakMaskRaw - 0.40) / 0.60, 0.0, 1.0), 1.8);

    const double cliffNoise = std::clamp(ridgedFbm2D(seed + 0xC11F0FF0ULL,
                                                     wx * (cfg.mountainScale * 2.7),
                                                     wz * (cfg.mountainScale * 2.7),
                                                     4, 2.2, 2.0),
                                         0.0, 1.0);
    const double cliffMask = std::clamp(mountainMask * 0.7 + std::max(0.0, ridgeSharp - 0.52) * 1.05, 0.0, 1.0);
    const double cliffContribution = std::pow(cliffNoise, 2.0) * cliffMask * 0.18;

    const double crackNoise = fbm2D(seed + 0x1234ABCDULL,
                                    wx * (cfg.mountainScale * 3.4),
                                    wz * (cfg.mountainScale * 3.4),
                                    3, 2.0, 0.5);
    const double crackSignal = std::clamp((std::abs(crackNoise) - 0.58) / 0.42, 0.0, 1.0);
    const double crackContribution = crackSignal * cliffMask * 0.12;

    const double ridgeContribution = ridgeSharp * peakMask * mountainMask;

    // Suppress mountain ridges for ocean/beach so they stay flat/underwater
    double biomeMountainBias;
    if (biome.type == BiomeType::Mountains) {
        biomeMountainBias = 1.0;
    } else if (biome.type == BiomeType::Ocean || biome.type == BiomeType::Beach) {
        biomeMountainBias = 0.0;
    } else {
        biomeMountainBias = cfg.nonMountainRidgeBias;
    }

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
                              3, 2.0, 0.55);

    const double wx2 = x + warp * cfg.caveWarpStrength;
    const double wz2 = z - warp * cfg.caveWarpStrength;
    const double squashedY = y * cfg.caveYSquash;

    const double n1 = fbm3D(seed + 0x3C6EF372ULL,
                            wx2 * cfg.caveScale,
                            squashedY * cfg.caveScale,
                            wz2 * cfg.caveScale,
                            4, 2.0, 0.5);
    const double n2 = fbm3D(seed + 0xA54FF53AULL,
                            wx2 * cfg.caveScale + 1.7,
                            squashedY * cfg.caveScale + 3.1,
                            wz2 * cfg.caveScale - 2.4,
                            4, 2.0, 0.5);

    const double depthFactor = std::clamp((static_cast<double>(terrainHeight - worldY) - cfg.caveDepthOffset) / cfg.caveDepthRange,
                                          0.0, 1.0);
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
                                 3, 2.0, 0.5);
    return density > cfg.aquiferThreshold;
}

// ─── Vegetation helpers ─────────────────────────────────────────────

double getTreeDensityForBiome(BiomeType biome,
                              const TerrainGenerationSystem::VegetationConfig& veg) {
    switch (biome) {
        case BiomeType::Forest:  return veg.treeDensityForest;
        case BiomeType::Taiga:   return veg.treeDensityTaiga;
        case BiomeType::Plains:  return veg.treeDensityPlains;
        case BiomeType::Savanna: return veg.treeDensitySavanna;
        case BiomeType::Swamp:   return veg.treeDensitySwamp;
        case BiomeType::Desert:  return veg.cactusDensityDesert;
        default:                 return 0.0;
    }
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

    // ═══ Phase 1: Pre-compute column info ═══

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

    // ═══ Phase 2: Terrain block fill ═══

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

                auto isTerrainSolidAt = [&](int sy) {
                    if (sy <= cfg.waterLevelY) return true; // everything under water logic acts like base
                    if (sy == cfg.bedrockY) return true;
                    double d = static_cast<double>(terrainHeight - sy);
                    if (std::abs(d) < cfg.overhangBandHalfWidth) {
                        double overhangNoise = fbm3D(seed + 0x98765432ULL,
                                                     worldX * cfg.overhangScale,
                                                     sy * cfg.overhangScale,
                                                     worldZ * cfg.overhangScale,
                                                     3, 2.0, 0.5);
                        d += overhangNoise * cfg.overhangStrength;
                    }
                    if (d >= 0.0) {
                        return !shouldCarveCave(seed, worldX, sy, worldZ, terrainHeight, cfg);
                    }
                    return false;
                };

                if (worldY == cfg.bedrockY) {
                    blockId = 4;
                } else if (isTerrainSolidAt(worldY)) {
                    int depth = 0;
                    for (int lookY = worldY + 1; lookY <= worldY + profile.fillerDepth; ++lookY) {
                        if (isTerrainSolidAt(lookY)) {
                            depth++;
                        } else {
                            break;
                        }
                    }

                    if (worldY <= cfg.deepStoneCutoffY) {
                        blockId = profile.deepBlock;
                    } else if (depth == 0) {
                        bool isUnderWater = (worldY < cfg.waterLevelY) && !isTerrainSolidAt(worldY + 1);
                        if (isUnderWater) {
                            const int waterDepth = cfg.waterLevelY - worldY;
                            if (waterDepth <= 3) {
                                blockId = 6;
                            } else if (waterDepth <= 8) {
                                blockId = 8;
                            } else {
                                blockId = 9;
                            }
                        } else {
                            blockId = sampleSurfaceBlock(profile, biome, worldY, cfg);
                            if (blockId == profile.topBlock && blockId != 0) {
                                const int distToWater = cfg.waterLevelY - worldY;
                                if (distToWater >= 0 && distToWater <= 3) {
                                    blockId = 6;
                                }
                            }
                        }
                    } else if (depth < profile.fillerDepth) {
                        blockId = profile.fillerBlock;
                    } else {
                        blockId = profile.deepBlock;
                    }

                    if (blockId == profile.deepBlock && blockId != 0) {
                        for (const auto& ore : cfg.ores) {
                            if (!ore.enabled) continue;
                            if (worldY > ore.maxY || worldY < ore.minY) continue;
                            double vein = fbm3D(seed ^ static_cast<std::uint64_t>(ore.id) * 0xDEAD1337ULL,
                                                worldX * ore.scale,
                                                worldY * ore.scale,
                                                worldZ * ore.scale,
                                                2, 2.0, 0.5);
                            if (toUnit(vein) > ore.threshold) {
                                blockId = ore.id;
                                break;
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
                } else if (worldY <= cfg.waterLevelY) {
                    blockId = 3;
                }

                if (worldY == cfg.bedrockY) {
                    blockId = 4;
                }

                chunk.setBlock(x, y, z, blockId);
            }
        }
    }

    // ═══ Phase 3: Vegetation (trees, cacti, tall grass) ═══

    constexpr int kCanopyExtent = 3; // max distance a canopy can extend from trunk

    for (int tz = -kCanopyExtent; tz < Chunk::kSizeZ + kCanopyExtent; ++tz) {
        for (int tx = -kCanopyExtent; tx < Chunk::kSizeX + kCanopyExtent; ++tx) {
            const int wx = chunkX * Chunk::kSizeX + tx;
            const int wz = chunkZ * Chunk::kSizeZ + tz;

            // ── Deterministic tree placement hash ──
            std::uint64_t treeKey = static_cast<std::uint64_t>(static_cast<std::uint32_t>(wx)) * 198491317ULL
                                  ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(wz)) * 6542989ULL
                                  ^ seed ^ 0x78EE5EEDULL;
            std::uint64_t tk = treeKey;
            double treeProbability = static_cast<double>(splitmix64(tk) >> 11) * (1.0 / 9007199254740992.0);

            // Get biome for tree density — reuse precomputed data for inner columns
            BiomeSample treeBiome;
            if (tx >= 0 && tx < Chunk::kSizeX && tz >= 0 && tz < Chunk::kSizeZ) {
                treeBiome = columns[tx][tz].biome;
            } else {
                treeBiome = sampleBiome(seed, wx, wz, cfg);
            }

            const double density = getTreeDensityForBiome(treeBiome.type, cfg.vegetation);
            if (density <= 0.0 || treeProbability >= density) continue;

            int precompH = (tx >= 0 && tx < Chunk::kSizeX && tz >= 0 && tz < Chunk::kSizeZ) ? columns[tx][tz].terrainHeight : sampleBlendedTerrainHeight(seed, wx, wz, cfg);

            auto isTerrainSolidAtGlobal = [&](int gx, int gy, int gz) {
                if (gy == cfg.bedrockY) return true;
                if (gy <= cfg.waterLevelY) return true;
                double d = static_cast<double>(precompH - gy);
                if (std::abs(d) < cfg.overhangBandHalfWidth) {
                    double overhangNoise = fbm3D(seed + 0x98765432ULL, gx * cfg.overhangScale, gy * cfg.overhangScale, gz * cfg.overhangScale, 3, 2.0, 0.5);
                    d += overhangNoise * cfg.overhangStrength;
                }
                if (d >= 0.0) {
                    return !shouldCarveCave(seed, gx, gy, gz, precompH, cfg);
                }
                return false;
            };

            int actualSurfaceY = -1;
            for (int sy = std::min(Chunk::kSizeY - 1, precompH + 24); sy >= std::max(0, precompH - 24); --sy) {
                if (isTerrainSolidAtGlobal(wx, sy, wz)) {
                    actualSurfaceY = sy;
                    break;
                }
            }

            if (actualSurfaceY == -1 || actualSurfaceY <= cfg.waterLevelY) continue;
            if (treeBiome.type == BiomeType::Mountains && actualSurfaceY > cfg.mountainStoneStartY - 10) continue;

            int treeBaseY = actualSurfaceY;


            // ── Tree shape from hash ──
            std::uint64_t sk = treeKey ^ 0x12345678ULL;
            std::uint64_t shapeHash = splitmix64(sk);

            if (treeBiome.type == BiomeType::Desert) {
                // ── Cactus ──
                int cactusH = 1 + static_cast<int>(shapeHash % 3); // 1-3
                if (tx >= 0 && tx < Chunk::kSizeX && tz >= 0 && tz < Chunk::kSizeZ) {
                    for (int h = 1; h <= cactusH; ++h) {
                        int wy = treeBaseY + h;
                        int ly = wy - chunkY * Chunk::kSizeY;
                        if (ly >= 0 && ly < Chunk::kSizeY) {
                            chunk.setBlock(tx, ly, tz, 18); // Cactus
                        }
                    }
                }
            } else {
                // ── Oak tree ──
                int trunkH = cfg.vegetation.oakTrunkMin +
                             static_cast<int>(shapeHash % static_cast<std::uint64_t>(
                                 cfg.vegetation.oakTrunkMax - cfg.vegetation.oakTrunkMin + 1));
                int canopyR = cfg.vegetation.canopyRadius;
                int canopyBottom = trunkH - 2;  // canopy starts 2 below trunk top
                int canopyTop = trunkH + 1;     // canopy extends 1 above trunk top

                // Taiga: taller trunk, narrower/conical canopy feel
                if (treeBiome.type == BiomeType::Taiga) {
                    trunkH += 2;
                    canopyBottom = trunkH - 3;
                    canopyTop = trunkH + 1;
                }

                // Swamp: shorter, wider
                if (treeBiome.type == BiomeType::Swamp) {
                    trunkH = std::max(3, trunkH - 1);
                    canopyBottom = trunkH - 2;
                    canopyTop = trunkH + 1;
                    canopyR = 3;
                }

                // Place trunk (only for columns inside this chunk)
                if (tx >= 0 && tx < Chunk::kSizeX && tz >= 0 && tz < Chunk::kSizeZ) {
                    for (int h = 1; h <= trunkH; ++h) {
                        int wy = treeBaseY + h;
                        int ly = wy - chunkY * Chunk::kSizeY;
                        if (ly >= 0 && ly < Chunk::kSizeY) {
                            chunk.setBlock(tx, ly, tz, 15); // Oak_Log
                        }
                    }
                }

                // Place canopy (leaves can extend into this chunk from neighboring columns)
                for (int dy = canopyBottom; dy <= canopyTop; ++dy) {
                    int layerR = canopyR;
                    // Top layer is smaller for rounded look
                    if (dy >= trunkH) layerR = std::max(0, canopyR - (dy - trunkH + 1));
                    if (dy == canopyBottom) layerR = std::max(1, canopyR - 1);

                    for (int ddx = -layerR; ddx <= layerR; ++ddx) {
                        for (int ddz = -layerR; ddz <= layerR; ++ddz) {
                            // Skip trunk center on lower canopy layers
                            if (ddx == 0 && ddz == 0 && dy <= trunkH - 1) continue;

                            // Rounded corners: skip diagonals at max radius
                            if (std::abs(ddx) == layerR && std::abs(ddz) == layerR && layerR > 1) {
                                std::uint64_t cornerKey = treeKey ^
                                    (static_cast<std::uint64_t>(static_cast<std::uint32_t>(ddx)) * 7919ULL +
                                     static_cast<std::uint64_t>(static_cast<std::uint32_t>(ddz)) * 7823ULL +
                                     static_cast<std::uint64_t>(static_cast<std::uint32_t>(dy)) * 7727ULL);
                                std::uint64_t ck = cornerKey;
                                if (splitmix64(ck) & 1) continue;
                            }

                            int lx = tx + ddx;
                            int lz = tz + ddz;
                            if (lx < 0 || lx >= Chunk::kSizeX || lz < 0 || lz >= Chunk::kSizeZ) continue;

                            int wy = treeBaseY + dy;
                            int ly = wy - chunkY * Chunk::kSizeY;
                            if (ly < 0 || ly >= Chunk::kSizeY) continue;

                            // Only place leaf where there's currently air
                            if (chunk.getBlock(lx, ly, lz) == 0) {
                                chunk.setBlock(lx, ly, lz, 16); // Oak_Leaves
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Phase 3b: Surface decoration (tall grass on grass blocks) ──
    for (int z = 0; z < Chunk::kSizeZ; ++z) {
        for (int x = 0; x < Chunk::kSizeX; ++x) {
            const ColumnInfo& col = columns[x][z];
            // Only decorate grass-topped biomes
            if (col.biome.type == BiomeType::Ocean || col.biome.type == BiomeType::Beach ||
                col.biome.type == BiomeType::Desert || col.biome.type == BiomeType::Tundra) {
                continue;
            }

            const int surfaceY = col.terrainHeight;
            if (surfaceY <= cfg.waterLevelY) continue;

            const int localSurfY = surfaceY - chunkY * Chunk::kSizeY;
            const int aboveY = localSurfY + 1;
            if (localSurfY < 0 || localSurfY >= Chunk::kSizeY) continue;
            if (aboveY < 0 || aboveY >= Chunk::kSizeY) continue;

            // Only decorate if surface is grass and above is air
            if (chunk.getBlock(x, localSurfY, z) != 5) continue;
            if (chunk.getBlock(x, aboveY, z) != 0) continue;

            const int worldX = chunkX * Chunk::kSizeX + x;
            const int worldZ = chunkZ * Chunk::kSizeZ + z;
            std::uint64_t grassKey = static_cast<std::uint64_t>(static_cast<std::uint32_t>(worldX)) * 48271ULL
                                   ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(worldZ)) * 40692ULL
                                   ^ seed ^ 0x68A55DECULL;
            std::uint64_t gk = grassKey;
            double grassProb = static_cast<double>(splitmix64(gk) >> 11) * (1.0 / 9007199254740992.0);

            if (grassProb < cfg.vegetation.tallGrassDensity) {
                chunk.setBlock(x, aboveY, z, 17); // Tall_Grass
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
        case BiomeType::Ocean:     return "Ocean";
        case BiomeType::Beach:     return "Beach";
        case BiomeType::Desert:    return "Desert";
        case BiomeType::Savanna:   return "Savanna";
        case BiomeType::Forest:    return "Forest";
        case BiomeType::Mountains: return "Mountains";
        case BiomeType::Tundra:    return "Tundra";
        case BiomeType::Swamp:     return "Swamp";
        case BiomeType::Taiga:     return "Taiga";
        case BiomeType::Plains:
        default:                   return "Plains";
    }
}

} // namespace TerrainGenerationSystem
