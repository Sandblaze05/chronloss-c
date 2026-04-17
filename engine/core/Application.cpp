#include "Application.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
#include <array>
#include <cmath>
#include <chrono>
#include "engine/render/Renderer.h"
#include "engine/client/Player.h"
#include "engine/Physics/PhysicsSystem.h"
#include "engine/world/Block.h"

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
    constexpr int kSpawnSearchUp = 64;
    constexpr float kBreakRepeatSeconds = 0.10f;
    constexpr float kPlaceRepeatSeconds = 0.12f;
    PhysicsSystem physics;
    std::array<PhysicsBody*, 1> physicsBodies = { &player.body };
    bool spawnPrechecked = false;
    float breakRepeatTimer = 0.0f;
    float placeRepeatTimer = 0.0f;

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

    auto isBodyAreaReadyAtFeetY = [&](int feetY) -> bool {
        const float minX = player.body.pos[0] - player.body.width * 0.5f;
        const float maxX = player.body.pos[0] + player.body.width * 0.5f;
        const float minZ = player.body.pos[2] - player.body.width * 0.5f;
        const float maxZ = player.body.pos[2] + player.body.width * 0.5f;

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

    auto isBodyOverlappingSolidAtFeetY = [&](int feetY) -> bool {
        const float minX = player.body.pos[0] - player.body.width * 0.5f;
        const float maxX = player.body.pos[0] + player.body.width * 0.5f;
        const float minZ = player.body.pos[2] - player.body.width * 0.5f;
        const float maxZ = player.body.pos[2] + player.body.width * 0.5f;

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
            const int baseFeetY = static_cast<int>(std::floor(player.body.pos[1]));
            for (int dy = 0; dy <= kSpawnSearchUp; ++dy) {
                const int candidateFeetY = baseFeetY + dy;
                if (!isBodyAreaReadyAtFeetY(candidateFeetY)) {
                    break;
                }
                if (!isBodyOverlappingSolidAtFeetY(candidateFeetY)) {
                    player.body.pos[1] = static_cast<float>(candidateFeetY);
                    player.body.vel[1] = 0.0f;
                    spawnPrechecked = true;
                    break;
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
        breakRepeatTimer -= deltaTime;
        placeRepeatTimer -= deltaTime;

        if (!ImGui::GetIO().WantCaptureMouse && !renderer.isOrbiting()) {
            ChunkStreamer& streamer = renderer.getStreamer();

            if (isLeftMousePressed && breakRepeatTimer <= 0.0f && renderer.hasHoveredBlock()) {
                int hx = 0, hy = 0, hz = 0;
                renderer.getHoveredBlock(hx, hy, hz);
                streamer.setBlockAtWorld(hx, hy, hz, 0);
                breakRepeatTimer = kBreakRepeatSeconds;
            }

            if (isRightMousePressed && placeRepeatTimer <= 0.0f && renderer.hasPlacementBlock()) {
                int pxPlace = 0, pyPlace = 0, pzPlace = 0;
                renderer.getPlacementBlock(pxPlace, pyPlace, pzPlace);

                const std::uint8_t existingId = streamer.getBlockAtWorld(pxPlace, pyPlace, pzPlace);
                const bool isTargetSolid = BlockRegistry::get(existingId).isSolid();

                const float blockMinX = static_cast<float>(pxPlace);
                const float blockMaxX = blockMinX + 1.0f;
                const float blockMinY = static_cast<float>(pyPlace);
                const float blockMaxY = blockMinY + 1.0f;
                const float blockMinZ = static_cast<float>(pzPlace);
                const float blockMaxZ = blockMinZ + 1.0f;

                const float playerMinX = player.body.pos[0] - (player.body.width * 0.5f);
                const float playerMaxX = player.body.pos[0] + (player.body.width * 0.5f);
                const float playerMinY = player.body.pos[1];
                const float playerMaxY = player.body.pos[1] + player.body.height;
                const float playerMinZ = player.body.pos[2] - (player.body.width * 0.5f);
                const float playerMaxZ = player.body.pos[2] + (player.body.width * 0.5f);

                const bool overlapsPlayer =
                    (blockMinX < playerMaxX && blockMaxX > playerMinX) &&
                    (blockMinY < playerMaxY && blockMaxY > playerMinY) &&
                    (blockMinZ < playerMaxZ && blockMaxZ > playerMinZ);

                if (!isTargetSolid && !overlapsPlayer) {
                    streamer.setBlockAtWorld(pxPlace, pyPlace, pzPlace, 1);
                    placeRepeatTimer = kPlaceRepeatSeconds;
                }
            }
        }

        if (!isLeftMousePressed) {
            breakRepeatTimer = 0.0f;
        }
        if (!isRightMousePressed) {
            placeRepeatTimer = 0.0f;
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
        ImGui::End();

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