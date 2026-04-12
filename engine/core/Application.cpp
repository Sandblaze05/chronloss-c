#include "Application.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
#include "engine/render/Renderer.h"
#include "engine/client/Player.h"
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

    Player player(8.0f, 1.0f, 8.0f);
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

    while (!glfwWindowShouldClose(window)) {
        // Calculate Delta Time
        float currentFrame = glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Update Game Logic using live camera-relative movement axes.
        float camForwardX = 0.0f, camForwardZ = -1.0f;
        float camRightX = 1.0f, camRightZ = 0.0f;
        renderer.getGroundAxes(camForwardX, camForwardZ, camRightX, camRightZ);
        player.update(window, deltaTime, camForwardX, camForwardZ, camRightX, camRightZ, renderer.getStreamer());

        // Render the Game (Pass the player's coordinates, camera offsets, and deltaTime)
        renderer.beginFrame(player.getX(), player.getY(), player.getZ(),
                camOffsetX, camOffsetY, camOffsetZ,
                deltaTime);
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Engine Debug");
        ImGui::Text("FPS: %.1f", io.Framerate);
        ImGui::Separator();
        ImGui::Text("Player Position:");
        ImGui::Text("X: %.2f", player.getX());
        ImGui::Text("Y: %.2f", player.getY());
        ImGui::Text("Z: %.2f", player.getZ());
        ImGui::Separator();
        ImGui::Text("World State:");
        ImGui::Text("Loaded Chunks: %zu", renderer.getLoadedChunkCount());
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
}