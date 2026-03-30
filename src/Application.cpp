#include "Application.hpp"
#ifndef __EMSCRIPTEN__
#include "src/ui/ImGuiManager.hpp"
#include <imgui.h>
#endif
#include "src/rendering/InstancedRenderData.hpp"

#include <iostream>
#include <stdexcept>
#include <functional>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

// Create or update a per-building HTML label (position: fixed over the canvas)
EM_JS(void, js_set_building_label, (int id, const char* text, float x, float y, int r, int g, int b), {
    var key = 'fce_lbl_' + id;
    var el = document.getElementById(key);
    if (!el) {
        el = document.createElement('div');
        el.id = key;
        el.style.cssText =
            'position:fixed;pointer-events:none;' +
            'font:bold 11px monospace;' +
            'text-shadow:1px 1px 2px #000,0 0 4px #000;' +
            'z-index:100;transform:translateX(-50%);white-space:nowrap';
        document.body.appendChild(el);
    }
    el.style.left  = x + 'px';
    el.style.top   = y + 'px';
    el.style.color = 'rgb(' + r + ',' + g + ',' + b + ')';
    el.textContent = UTF8ToString(text);
});

// Show LIVE / MOCK badge in top-right corner
EM_JS(void, js_set_data_source_badge, (int is_mock), {
    var el = document.getElementById('fce_datasrc');
    if (!el) {
        el = document.createElement('div');
        el.id = 'fce_datasrc';
        el.style.cssText =
            'position:fixed;top:10px;right:10px;' +
            'padding:3px 10px;border-radius:4px;' +
            'font:bold 13px monospace;z-index:100';
        document.body.appendChild(el);
    }
    if (is_mock) {
        el.textContent    = '\u25cf MOCK';
        el.style.background = 'rgba(200,120,0,0.85)';
        el.style.color      = '#fff';
    } else {
        el.textContent    = '\u25cf LIVE';
        el.style.background = 'rgba(30,160,60,0.9)';
        el.style.color      = '#fff';
    }
});
#endif

Application::Application() {
    initWindow();
    initRenderer();
    initGameLogic();
}

Application::~Application() {
    // RAII cleanup: Members destroyed in reverse declaration order
    // 1. ~Renderer() - cleans up ImGui (if initialized), calls waitIdle(), cleans up Vulkan resources
    // 2. ~Camera() - no special cleanup needed

    // Manual cleanup for raw pointers only
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

void Application::run() {
    // Initialize frame timing
    lastFrameTime = std::chrono::high_resolution_clock::now();

#ifdef __EMSCRIPTEN__
    // WebGPU: Use emscripten_set_main_loop for browser's requestAnimationFrame
    emscripten_set_main_loop_arg(
        [](void* arg) {
            auto* app = static_cast<Application*>(arg);
            app->mainLoopFrame();
        },
        this,
        0,  // Use browser's requestAnimationFrame (typically 60 FPS)
        1   // Simulate infinite loop
    );
#else
    // Native: Traditional game loop
    mainLoop();
#endif
}

void Application::initWindow() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

#ifdef __EMSCRIPTEN__
    // Get actual browser viewport size at startup
    int winW = EM_ASM_INT({ return window.innerWidth; });
    int winH = EM_ASM_INT({ return window.innerHeight; });
    if (winW <= 0) winW = WINDOW_WIDTH;
    if (winH <= 0) winH = WINDOW_HEIGHT;

    window = glfwCreateWindow(winW, winH, WINDOW_TITLE, nullptr, nullptr);
#else
    // Use monitor resolution for near-fullscreen window
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    int winW = mode ? mode->width  : WINDOW_WIDTH;
    int winH = mode ? mode->height : WINDOW_HEIGHT;

    window = glfwCreateWindow(winW, winH, WINDOW_TITLE, nullptr, nullptr);
    glfwMaximizeWindow(window);
#endif

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);

#ifdef __EMSCRIPTEN__
    // Defer resize: store dimensions and apply at the start of the next frame.
    // glfwSetWindowSize must NOT be called here because it synchronously triggers
    // framebufferResizeCallback → recreateSwapchain(), which is unsafe from a JS
    // event handler context (outside the Asyncify main-loop coroutine).
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false,
        [](int, const EmscriptenUiEvent* e, void* userData) -> EM_BOOL {
            auto* app = static_cast<Application*>(userData);
            app->m_pendingResize = true;
            app->m_pendingWidth  = e->windowInnerWidth;
            app->m_pendingHeight = e->windowInnerHeight;
            return EM_TRUE;
        });
#endif
}

