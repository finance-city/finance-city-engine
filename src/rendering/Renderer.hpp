#pragma once

#include "src/resources/ResourceManager.hpp"
#include "src/scene/SceneManager.hpp"
#include "src/utils/Vertex.hpp"
#include "src/rendering/RendererBridge.hpp"
#include "src/rendering/InstancedRenderData.hpp"
#include "src/effects/ParticleRenderer.hpp"
#include "src/rendering/SkyboxRenderer.hpp"
#include "src/rendering/ShadowRenderer.hpp"
#include "src/rendering/IBLManager.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <optional>

/**
 * @brief High-level renderer coordinating subsystems
 *
 * Responsibilities:
 * - Coordinate rendering components (swapchain, pipeline, command, sync)
 * - Manage ResourceManager, SceneManager, IBL, shadow, skybox, particles
 * - Uniform buffer management
 * - Frame rendering orchestration
 */
class Renderer {
public:
    /**
     * @brief Construct renderer with window
     * @param window GLFW window for surface creation
     * @param enableValidation Whether to enable validation layers
     */
    Renderer(GLFWwindow* window, bool enableValidation);

    ~Renderer();

    // Disable copy and move
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    /**
     * @brief Draw a single frame
     */
    void drawFrame();

    /**
     * @brief Wait for device to be idle (for cleanup)
     */
    void waitIdle();

    /**
     * @brief Handle framebuffer resize (reads new size from GLFW)
     */
    void handleFramebufferResize();

    /**
     * @brief Handle framebuffer resize with explicit dimensions (bypasses GLFW query).
     * Used on WASM/Emscripten where glfwGetFramebufferSize may lag behind the actual
     * browser viewport size change.
     */
    void handleFramebufferResize(int width, int height);

    /**
     * @brief Update camera matrices and position
     * @param view View matrix
     * @param projection Projection matrix
     * @param position Camera world position (for specular lighting)
     */
    void updateCamera(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& position);


    /**
     * @brief Get mesh bounding box center (for camera targeting)
     * @return Center of primary mesh's bounding box
     */
    glm::vec3 getMeshCenter() const;

    /**
     * @brief Get mesh bounding box radius (for camera far plane calculation)
     * @return Radius of primary mesh's bounding sphere
     */
    float getMeshRadius() const;

    /**
     * @brief Get RHI device (for external components like ImGui)
     */
    rhi::RHIDevice* getRHIDevice() { return rhiBridge ? rhiBridge->getDevice() : nullptr; }

    /**
     * @brief Get RHI swapchain (for external components like ImGui)
     */
    rhi::RHISwapchain* getRHISwapchain() { return rhiBridge ? rhiBridge->getSwapchain() : nullptr; }

    /**
     * @brief Get RHI graphics queue (for external components like game logic)
     */
    rhi::RHIQueue* getGraphicsQueue() { return rhiBridge ? rhiBridge->getGraphicsQueue() : nullptr; }

#ifndef __EMSCRIPTEN__
    /**
     * @brief Get ImGui manager (for external UI updates)
     */
    class ImGuiManager* getImGuiManager() { return imguiManager.get(); }
#endif

    /**
     * @brief Initialize ImGui subsystem (no-op on WASM)
     */
    void initImGui(GLFWwindow* window);

    /**
     * @brief Submit instanced rendering data for this frame
     * @param data Rendering data (mesh, instance buffer, count)
     *
     * This is a clean interface - Renderer doesn't know about game entities.
     * Application layer extracts rendering data from game logic and passes it here.
     */
    void submitInstancedRenderData(const rendering::InstancedRenderData& data);

    /**
     * @brief Submit particle system for rendering this frame
     * @param particleSystem Particle system to render
     */
    void submitParticleSystem(effects::ParticleSystem* particleSystem);

    // Lighting configuration
    void setSunDirection(const glm::vec3& dir) { sunDirection = glm::normalize(dir); }
    glm::vec3 getSunDirection() const { return sunDirection; }
    void setSunIntensity(float intensity) { sunIntensity = intensity; }
    float getSunIntensity() const { return sunIntensity; }
    void setSunColor(const glm::vec3& color) { sunColor = color; }
    glm::vec3 getSunColor() const { return sunColor; }
    void setAmbientIntensity(float intensity) { ambientIntensity = intensity; }
    float getAmbientIntensity() const { return ambientIntensity; }

    // Shadow configuration
    void setShadowBias(float bias) { shadowBias = bias; }
    float getShadowBias() const { return shadowBias; }
    void setShadowStrength(float strength) { shadowStrength = strength; }
    float getShadowStrength() const { return shadowStrength; }

