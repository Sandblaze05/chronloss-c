#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class Chunk;

namespace TerrainGenerationSystem {

enum class BiomeType {
    Ocean,
    Beach,
    Desert,
    Savanna,
    Plains,
    Forest,
    Swamp,
    Taiga,
    Tundra,
    Mountains
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

struct VegetationConfig {
    double treeDensityForest = 0.022;
    double treeDensityTaiga = 0.028;
    double treeDensityPlains = 0.003;
    double treeDensitySavanna = 0.005;
    double treeDensitySwamp = 0.012;
    double cactusDensityDesert = 0.006;
    int oakTrunkMin = 4;
    int oakTrunkMax = 6;
    int canopyRadius = 2;
    double tallGrassDensity = 0.18;
    double flowerDensity = 0.012;
};

struct TerrainGenConfig {
    // Biome noise scales
    double tempScale = 1.0 / 800.0;
    double moistureScale = 1.0 / 700.0;

    // Continentalness — separates ocean from land
    double continentalnessScale = 1.0 / 1800.0;
    double oceanThreshold = 0.40;
    double beachThreshold = 0.45;

    // Biome classification thresholds (kept for UI compatibility)
    double desertTemperatureThreshold = 0.58;
    double desertMoistureThreshold = 0.38;
    double mountainTemperatureThreshold = 0.42;
    double mountainMoistureThreshold = 0.45;
    double swampMoistureThreshold = 0.62;
    double forestMoistureThreshold = 0.45;

    // Noise contrast for biome classification (power < 1 spreads values)
    double biomeNoiseContrast = 0.55;

    // Per-biome terrain profiles
    BiomeProfileConfig ocean{};
    BiomeProfileConfig beach{};
    BiomeProfileConfig desert{};
    BiomeProfileConfig savanna{};
    BiomeProfileConfig plains{};
    BiomeProfileConfig forest{};
    BiomeProfileConfig swamp{};
    BiomeProfileConfig taiga{};
    BiomeProfileConfig tundra{};
    BiomeProfileConfig mountains{};

    // Terrain shape
    double terrainWarpScale = 1.0 / 170.0;
    double terrainWarpStrength = 45.0;
    double macroScale = 1.0 / 700.0;
    double mountainScale = 1.0 / 220.0;
    double detailScale = 1.0 / 92.0;
    double macroWeight = 0.52;
    double ridgeWeight = 1.75;
    double detailWeight = 0.28;
    double nonMountainRidgeBias = 0.20;
    double swampHeightCap = 66.0;

    int blendRadius = 12;
    std::array<double, 5> blendWeights = {{0.70, 0.075, 0.075, 0.075, 0.075}};

    // Caves
    double caveScale = 1.0 / 42.0;
    double caveWarpScale = 1.0 / 96.0;
    double caveWarpStrength = 24.0;
    int caveSurfaceGuard = 14;
    double caveYSquash = 0.5;
    double caveDepthOffset = 10.0;
    double caveDepthRange = 80.0;
    double caveThresholdBase = 0.055;
    double caveThresholdDepthGain = 0.025;

    // Aquifers
    double aquiferScale = 1.0 / 68.0;
    double aquiferYScaleMul = 1.2;
    double aquiferThreshold = 0.3;
    int aquiferDepthYOffset = 8;

    // Layering and surface
    int bedrockY = -100;
    int deepStoneCutoffY = 18;
    int waterLevelY = 64;
    double topSurfaceDensityThreshold = 3.0;
    int snowStartY = 158;
    int mountainStoneStartY = 130;

    // Overhangs
    double overhangBandHalfWidth = 18.0;
    double overhangScale = 1.0 / 55.0;
    double overhangStrength = 10.0;

    // Ores
    std::array<OreConfig, 5> ores{};

    // Vegetation (trees, cacti, tall grass, flowers)
    VegetationConfig vegetation{};
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