void Application::initRenderer() {
    // Create camera using actual framebuffer size (not hardcoded constants)
    int fbW = WINDOW_WIDTH, fbH = WINDOW_HEIGHT;
    if (window) {
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW <= 0 || fbH <= 0) { fbW = WINDOW_WIDTH; fbH = WINDOW_HEIGHT; }
    }
    float aspectRatio = static_cast<float>(fbW) / static_cast<float>(fbH);
    camera = std::make_unique<Camera>(aspectRatio);

    // Create renderer
    renderer = std::make_unique<Renderer>(window, validationLayers, enableValidationLayers);

    // Initialize ImGui
    renderer->initImGui(window);
}

void Application::initGameLogic() {
    // Get RHI device and queue from renderer
    auto* rhiDevice = renderer->getRHIDevice();
    auto* rhiQueue = renderer->getGraphicsQueue();

    // Create WorldManager
    worldManager = std::make_unique<WorldManager>(rhiDevice, rhiQueue);
    worldManager->initialize();

    // Initialize Particle System
    particleSystem = std::make_unique<effects::ParticleSystem>(rhiDevice, rhiQueue);

    // 16 NASDAQ stocks — ticker, approximate base price (USD)
    struct StockDef { const char* ticker; float basePrice; };
    static constexpr StockDef stocks[16] = {
        {"NVDA",  186.0f}, {"AAPL",  224.0f}, {"TSLA", 248.0f}, {"MSFT", 420.0f},
        {"GOOGL", 175.0f}, {"AMZN",  215.0f}, {"META", 590.0f}, {"NFLX", 487.0f},
        {"AMD",   125.0f}, {"QCOM",  155.0f}, {"AVGO", 185.0f}, {"MU",   105.0f},
        {"PYPL",   75.0f}, {"INTC",   25.0f}, {"TXN",  185.0f}, {"ASML", 350.0f},
    };

    // 4x4 grid centered on NASDAQ sector (400, 0, 400), 90m spacing
    auto* buildingManager = worldManager->getBuildingManager();
    if (buildingManager) {
        for (int i = 0; i < 16; i++) {
            int row = i / 4, col = i % 4;
            float px = 400.0f + (col - 1.5f) * 90.0f;
            float pz = 400.0f + (row - 1.5f) * 90.0f;
            buildingManager->createBuilding(stocks[i].ticker, "NASDAQ",
                                            glm::vec3(px, 0.0f, pz), stocks[i].basePrice);
        }
    }

    // WebSocket feed — primary data source (native + WASM)
    wsDataFeed = std::make_unique<WebSocketDataFeed>();
    wsDataFeed->connect(WS_URL);

    // Mock fallback (used when WS is not connected)
    mockDataGen = std::make_unique<MockDataGenerator>();
    for (const auto& s : stocks) {
        mockDataGen->registerTicker(s.ticker, s.basePrice);
    }
    mockDataGen->setVolatility(0.08f);
}

void Application::mainLoop() {
    // Native: Traditional game loop
    while (!glfwWindowShouldClose(window)) {
        mainLoopFrame();
    }
    renderer->waitIdle();
}

void Application::mainLoopFrame() {
#ifdef __EMSCRIPTEN__
    // Check for window close (ESC key or close button)
    if (glfwWindowShouldClose(window)) {
        emscripten_cancel_main_loop();
        return;
    }

    // Apply deferred resize with explicit dimensions, bypassing the GLFW callback chain.
    // glfwSetWindowSize is called first so GLFW's internal state and the canvas buffer
    // size are both updated, then handleFramebufferResize(w,h) recreates the swapchain/
    // depth/pipeline directly without re-querying GLFW (which may still return stale data).
    if (m_pendingResize && m_pendingWidth > 0 && m_pendingHeight > 0) {
        // Update GLFW internal state + Emscripten canvas buffer size.
        // framebufferResizeCallback is a no-op on WASM so this won't double-recreate.
        glfwSetWindowSize(window, m_pendingWidth, m_pendingHeight);
        // Explicitly recreate swapchain / depth / pipeline with the new dimensions.
        renderer->handleFramebufferResize(m_pendingWidth, m_pendingHeight);
        // Update camera projection matrix for new aspect ratio.
        camera->setAspectRatio(static_cast<float>(m_pendingWidth) /
                               static_cast<float>(m_pendingHeight));
        m_pendingResize = false;
    }
#endif

    // Calculate delta time
    auto currentFrameTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentFrameTime - lastFrameTime).count();
    lastFrameTime = currentFrameTime;

    glfwPollEvents();
    processInput();
    renderer->updateCamera(camera->getViewMatrix(), camera->getProjectionMatrix(), camera->getPosition());

    // Track whether this frame's price data came from a live WS feed.
    // Suppresses mock fallback whenever a WS connection is open — even frames
    // with no new batch (most frames) should not trigger mock overwrite.
    bool wsProvided = false;

        // Update game world
        if (worldManager) {
            // Update price data: WS if connected, mock fallback otherwise
            if (wsDataFeed && wsDataFeed->isConnected()) {
                wsProvided = true;  // Connected = suppress mock, even if no batch this frame
                auto updates = wsDataFeed->drainUpdates();
                if (!updates.empty()) {
                    worldManager->updateMarketData(updates);
                }
            }
            if (!wsProvided && mockDataGen) {
                priceUpdateTimer += deltaTime;
                if (priceUpdateTimer >= priceUpdateInterval) {
                    priceUpdateTimer = 0.0f;
                    worldManager->updateMarketData(mockDataGen->generateUpdates());
                }
            }

            // Update animations
            worldManager->update(deltaTime);
        }

        // Extract rendering data from game logic
        if (worldManager) {
            auto* buildingManager = worldManager->getBuildingManager();
            if (buildingManager) {
                if (buildingManager->isObjectBufferDirty()) {
                    buildingManager->updateObjectBuffer();
                }

                rendering::InstancedRenderData renderData;
                renderData.mesh = buildingManager->getBuildingMesh();
                renderData.objectBuffer = buildingManager->getObjectBuffer();
                renderData.instanceCount = static_cast<uint32_t>(buildingManager->getBuildingCount() + 1);
                renderer->submitInstancedRenderData(renderData);
            }
        }

        // Update Particle System
        if (particleSystem) {
            particleSystem->update(deltaTime);
            // Submit particle system to renderer
            renderer->submitParticleSystem(particleSystem.get());
        }

        // Render ImGui UI