    // PBR tone mapping
    void setExposure(float exp) { exposure = exp; }
    float getExposure() const { return exposure; }

    /**
     * @brief Load HDR environment map and initialize full IBL pipeline
     * @param hdrPath Path to .hdr equirectangular environment map
     * @return true if IBL was successfully initialized
     */
    bool loadEnvironmentMap(const std::string& hdrPath);

private:
    // Window reference
    GLFWwindow* window;

    // RHI Bridge (provides RHI device access and lifecycle management)
    std::unique_ptr<rendering::RendererBridge> rhiBridge;

    // High-level managers
    std::unique_ptr<ResourceManager> resourceManager;
    std::unique_ptr<SceneManager> sceneManager;
#ifndef __EMSCRIPTEN__
    std::unique_ptr<class ImGuiManager> imguiManager;
#endif

    // RHI core resources
    std::unique_ptr<rhi::RHITexture> rhiDepthImage;
    std::unique_ptr<rhi::RHITextureView> rhiDepthImageView;
    std::vector<std::unique_ptr<rhi::RHIBuffer>> rhiUniformBuffers;
    std::unique_ptr<rhi::RHIBindGroupLayout> rhiBindGroupLayout;
    std::vector<std::unique_ptr<rhi::RHIBindGroup>> rhiBindGroups;

    // Building Instancing Pipeline
    std::unique_ptr<rhi::RHIShader> buildingVertexShader;
    std::unique_ptr<rhi::RHIShader> buildingFragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> buildingBindGroupLayout;
    std::vector<std::unique_ptr<rhi::RHIBindGroup>> buildingBindGroups;
    std::unique_ptr<rhi::RHIPipelineLayout> buildingPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> buildingPipeline;

    // Frame synchronization
    uint32_t currentFrame = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    // SSBO bind group (set 1) for per-object data
    std::unique_ptr<rhi::RHIBindGroupLayout> ssboBindGroupLayout;
    std::array<std::unique_ptr<rhi::RHIBindGroup>, MAX_FRAMES_IN_FLIGHT> ssboBindGroups;
    std::array<rhi::RHIBuffer*, MAX_FRAMES_IN_FLIGHT> cachedObjectBuffers = {};

    // GPU Frustum Culling resources
    std::unique_ptr<rhi::RHIShader> cullComputeShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> cullBindGroupLayout;
    std::unique_ptr<rhi::RHIPipelineLayout> cullPipelineLayout;
    std::unique_ptr<rhi::RHIComputePipeline> cullPipeline;
    std::array<std::unique_ptr<rhi::RHIBuffer>, MAX_FRAMES_IN_FLIGHT> cullUniformBuffers;
    std::array<std::unique_ptr<rhi::RHIBuffer>, MAX_FRAMES_IN_FLIGHT> indirectDrawBuffers;
    std::array<std::unique_ptr<rhi::RHIBuffer>, MAX_FRAMES_IN_FLIGHT> visibleIndicesBuffers;
    std::array<std::unique_ptr<rhi::RHIBindGroup>, MAX_FRAMES_IN_FLIGHT> cullBindGroups;
    static constexpr uint32_t MAX_CULL_OBJECTS = 4096;

    // Async compute
    std::unique_ptr<rhi::RHITimelineSemaphore> computeTimelineSemaphore;
    uint64_t computeTimelineValue = 0;
    bool useAsyncCompute = false;

    struct alignas(16) CullUBO {
        glm::vec4 frustumPlanes[6];
        uint32_t objectCount;
        uint32_t indexCount;
        uint32_t pad[2];
    };

    // For uniform buffer animation
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;

    // Camera matrices
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::vec3 cameraPosition = glm::vec3(0.0f);

    // Lighting parameters
    glm::vec3 sunDirection = glm::normalize(glm::vec3(1.0f, 0.6f, 1.0f));
    float sunIntensity = 1.5f;
    glm::vec3 sunColor = glm::vec3(1.0f, 0.95f, 0.9f);  // Slightly warm white sunlight
    float ambientIntensity = 0.25f;

    // Instanced rendering data (submitted per-frame) - stored by value
    std::optional<rendering::InstancedRenderData> pendingInstancedData;

    // Particle rendering
    std::unique_ptr<effects::ParticleRenderer> particleRenderer;
    effects::ParticleSystem* pendingParticleSystem = nullptr;

