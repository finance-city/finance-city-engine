#pragma once

#include "src/rendering/Renderer.hpp"
#include "src/scene/Camera.hpp"
#include "src/game/managers/WorldManager.hpp"
#include "src/game/sync/MockDataGenerator.hpp"
#include "src/game/sync/WebSocketDataFeed.hpp"
#include "src/effects/ParticleSystem.hpp"

#include <GLFW/glfw3.h>
#include <memory>
#include <vector>
#include <string>
#include <chrono>

/**
 * @brief Top-level application class managing window and main loop
 *
 * Responsibilities:
 * - GLFW window creation and management
 * - Main event loop
 * - Renderer lifecycle management
 * - UI management (ImGui)
 * - Window resize callbacks
 */
class Application {
public:
    /**
     * @brief Construct application with default window size and validation settings
     */
    Application();

    /**
     * @brief Destructor - ensures proper cleanup order
     */
    ~Application();

    // Disable copy and move
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    /**
     * @brief Run the application (initialize, loop, cleanup)
     */
    void run();

private:
    // Window configuration
    static constexpr uint32_t WINDOW_WIDTH = 1600;
    static constexpr uint32_t WINDOW_HEIGHT = 900;
    static constexpr const char* WINDOW_TITLE = "Finance City Engine";

    // Validation layers (enabled in debug builds only)
    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

#ifdef NDEBUG
    static constexpr bool enableValidationLayers = false;
#else
    static constexpr bool enableValidationLayers = true;
#endif

    // Members (destruction order matters - reverse of declaration order)
    GLFWwindow* window = nullptr;
    std::unique_ptr<Camera> camera;              // Destroyed first (no dependencies)
    std::unique_ptr<Renderer> renderer;          // Destroyed second (now owns ImGuiManager)

    // WebSocket backend URL — change to match your server
    static constexpr const char* WS_URL = "ws://localhost:8081";

    // Game Logic Layer
    std::unique_ptr<WorldManager> worldManager;
    std::unique_ptr<WebSocketDataFeed> wsDataFeed;
    std::unique_ptr<MockDataGenerator> mockDataGen;  // Fallback when WS not connected
    float priceUpdateTimer = 0.0f;
    float priceUpdateInterval = 1.0f;            // Mock fallback update interval

    // Particle System
    std::unique_ptr<effects::ParticleSystem> particleSystem;

    // Mouse state
    bool firstMouse = true;
    bool mousePressed = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;

    // Frame timing for main loop
    std::chrono::high_resolution_clock::time_point lastFrameTime;

#ifdef __EMSCRIPTEN__
    // Deferred resize: set by the JS resize callback, applied at the start of the next frame
    bool m_pendingResize = false;
    int m_pendingWidth = 0;
    int m_pendingHeight = 0;
#endif

    // Initialization
    void initWindow();
    void initRenderer();
    void initGameLogic();

    // Main loop
    void mainLoop();
    void mainLoopFrame();  // Single frame update for Emscripten compatibility

    // Input handling
    void processInput();

    // Callbacks
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
};