#ifndef __EMSCRIPTEN__
        if (auto* imgui = renderer->getImGuiManager()) {
            imgui->newFrame();

            uint32_t buildingCount = 0;
            if (worldManager && worldManager->getBuildingManager()) {
                buildingCount = static_cast<uint32_t>(worldManager->getBuildingManager()->getBuildingCount());
            }
            imgui->renderUI(*camera, buildingCount, particleSystem.get());

            // Handle particle effect requests from UI
            auto particleRequest = imgui->getAndClearParticleRequest();
            if (particleRequest.requested && particleSystem) {
                particleSystem->spawnEffect(
                    particleRequest.type,
                    particleRequest.position,
                    particleRequest.duration
                );
            }

            // Render ticker labels above each building
            if (worldManager && worldManager->getBuildingManager()) {
                int fbW, fbH;
                glfwGetFramebufferSize(window, &fbW, &fbH);

                glm::mat4 vp = camera->getProjectionMatrix() * camera->getViewMatrix();
                ImDrawList* dl = ImGui::GetBackgroundDrawList();

                for (auto* b : worldManager->getBuildingManager()->getAllBuildings()) {
                    // Project top-center of building to screen
                    glm::vec3 worldTop = b->position + glm::vec3(0.0f, b->currentHeight + 8.0f, 0.0f);
                    glm::vec4 clip = vp * glm::vec4(worldTop, 1.0f);

                    if (clip.w <= 0.0f) continue;  // Behind camera

                    glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f) continue;

                    float sx = (ndc.x + 1.0f) * 0.5f * (float)fbW;
                    float sy = (1.0f - ndc.y) * 0.5f * (float)fbH;

                    // Color: green=상승, red=하락, gray=보합
                    ImU32 color;
                    float rate = b->priceChangePercent;
                    if (rate > 0.1f) {
                        color = IM_COL32(80, 220, 100, 230);   // green
                    } else if (rate < -0.1f) {
                        color = IM_COL32(220, 80, 80, 230);    // red
                    } else {
                        color = IM_COL32(200, 200, 200, 200);  // gray
                    }

                    // "NVDA  $186  +0.69%"
                    char label[48];
                    std::snprintf(label, sizeof(label), "%s  $%.0f  %+.2f%%",
                                  b->ticker.c_str(), b->currentPrice, rate);

                    ImVec2 textSize = ImGui::CalcTextSize(label);
                    // Subtle dark background for readability
                    dl->AddRectFilled(
                        ImVec2(sx - textSize.x * 0.5f - 3, sy - 1),
                        ImVec2(sx + textSize.x * 0.5f + 3, sy + textSize.y + 1),
                        IM_COL32(0, 0, 0, 110), 2.0f);
                    dl->AddText(ImVec2(sx - textSize.x * 0.5f, sy), color, label);
                }
            }

            // LIVE / MOCK badge — top-right corner
            {
                const char* srcText = wsProvided ? "\xe2\x97\x8f LIVE" : "\xe2\x97\x8f MOCK";
                ImU32 srcBg   = wsProvided ? IM_COL32(30, 160, 60, 210)  : IM_COL32(200, 120, 0, 210);
                ImVec2 ts     = ImGui::CalcTextSize(srcText);
                ImGuiIO& io   = ImGui::GetIO();
                float px = io.DisplaySize.x - ts.x - 20.0f;
                float py = 10.0f;
                dl->AddRectFilled(ImVec2(px - 6, py - 3),
                                  ImVec2(px + ts.x + 6, py + ts.y + 3),
                                  srcBg, 5.0f);
                dl->AddText(ImVec2(px, py), IM_COL32(255, 255, 255, 255), srcText);
            }

            // Apply lighting settings from UI
            auto& lighting = imgui->getLightingSettings();
            renderer->setSunDirection(lighting.sunDirection);
            renderer->setSunIntensity(lighting.sunIntensity);
            renderer->setSunColor(lighting.sunColor);
            renderer->setAmbientIntensity(lighting.ambientIntensity);
            renderer->setShadowBias(lighting.shadowBias);
            renderer->setShadowStrength(lighting.shadowStrength);
            renderer->setExposure(lighting.exposure);
        }