    // Subsystem renderers
    std::unique_ptr<rendering::SkyboxRenderer> skyboxRenderer;
    std::unique_ptr<rendering::ShadowRenderer> shadowRenderer;
    std::unique_ptr<rendering::IBLManager> iblManager;
    float shadowBias = 0.008f;
    float shadowStrength = 0.7f;
    float exposure = 1.0f;

#ifdef __EMSCRIPTEN__
    // HDR Render Target (RGBA16Float — geometry renders here)
    std::unique_ptr<rhi::RHITexture> hdrColorTexture;
    std::unique_ptr<rhi::RHITextureView> hdrColorView;
    std::unique_ptr<rhi::RHISampler> hdrSampler;

    // LDR Intermediate Target (RGBA8Unorm — tonemap writes here, FXAA reads from here)
    std::unique_ptr<rhi::RHITexture> ldrColorTexture;
    std::unique_ptr<rhi::RHITextureView> ldrColorView;

    // Tonemap Pipeline (HDR → LDR intermediate: ACES + gamma)
    std::unique_ptr<rhi::RHIShader> tonemapVertexShader;
    std::unique_ptr<rhi::RHIShader> tonemapFragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> tonemapBindGroupLayout;
    std::unique_ptr<rhi::RHIBindGroup> tonemapBindGroup;
    std::unique_ptr<rhi::RHIPipelineLayout> tonemapPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> tonemapPipeline;

    // FXAA Pipeline (LDR intermediate → swapchain: anti-aliasing)
    std::unique_ptr<rhi::RHIShader> fxaaVertexShader;
    std::unique_ptr<rhi::RHIShader> fxaaFragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> fxaaBindGroupLayout;
    std::unique_ptr<rhi::RHIBindGroup> fxaaBindGroup;
    std::unique_ptr<rhi::RHIPipelineLayout> fxaaPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> fxaaPipeline;

    // Bloom Render Targets (half-res RGBA16Float)
    // bloomA: prefilter output + vertical blur final result (read by tonemap)
    // bloomB: horizontal blur intermediate
    std::unique_ptr<rhi::RHITexture>     bloomTextureA;
    std::unique_ptr<rhi::RHITextureView> bloomViewA;
    std::unique_ptr<rhi::RHITexture>     bloomTextureB;
    std::unique_ptr<rhi::RHITextureView> bloomViewB;

    // Bloom Prefilter Pipeline (full-res HDR → half-res bloomA)
    std::unique_ptr<rhi::RHIShader>          bloomPrefilterVS;
    std::unique_ptr<rhi::RHIShader>          bloomPrefilterFS;
    std::unique_ptr<rhi::RHIBindGroupLayout> bloomBindGroupLayout;   // shared by all 3 bloom passes
    std::unique_ptr<rhi::RHIBindGroup>       bloomPrefilterBindGroup;
    std::unique_ptr<rhi::RHIPipelineLayout>  bloomPipelineLayout;    // shared by all 3 bloom passes
    std::unique_ptr<rhi::RHIRenderPipeline>  bloomPrefilterPipeline;

    // Bloom Blur Pipelines (H: bloomA→bloomB, V: bloomB→bloomA)
    std::unique_ptr<rhi::RHIShader>         bloomBlurHFS;            // entry point: fs_horizontal
    std::unique_ptr<rhi::RHIShader>         bloomBlurVFS;            // entry point: fs_vertical
    std::unique_ptr<rhi::RHIBindGroup>      bloomBlurHBindGroup;     // reads bloomViewA
    std::unique_ptr<rhi::RHIBindGroup>      bloomBlurVBindGroup;     // reads bloomViewB
    std::unique_ptr<rhi::RHIRenderPipeline> bloomBlurHPipeline;
    std::unique_ptr<rhi::RHIRenderPipeline> bloomBlurVPipeline;
#endif

    // RHI initialization methods
    void createRHIDepthResources();
    void createRHIUniformBuffers();
    void createRHIBindGroups();
    void createBuildingPipeline();
    void createParticleRenderer();
    void createSkyboxRenderer();
    void createShadowRenderer();
    void createIBL();
    void createCullingPipeline();
#ifdef __EMSCRIPTEN__
    void createHDRRenderTarget();
    void createBloomPipeline();
    void createTonemapPipeline();
    void createFXAAPipeline();
#endif
    void performFrustumCulling(rhi::RHICommandEncoder* encoder, uint32_t frameIndex,
                               uint32_t objectCount, uint32_t indexCount);
    void performFrustumCullingAsync(uint32_t frameIndex, uint32_t objectCount, uint32_t indexCount);
    void extractFrustumPlanes(const glm::mat4& vp, glm::vec4 planes[6]);

    void updateRHIUniformBuffer(uint32_t currentImage);

    // Swapchain recreation
    void recreateSwapchain();

#ifdef __EMSCRIPTEN__
    // Recreate post-process bind groups after render target resize
    void recreatePostProcessBindGroups();
#endif
};
