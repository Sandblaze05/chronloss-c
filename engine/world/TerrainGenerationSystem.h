#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class Chunk;

namespace TerrainGenerationSystem {

enum class BiomeType {
    Desert,
    Plains,
    Forest,
    Mountains,
    Tundra,
    Swamp
};

struct TerrainDebugSample {
    BiomeType biome = BiomeType::Plains;
    double temperature = 0.5;
    double moisture = 0.5;
    int terrainHeight = 0;
    std::uint8_t surfaceBlock = 0;
};

struct BiomeProfileConfig {
    std::uint8_t topBlock = 5;
    std::uint8_t fillerBlock = 2;
    std::uint8_t deepBlock = 1;
    int fillerDepth = 4;
    int baseHeight = 56;
    int heightAmplitude = 20;
};

struct OreConfig {
    std::uint8_t id = 11;
    double scale = 1.0 / 14.0;
    double threshold = 0.72;
    int maxY = 32;
    int minY = -100;
    bool enabled = true;
};

struct TerrainGenConfig {
    double tempScale = 1.0 / 1024.0;
    double moistureScale = 1.0 / 896.0;
    double desertTemperatureThreshold = 0.72;
    double desertMoistureThreshold = 0.34;
    double mountainTemperatureThreshold = 0.28;
    double mountainMoistureThreshold = 0.45;
    double swampMoistureThreshold = 0.72;
    double forestMoistureThreshold = 0.52;

    BiomeProfileConfig desert{};
    BiomeProfileConfig plains{};
    BiomeProfileConfig forest{};
    BiomeProfileConfig mountains{};
    BiomeProfileConfig tundra{};
    BiomeProfileConfig swamp{};

    double terrainWarpScale = 1.0 / 150.0;
    double terrainWarpStrength = 45.0;

    double macroScale = 1.0 / 768.0;
    double mountainScale = 1.0 / 280.0;
    double detailScale = 1.0 / 84.0;
    double macroWeight = 0.50;
    double ridgeWeight = 1.25;
    double detailWeight = 0.28;
    double nonMountainRidgeBias = 0.22;
    double swampHeightCap = 66.0;

    int blendRadius = 16;
    std::array<double, 5> blendWeights = {{0.62, 0.095, 0.095, 0.095, 0.095}};

    double caveScale = 1.0 / 42.0;
    double caveWarpScale = 1.0 / 96.0;
    double caveWarpStrength = 24.0;
    int caveSurfaceGuard = 14;
    double caveYSquash = 0.5;
    double caveDepthOffset = 10.0;
    double caveDepthRange = 80.0;
    double caveThresholdBase = 0.055;
    double caveThresholdDepthGain = 0.025;

    double aquiferScale = 1.0 / 68.0;
    double aquiferYScaleMul = 1.2;
    double aquiferThreshold = 0.3;
    int aquiferDepthYOffset = 8;

    int bedrockY = -100;
    int deepStoneCutoffY = 18;
    int waterLevelY = 64;
    double topSurfaceDensityThreshold = 3.0;
    int snowStartY = 126;
    int mountainStoneStartY = 108;

    double overhangBandHalfWidth = 30.0;
    double overhangScale = 1.0 / 55.0;
    double overhangStrength = 15.0;

    std::array<OreConfig, 5> ores{};
};

TerrainGenConfig getConfig();
void setConfig(const TerrainGenConfig& config);
void resetConfig();

TerrainDebugSample sampleDebugAt(std::uint64_t seed, int worldX, int worldZ);
const char* biomeTypeToString(BiomeType biome);

void generateChunkTerrain(std::uint64_t seed,
                          Chunk& chunk,
                          int chunkX,
                          int chunkY,
                          int chunkZ);

} // namespace TerrainGenerationSystem