#endif

#ifdef __EMSCRIPTEN__
    // Update HTML building labels and data-source badge each frame
    if (worldManager && worldManager->getBuildingManager()) {
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glm::mat4 vp = camera->getProjectionMatrix() * camera->getViewMatrix();

        for (auto* b : worldManager->getBuildingManager()->getAllBuildings()) {
            // Project the point 8 m above the building top
            glm::vec3 worldTop = b->position + glm::vec3(0.0f, b->currentHeight + 8.0f, 0.0f);
            glm::vec4 clip = vp * glm::vec4(worldTop, 1.0f);
            if (clip.w <= 0.0f) continue;

            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f) continue;

            float sx = (ndc.x + 1.0f) * 0.5f * static_cast<float>(fbW);
            float sy = (1.0f - ndc.y) * 0.5f * static_cast<float>(fbH);

            float rate = b->priceChangePercent;
            int cr, cg, cb;
            if      (rate >  0.1f) { cr = 80;  cg = 220; cb = 100; }
            else if (rate < -0.1f) { cr = 220; cg = 80;  cb = 80;  }
            else                   { cr = 200; cg = 200; cb = 200; }

            char label[48];
            std::snprintf(label, sizeof(label), "%s  $%.0f  %+.2f%%",
                          b->ticker.c_str(), b->currentPrice, rate);

            js_set_building_label(static_cast<int>(b->entityId), label, sx, sy, cr, cg, cb);
        }

        js_set_data_source_badge(wsProvided ? 0 : 1);
    }
#endif

    // Renderer handles both scene and ImGui rendering
    renderer->drawFrame();
}

void Application::processInput() {
    // ESC to close
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    // WASD: ground-plane pan, Q/E: vertical elevation
    float moveSpeed = camera->getDistance() * 0.005f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        camera->pan(0.0f, moveSpeed);
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        camera->pan(0.0f, -moveSpeed);
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        camera->pan(-moveSpeed, 0.0f);
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        camera->pan(moveSpeed, 0.0f);
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        camera->elevate(-moveSpeed);
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        camera->elevate(moveSpeed);
    }

}

void Application::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
#ifdef __EMSCRIPTEN__
    // On WASM, resize is handled explicitly in mainLoopFrame() via the deferred resize
    // mechanism using handleFramebufferResize(w, h). Doing it here is unsafe because:
    // 1. This callback can fire from a JS event handler (outside the Asyncify coroutine).
    // 2. Even when fired from glfwSetWindowSize() inside mainLoopFrame(), the explicit
    //    call that follows already handles it — processing it twice wastes resources.
    (void)window; (void)width; (void)height;
#else
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
    app->renderer->handleFramebufferResize();

    // Update camera aspect ratio
    if (height > 0) {
        float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        app->camera->setAspectRatio(aspectRatio);
    }
#endif
}

void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            app->mousePressed = true;
            glfwGetCursorPos(window, &app->lastMouseX, &app->lastMouseY);
        } else if (action == GLFW_RELEASE) {
            app->mousePressed = false;
            app->firstMouse = true;
        }
    }
}

void Application::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (!app->mousePressed) {
        return;
    }

    if (app->firstMouse) {
        app->lastMouseX = xpos;
        app->lastMouseY = ypos;
        app->firstMouse = false;
        return;
    }

    float deltaX = static_cast<float>(xpos - app->lastMouseX);
    float deltaY = static_cast<float>(ypos - app->lastMouseY);

    app->camera->rotate(deltaX, deltaY);

    app->lastMouseX = xpos;
    app->lastMouseY = ypos;
}

void Application::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
    app->camera->zoom(static_cast<float>(yoffset));
}

void Application::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_R:
                app->camera->reset();
                break;
        }
    }
}
