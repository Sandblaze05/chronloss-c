#include "Application.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
#include <array>
#include <cmath>
#include <chrono>
#include <string>
#include "engine/render/Renderer.h"
#include "engine/client/Player.h"
#include "engine/Physics/PhysicsSystem.h"
#include "engine/world/Block.h"
#include "engine/world/TerrainGenerationSystem.h"
#include "engine/world/WorldInteractionSystem.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

Application::Application() {
	init();
}

Application::~Application() {
	shutdown();
}

void Application::init() {
	if (!glfwInit()) {
		throw std::runtime_error("Failed to init GLFW");
	}
}

void Application::shutdown() {
	glfwTerminate();
}

void Application::run() {
    // Create a non-resizable, maximized window
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    // Request the window to start maximized where supported
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    // request 4x MSAA buffer
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Chronloss-c", nullptr, nullptr);

    if (!window) {
        throw std::runtime_error("Failed to create window");
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        throw std::runtime_error("Failed to initialize GLAD");
    }


    // Update viewport to actual framebuffer size (handles maximized window)
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    glViewport(0, 0, fbw > 0 ? fbw : 800, fbh > 0 ? fbh : 600);

    Player player(0.0f, 1.0f, 0.0f);
    Renderer renderer;

    // Initialize block registry once
    BlockRegistry::init();

    // connect scroll wheel to renderer zoom
    glfwSetWindowUserPointer(window, &renderer);
    glfwSetScrollCallback(window, [](GLFWwindow* win, double xoffset, double yoffset) {
        void* ptr = glfwGetWindowUserPointer(win);
        if (!ptr) return;
        static_cast<Renderer*>(ptr)->onScroll(xoffset, yoffset);
    });

    // forward mouse button events for orbit control
    glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
        void* ptr = glfwGetWindowUserPointer(win);
        if (!ptr) return;
        // update last cursor pos first so we don't get a big jump
        double x, y;
        glfwGetCursorPos(win, &x, &y);
        static_cast<Renderer*>(ptr)->onCursorPos(x, y);
        static_cast<Renderer*>(ptr)->onMouseButton(button, action, mods);
    });

    // forward cursor movement to renderer
    glfwSetCursorPosCallback(window, [](GLFWwindow* win, double xpos, double ypos) {
        void* ptr = glfwGetWindowUserPointer(win);
        if (!ptr) return;
        static_cast<Renderer*>(ptr)->onCursorPos(xpos, ypos);
    });

    // set a clear color
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);

    float lastFrame = 0.0f;
    float cpuFrameTimeMs = 0.0f;
    float gpuFrameTimeMs = 0.0f;
    float physicsAccumulator = 0.0f;
    constexpr float kPhysicsDt = 1.0f / 60.0f;
    constexpr int kMaxPhysicsStepsPerFrame = 5;
    constexpr int kSpawnSearchRadius = 12;
    constexpr int kSpawnScanMinY = -100;
    constexpr int kSpawnScanMaxY = 255;
    PhysicsSystem physics;
    WorldInteractionSystem worldInteraction;
    std::array<PhysicsBody*, 1> physicsBodies = { &player.body };
    bool spawnPrechecked = false;

    // Camera offsets control the camera position relative to the player.
    // Tweak these to change viewing angle and WASD orientation.
    float camOffsetX = 10.0f;
    float camOffsetY = 15.0f;
    float camOffsetZ = 10.0f;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    GLuint gpuTimeQueries[2] = {0, 0};
    bool gpuQueryIssued[2] = {false, false};
    int gpuQueryIndex = 0;
    glGenQueries(2, gpuTimeQueries);

    auto isBodyAreaReadyAt = [&](float bodyX, float bodyZ, int feetY) -> bool {
        const float minX = bodyX - player.body.width * 0.5f;
        const float maxX = bodyX + player.body.width * 0.5f;
        const float minZ = bodyZ - player.body.width * 0.5f;
        const float maxZ = bodyZ + player.body.width * 0.5f;

        const int minXi = static_cast<int>(std::floor(minX));
        const int maxXi = static_cast<int>(std::floor(maxX));
        const int minZi = static_cast<int>(std::floor(minZ));
        const int maxZi = static_cast<int>(std::floor(maxZ));
        const int minYi = feetY;
        const int maxYi = static_cast<int>(std::floor(static_cast<float>(feetY) + player.body.height));

        ChunkStreamer& streamer = renderer.getStreamer();
        for (int x = minXi; x <= maxXi; ++x) {
            for (int y = minYi; y <= maxYi; ++y) {
                for (int z = minZi; z <= maxZi; ++z) {
                    if (!streamer.isBlockDataReadyAtWorld(x, y, z)) {
                        return false;
                    }
                }
            }
        }

        return true;
    };

    auto isBodyOverlappingSolidAt = [&](float bodyX, float bodyZ, int feetY) -> bool {
        const float minX = bodyX - player.body.width * 0.5f;
        const float maxX = bodyX + player.body.width * 0.5f;
        const float minZ = bodyZ - player.body.width * 0.5f;
        const float maxZ = bodyZ + player.body.width * 0.5f;

        const int minXi = static_cast<int>(std::floor(minX));
        const int maxXi = static_cast<int>(std::floor(maxX));
        const int minZi = static_cast<int>(std::floor(minZ));
        const int maxZi = static_cast<int>(std::floor(maxZ));
        const int minYi = feetY;
        const int maxYi = static_cast<int>(std::floor(static_cast<float>(feetY) + player.body.height));

        ChunkStreamer& streamer = renderer.getStreamer();
        for (int x = minXi; x <= maxXi; ++x) {
            for (int y = minYi; y <= maxYi; ++y) {
                for (int z = minZi; z <= maxZi; ++z) {
                    const std::uint8_t id = streamer.getBlockAtWorld(x, y, z);
                    if (BlockRegistry::get(id).isSolid()) {
                        return true;
                    }
                }
            }
        }

        return false;
    };

    auto findSurfaceFeetY = [&](int worldX, int worldZ, int& outFeetY) -> bool {
        ChunkStreamer& streamer = renderer.getStreamer();
        bool seenLoadedCell = false;

        for (int y = kSpawnScanMaxY; y >= kSpawnScanMinY; --y) {
            if (!streamer.isBlockDataReadyAtWorld(worldX, y, worldZ)) {
                if (seenLoadedCell) {
                    break;
                }
                continue;
            }

            seenLoadedCell = true;
            const std::uint8_t id = streamer.getBlockAtWorld(worldX, y, worldZ);
            if (BlockRegistry::get(id).isSolid()) {
                outFeetY = y + 1;
                return true;
            }
        }

        return false;
    };

    while (!glfwWindowShouldClose(window)) {
        const auto cpuFrameStart = std::chrono::steady_clock::now();

        // Calculate Delta Time
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        if (gpuQueryIssued[gpuQueryIndex]) {
            GLint available = 0;
            glGetQueryObjectiv(gpuTimeQueries[gpuQueryIndex], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available) {
                GLuint64 elapsedNs = 0;
                glGetQueryObjectui64v(gpuTimeQueries[gpuQueryIndex], GL_QUERY_RESULT, &elapsedNs);
                gpuFrameTimeMs = static_cast<float>(static_cast<double>(elapsedNs) / 1e6);
            }
        }

        glBeginQuery(GL_TIME_ELAPSED, gpuTimeQueries[gpuQueryIndex]);

        // Update Game Logic using live camera-relative movement axes.
        float camForwardX = 0.0f, camForwardZ = -1.0f;
        float camRightX = 1.0f, camRightZ = 0.0f;
        renderer.getGroundAxes(camForwardX, camForwardZ, camRightX, camRightZ);
        player.update(window, deltaTime, camForwardX, camForwardZ, camRightX, camRightZ, renderer.getStreamer());

        // Request terrain around the player before running physics.
        renderer.getStreamer().tick(player.getX(), player.getY(), player.getZ());

        if (!spawnPrechecked) {
            const int baseX = static_cast<int>(std::floor(player.body.pos[0]));
            const int baseZ = static_cast<int>(std::floor(player.body.pos[2]));

            bool foundSafeSpawn = false;
            for (int radius = 0; radius <= kSpawnSearchRadius && !foundSafeSpawn; ++radius) {
                for (int dz = -radius; dz <= radius && !foundSafeSpawn; ++dz) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (radius > 0 && std::abs(dx) != radius && std::abs(dz) != radius) {
                            continue;
                        }

                        const int candidateX = baseX + dx;
                        const int candidateZ = baseZ + dz;
                        int candidateFeetY = 0;
                        if (!findSurfaceFeetY(candidateX, candidateZ, candidateFeetY)) {
                            continue;
                        }

                        const float candidatePosX = static_cast<float>(candidateX) + 0.5f;
                        const float candidatePosZ = static_cast<float>(candidateZ) + 0.5f;

                        if (!isBodyAreaReadyAt(candidatePosX, candidatePosZ, candidateFeetY)) {
                            continue;
                        }
                        if (isBodyOverlappingSolidAt(candidatePosX, candidatePosZ, candidateFeetY)) {
                            continue;
                        }

                        player.body.pos[0] = candidatePosX;
                        player.body.pos[1] = static_cast<float>(candidateFeetY);
                        player.body.pos[2] = candidatePosZ;
                        player.body.vel[0] = 0.0f;
                        player.body.vel[1] = 0.0f;
                        player.body.vel[2] = 0.0f;
                        spawnPrechecked = true;
                        foundSafeSpawn = true;
                        break;
                    }
                }
            }
        }

        const int px = static_cast<int>(std::floor(player.body.pos[0]));
        const int feetY = static_cast<int>(std::floor(player.body.pos[1]));
        const int headY = static_cast<int>(std::floor(player.body.pos[1] + player.body.height));
        const int pz = static_cast<int>(std::floor(player.body.pos[2]));
        const bool spawnAreaReady =
            renderer.getStreamer().isBlockDataReadyAtWorld(px, feetY - 1, pz) &&
            renderer.getStreamer().isBlockDataReadyAtWorld(px, feetY, pz) &&
            renderer.getStreamer().isBlockDataReadyAtWorld(px, headY, pz);

        if (spawnPrechecked && spawnAreaReady) {
            physicsAccumulator += deltaTime;
            int physicsSteps = 0;
            while (physicsAccumulator >= kPhysicsDt && physicsSteps < kMaxPhysicsStepsPerFrame) {
                for (PhysicsBody* physicsBody : physicsBodies) {
                    physics.update(*physicsBody, kPhysicsDt, renderer.getStreamer());
                }
                physicsAccumulator -= kPhysicsDt;
                ++physicsSteps;
            }
        } else {
            // Keep player suspended until local terrain data is available.
            player.body.vel[1] = 0.0f;
            physicsAccumulator = 0.0f;
        }

        // Render the Game (Pass the player's coordinates, camera offsets, and deltaTime)
        renderer.beginFrame(player.getX(), player.getY(), player.getZ(),
                camOffsetX, camOffsetY, camOffsetZ,
                deltaTime);
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const bool isLeftMousePressed = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
        const bool isRightMousePressed = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);

        ChunkStreamer& streamer = renderer.getStreamer();
        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        worldInteraction.updateSelection(renderer.getLastCameraFrameData(),
                                         mouseX,
                                         mouseY,
                                         player.getX(),
                                         player.getY(),
                                         player.getZ(),
                                         streamer);

        int highlightX = 0;
        int highlightY = 0;
        int highlightZ = 0;
        if (worldInteraction.hasHoveredBlock()) {
            worldInteraction.getHoveredBlock(highlightX, highlightY, highlightZ);
            renderer.setHighlightBlock(true, highlightX, highlightY, highlightZ);
        } else {
            renderer.setHighlightBlock(false, 0, 0, 0);
        }

        if (!ImGui::GetIO().WantCaptureMouse && !renderer.isOrbiting()) {
            worldInteraction.applyMouseActions(isLeftMousePressed,
                                               isRightMousePressed,
                                               deltaTime,
                                               streamer,
                                               player.body);
        }

        ImGui::Begin("Engine Debug");
        ImGui::Text("FPS: %.1f", io.Framerate);
        ImGui::Separator();
        ImGui::Text("Player Position:");
        ImGui::Text("X: %.2f", player.getX());
        ImGui::Text("Y: %.2f", player.getY());
        ImGui::Text("Z: %.2f", player.getZ());
        ImGui::Text("onGround? %s", player.body.onGround ? "true" : "false");
        ImGui::Separator();
        ImGui::Text("Performance:");
        ImGui::Text("Frame time (ms): %.2f", deltaTime * 1000.0f);
        ImGui::Text("CPU frame time: %.2f", cpuFrameTimeMs);
        ImGui::Text("GPU frame time: %.2f", gpuFrameTimeMs);
        ImGui::Text("Chunk gen time: %.2f", renderer.getStreamer().getAverageChunkGenMs());
        ImGui::Text("Chunk mesh time: %.2f", renderer.getStreamer().getAverageChunkMeshMs());
        ImGui::Text("Draw calls: %zu", renderer.getLastDrawCallCount());
        ImGui::Text("Vertex count: %zu", renderer.getLastVertexCount());
        ImGui::Separator();
        ImGui::Text("World State:");
        ImGui::Text("Loaded Chunks: %zu", renderer.getLoadedChunkCount());
        ImGui::Text("Avg chunk load: %.2f ms", renderer.getStreamer().getAverageChunkLoadMs());
        const int playerBlockX = static_cast<int>(std::floor(player.getX()));
        const int playerBlockZ = static_cast<int>(std::floor(player.getZ()));
        const TerrainGenerationSystem::TerrainDebugSample playerTerrainDebug =
            TerrainGenerationSystem::sampleDebugAt(streamer.getSeed(), playerBlockX, playerBlockZ);
        const BlockData& playerSurfaceBlock = BlockRegistry::get(playerTerrainDebug.surfaceBlock);
        ImGui::Text("Biome @ Player: %s", TerrainGenerationSystem::biomeTypeToString(playerTerrainDebug.biome));
        ImGui::Text("Temp/Moisture: %.2f / %.2f", playerTerrainDebug.temperature, playerTerrainDebug.moisture);
        ImGui::Text("Terrain Height @ Player XZ: %d", playerTerrainDebug.terrainHeight);
        ImGui::Text("Expected Surface @ Player XZ: %s", playerSurfaceBlock.name.c_str());
        if (ImGui::Button("Reset Chunk Load Stats")) {
            renderer.getStreamer().resetLoadStats();
        }
        // Expose chunk streamer radii so we can change render distance at runtime
        {
            ChunkStreamer& streamer = renderer.getStreamer();
            int horiz = streamer.getRadius();
            int vert = streamer.getVerticalRadius();
            if (ImGui::SliderInt("Chunk Radius", &horiz, 1, 12)) {
                streamer.setRadius(horiz);
            }
            if (ImGui::SliderInt("Vertical Radius", &vert, 0, 6)) {
                streamer.setVerticalRadius(vert);
            }
        }
        {
            float range = worldInteraction.getInteractionRange();
            if (ImGui::SliderFloat("Interaction Range", &range, 1.0f, 12.0f)) {
                worldInteraction.setInteractionRange(range);
            }
        }

        ImGui::Separator();
        ImGui::Text("Mining:");
        if (worldInteraction.hasHoveredBlock()) {
            int miningX = 0;
            int miningY = 0;
            int miningZ = 0;
            worldInteraction.getHoveredBlock(miningX, miningY, miningZ);

            const std::uint8_t miningBlockId = streamer.getBlockAtWorld(miningX, miningY, miningZ);
            const BlockData& miningBlockData = BlockRegistry::get(miningBlockId);
            const TerrainGenerationSystem::TerrainDebugSample targetTerrainDebug =
                TerrainGenerationSystem::sampleDebugAt(streamer.getSeed(), miningX, miningZ);
            const BlockData& targetSurfaceBlock = BlockRegistry::get(targetTerrainDebug.surfaceBlock);
            const float miningProgress = worldInteraction.getMiningProgress();
            const float blockToughness = static_cast<float>(miningBlockData.toughness);

            ImGui::Text("Target: (%d, %d, %d)", miningX, miningY, miningZ);
            ImGui::Text("Block: %s", miningBlockData.name.c_str());
            ImGui::Text("Biome @ Target XZ: %s", TerrainGenerationSystem::biomeTypeToString(targetTerrainDebug.biome));
            ImGui::Text("Target Temp/Moisture: %.2f / %.2f", targetTerrainDebug.temperature, targetTerrainDebug.moisture);
            ImGui::Text("Terrain Height @ Target XZ: %d", targetTerrainDebug.terrainHeight);
            ImGui::Text("Expected Surface @ Target XZ: %s", targetSurfaceBlock.name.c_str());

            if (blockToughness < 0.0f) {
                ImGui::Text("Progress: Unbreakable");
            } else if (blockToughness <= 0.0f) {
                ImGui::Text("Progress: Instant");
            } else {
                float miningPercent = (miningProgress / blockToughness) * 100.0f;
                if (miningPercent < 0.0f) miningPercent = 0.0f;
                if (miningPercent > 100.0f) miningPercent = 100.0f;
                ImGui::Text("Progress: %.2f / %.2f s (%.0f%%)",
                            miningProgress,
                            blockToughness,
                            miningPercent);
                ImGui::ProgressBar(miningPercent / 100.0f, ImVec2(0.0f, 0.0f));
            }
        } else {
            ImGui::Text("Progress: No target");
        }
        ImGui::End();

        {
            auto sliderDouble = [](const char* label, double& value, float minValue, float maxValue) {
                float v = static_cast<float>(value);
                if (ImGui::SliderFloat(label, &v, minValue, maxValue)) {
                    value = static_cast<double>(v);
                    return true;
                }
                return false;
            };

            auto editBiomeProfile = [](const char* label, TerrainGenerationSystem::BiomeProfileConfig& profile) {
                bool changed = false;
                if (ImGui::TreeNode(label)) {
                    int topBlock = static_cast<int>(profile.topBlock);
                    int fillerBlock = static_cast<int>(profile.fillerBlock);
                    int deepBlock = static_cast<int>(profile.deepBlock);

                    changed |= ImGui::SliderInt("Top Block", &topBlock, 0, 255);
                    changed |= ImGui::SliderInt("Filler Block", &fillerBlock, 0, 255);
                    changed |= ImGui::SliderInt("Deep Block", &deepBlock, 0, 255);
                    changed |= ImGui::SliderInt("Filler Depth", &profile.fillerDepth, 1, 16);
                    changed |= ImGui::SliderInt("Base Height", &profile.baseHeight, -128, 256);
                    changed |= ImGui::SliderInt("Height Amplitude", &profile.heightAmplitude, 0, 128);

                    profile.topBlock = static_cast<std::uint8_t>(topBlock);
                    profile.fillerBlock = static_cast<std::uint8_t>(fillerBlock);
                    profile.deepBlock = static_cast<std::uint8_t>(deepBlock);

                    ImGui::TreePop();
                }
                return changed;
            };

            auto applyAlpinePreset = [](TerrainGenerationSystem::TerrainGenConfig& cfg) {
                cfg.mountainTemperatureThreshold = 0.44;
                cfg.mountainMoistureThreshold = 0.42;

                cfg.mountains.baseHeight = 82;
                cfg.mountains.heightAmplitude = 110;
                cfg.mountains.fillerDepth = 5;

                cfg.macroScale = 1.0 / 720.0;
                cfg.mountainScale = 1.0 / 240.0;
                cfg.detailScale = 1.0 / 92.0;
                cfg.macroWeight = 0.50;
                cfg.ridgeWeight = 1.55;
                cfg.detailWeight = 0.22;
                cfg.nonMountainRidgeBias = 0.16;

                cfg.terrainWarpScale = 1.0 / 170.0;
                cfg.terrainWarpStrength = 52.0;
                cfg.blendRadius = 12;
                cfg.blendWeights = {{0.70, 0.075, 0.075, 0.075, 0.075}};

                cfg.overhangBandHalfWidth = 18.0;
                cfg.overhangScale = 1.0 / 48.0;
                cfg.overhangStrength = 12.0;

                cfg.snowStartY = 134;
                cfg.mountainStoneStartY = 118;
            };

            auto applyHighlandsPreset = [](TerrainGenerationSystem::TerrainGenConfig& cfg) {
                cfg.mountainTemperatureThreshold = 0.44;
                cfg.mountainMoistureThreshold = 0.40;

                cfg.mountains.baseHeight = 86;
                cfg.mountains.heightAmplitude = 62;
                cfg.mountains.fillerDepth = 5;

                cfg.macroScale = 1.0 / 760.0;
                cfg.mountainScale = 1.0 / 300.0;
                cfg.detailScale = 1.0 / 84.0;
                cfg.macroWeight = 0.58;
                cfg.ridgeWeight = 1.05;
                cfg.detailWeight = 0.30;
                cfg.nonMountainRidgeBias = 0.30;

                cfg.terrainWarpScale = 1.0 / 160.0;
                cfg.terrainWarpStrength = 40.0;
                cfg.blendRadius = 20;
                cfg.blendWeights = {{0.60, 0.10, 0.10, 0.10, 0.10}};

                cfg.overhangBandHalfWidth = 18.0;
                cfg.overhangScale = 1.0 / 56.0;
                cfg.overhangStrength = 10.0;

                cfg.snowStartY = 124;
                cfg.mountainStoneStartY = 108;
            };

            static TerrainGenerationSystem::TerrainGenConfig worldGenConfig = TerrainGenerationSystem::getConfig();
            bool worldGenDirty = false;

            ImGui::Begin("World Generation Tuning");
            ImGui::Text("Applies to newly generated chunks.");
            if (ImGui::Button("Reload From Runtime")) {
                worldGenConfig = TerrainGenerationSystem::getConfig();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Defaults")) {
                TerrainGenerationSystem::resetConfig();
                worldGenConfig = TerrainGenerationSystem::getConfig();
                worldGenDirty = false;
            }
            if (ImGui::Button("Preset: Dramatic Alpine")) {
                applyAlpinePreset(worldGenConfig);
                worldGenDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Preset: Broad Highlands")) {
                applyHighlandsPreset(worldGenConfig);
                worldGenDirty = true;
            }

            if (ImGui::CollapsingHeader("Biome Classification", ImGuiTreeNodeFlags_DefaultOpen)) {
                worldGenDirty |= sliderDouble("Temp Scale", worldGenConfig.tempScale, 0.0001f, 0.01f);
                worldGenDirty |= sliderDouble("Moisture Scale", worldGenConfig.moistureScale, 0.0001f, 0.01f);
                worldGenDirty |= sliderDouble("Continentalness Scale", worldGenConfig.continentalnessScale, 0.0001f, 0.01f);
                worldGenDirty |= sliderDouble("Ocean Threshold", worldGenConfig.oceanThreshold, 0.0f, 1.0f);
                worldGenDirty |= sliderDouble("Beach Threshold", worldGenConfig.beachThreshold, 0.0f, 1.0f);
                worldGenDirty |= sliderDouble("Biome Noise Contrast", worldGenConfig.biomeNoiseContrast, 0.1f, 2.0f);
                worldGenDirty |= sliderDouble("Desert Temp Threshold", worldGenConfig.desertTemperatureThreshold, 0.0f, 1.0f);
                worldGenDirty |= sliderDouble("Desert Moisture Threshold", worldGenConfig.desertMoistureThreshold, 0.0f, 1.0f);
                worldGenDirty |= sliderDouble("Mountain Temp Threshold", worldGenConfig.mountainTemperatureThreshold, 0.0f, 1.0f);
                worldGenDirty |= sliderDouble("Mountain Moisture Threshold", worldGenConfig.mountainMoistureThreshold, 0.0f, 1.0f);
                worldGenDirty |= sliderDouble("Swamp Moisture Threshold", worldGenConfig.swampMoistureThreshold, 0.0f, 1.0f);
                worldGenDirty |= sliderDouble("Forest Moisture Threshold", worldGenConfig.forestMoistureThreshold, 0.0f, 1.0f);
            }

            if (ImGui::CollapsingHeader("Biome Profiles", ImGuiTreeNodeFlags_DefaultOpen)) {
                worldGenDirty |= editBiomeProfile("Ocean", worldGenConfig.ocean);
                worldGenDirty |= editBiomeProfile("Beach", worldGenConfig.beach);
                worldGenDirty |= editBiomeProfile("Desert", worldGenConfig.desert);
                worldGenDirty |= editBiomeProfile("Savanna", worldGenConfig.savanna);
                worldGenDirty |= editBiomeProfile("Plains", worldGenConfig.plains);
                worldGenDirty |= editBiomeProfile("Forest", worldGenConfig.forest);
                worldGenDirty |= editBiomeProfile("Swamp", worldGenConfig.swamp);
                worldGenDirty |= editBiomeProfile("Taiga", worldGenConfig.taiga);
                worldGenDirty |= editBiomeProfile("Mountains", worldGenConfig.mountains);
                worldGenDirty |= editBiomeProfile("Tundra", worldGenConfig.tundra);
            }

            if (ImGui::CollapsingHeader("Terrain Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
                worldGenDirty |= sliderDouble("Terrain Warp Scale", worldGenConfig.terrainWarpScale, 0.0001f, 0.05f);
                worldGenDirty |= sliderDouble("Terrain Warp Strength", worldGenConfig.terrainWarpStrength, 0.0f, 128.0f);
                worldGenDirty |= sliderDouble("Macro Scale", worldGenConfig.macroScale, 0.0001f, 0.01f);
                worldGenDirty |= sliderDouble("Mountain Scale", worldGenConfig.mountainScale, 0.0001f, 0.02f);
                worldGenDirty |= sliderDouble("Detail Scale", worldGenConfig.detailScale, 0.0001f, 0.05f);
                worldGenDirty |= sliderDouble("Macro Weight", worldGenConfig.macroWeight, 0.0f, 2.0f);
                worldGenDirty |= sliderDouble("Ridge Weight", worldGenConfig.ridgeWeight, 0.0f, 2.0f);
                worldGenDirty |= sliderDouble("Detail Weight", worldGenConfig.detailWeight, 0.0f, 2.0f);
                worldGenDirty |= sliderDouble("Non-Mountain Ridge Bias", worldGenConfig.nonMountainRidgeBias, 0.0f, 2.0f);
                worldGenDirty |= sliderDouble("Swamp Height Cap", worldGenConfig.swampHeightCap, -64.0f, 128.0f);

                worldGenDirty |= ImGui::SliderInt("Blend Radius", &worldGenConfig.blendRadius, 0, 128);
                for (int i = 0; i < 5; ++i) {
                    ImGui::PushID(i);
                    std::string label = std::string("Blend Weight ") + std::to_string(i);
                    worldGenDirty |= sliderDouble(label.c_str(), worldGenConfig.blendWeights[static_cast<std::size_t>(i)], 0.0f, 1.0f);
                    ImGui::PopID();
                }
            }

            if (ImGui::CollapsingHeader("Caves and Aquifers", ImGuiTreeNodeFlags_DefaultOpen)) {
                worldGenDirty |= sliderDouble("Cave Scale", worldGenConfig.caveScale, 0.0001f, 0.1f);
                worldGenDirty |= sliderDouble("Cave Warp Scale", worldGenConfig.caveWarpScale, 0.0001f, 0.05f);
                worldGenDirty |= sliderDouble("Cave Warp Strength", worldGenConfig.caveWarpStrength, 0.0f, 64.0f);
                worldGenDirty |= ImGui::SliderInt("Cave Surface Guard", &worldGenConfig.caveSurfaceGuard, 0, 64);
                worldGenDirty |= sliderDouble("Cave Y Squash", worldGenConfig.caveYSquash, 0.1f, 2.0f);
                worldGenDirty |= sliderDouble("Cave Depth Offset", worldGenConfig.caveDepthOffset, 0.0f, 64.0f);
                worldGenDirty |= sliderDouble("Cave Depth Range", worldGenConfig.caveDepthRange, 1.0f, 256.0f);
                worldGenDirty |= sliderDouble("Cave Threshold Base", worldGenConfig.caveThresholdBase, 0.0f, 0.25f);
                worldGenDirty |= sliderDouble("Cave Threshold Depth Gain", worldGenConfig.caveThresholdDepthGain, 0.0f, 0.25f);

                worldGenDirty |= sliderDouble("Aquifer Scale", worldGenConfig.aquiferScale, 0.0001f, 0.05f);
                worldGenDirty |= sliderDouble("Aquifer Y Scale Mul", worldGenConfig.aquiferYScaleMul, 0.1f, 4.0f);
                worldGenDirty |= sliderDouble("Aquifer Threshold", worldGenConfig.aquiferThreshold, -1.0f, 1.0f);
                worldGenDirty |= ImGui::SliderInt("Aquifer Depth Y Offset", &worldGenConfig.aquiferDepthYOffset, 0, 32);
            }

            if (ImGui::CollapsingHeader("Layering and Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
                worldGenDirty |= ImGui::SliderInt("Bedrock Y", &worldGenConfig.bedrockY, -256, 0);
                worldGenDirty |= ImGui::SliderInt("Deep Stone Cutoff Y", &worldGenConfig.deepStoneCutoffY, -128, 128);
                worldGenDirty |= ImGui::SliderInt("Water Level Y", &worldGenConfig.waterLevelY, -128, 256);
                worldGenDirty |= sliderDouble("Top Surface Density Threshold", worldGenConfig.topSurfaceDensityThreshold, 0.0f, 12.0f);
                worldGenDirty |= ImGui::SliderInt("Snow Start Y", &worldGenConfig.snowStartY, -64, 256);
                worldGenDirty |= ImGui::SliderInt("Mountain Stone Start Y", &worldGenConfig.mountainStoneStartY, -64, 256);

                worldGenDirty |= sliderDouble("Overhang Band Half Width", worldGenConfig.overhangBandHalfWidth, 0.0f, 128.0f);
                worldGenDirty |= sliderDouble("Overhang Scale", worldGenConfig.overhangScale, 0.0001f, 0.05f);
                worldGenDirty |= sliderDouble("Overhang Strength", worldGenConfig.overhangStrength, 0.0f, 64.0f);
            }

            if (ImGui::CollapsingHeader("Ore Veins", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (std::size_t i = 0; i < worldGenConfig.ores.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    std::string oreLabel = std::string("Ore Slot ") + std::to_string(i);
                    if (ImGui::TreeNode(oreLabel.c_str())) {
                        auto& ore = worldGenConfig.ores[i];
                        worldGenDirty |= ImGui::Checkbox("Enabled", &ore.enabled);

                        int oreId = static_cast<int>(ore.id);
                        worldGenDirty |= ImGui::SliderInt("Block ID", &oreId, 0, 255);
                        ore.id = static_cast<std::uint8_t>(oreId);

                        worldGenDirty |= sliderDouble("Noise Scale", ore.scale, 0.0001f, 0.5f);
                        worldGenDirty |= sliderDouble("Threshold", ore.threshold, 0.0f, 1.0f);
                        worldGenDirty |= ImGui::SliderInt("Min Y", &ore.minY, -256, 256);
                        worldGenDirty |= ImGui::SliderInt("Max Y", &ore.maxY, -256, 256);

                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }

            if (worldGenDirty) {
                TerrainGenerationSystem::setConfig(worldGenConfig);
            }

            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glEndQuery(GL_TIME_ELAPSED);
        gpuQueryIssued[gpuQueryIndex] = true;
        gpuQueryIndex = (gpuQueryIndex + 1) % 2;

        glfwSwapBuffers(window);
        glfwPollEvents();

        const auto cpuFrameEnd = std::chrono::steady_clock::now();
        cpuFrameTimeMs = static_cast<float>(std::chrono::duration<double, std::milli>(cpuFrameEnd - cpuFrameStart).count());
    }

    glDeleteQueries(2, gpuTimeQueries);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
}