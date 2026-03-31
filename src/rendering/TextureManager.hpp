#pragma once

#include <rhi/RHI.hpp>
#include <memory>
#include <vector>
#include <cstdint>

namespace rendering {

/**
 * @brief Manages procedurally-generated building facade textures
 *
 * Generates 4 building albedo + 4 normal textures entirely in CPU memory
 * (no PNG files needed) and uploads them to the GPU as Texture2DArrays.
 *
 * Building types (layer index):
 *   0 = Concrete Box  — gray panels, subtle seam grid
 *   1 = Glass Tower   — dark-blue glass curtainwall, silver mullions
 *   2 = Brick Mid     — running-bond red brick, off-white mortar
 *   3 = Modern Office — horizontal glass/spandrel bands
 *
 * Bound at group 0 binding 7 (albedo), 8 (normal), 11 (aniso sampler).
 */
class TextureManager {
public:
    static constexpr uint32_t TEX_SIZE    = 256;
    static constexpr uint32_t NUM_LAYERS  = 4;

    TextureManager(rhi::RHIDevice* device, rhi::RHIQueue* queue);
    ~TextureManager() = default;

    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    /**
     * @brief Generate all textures and upload to GPU.
     * @return true on success
     */
    bool generate();

    /**
     * @brief Create minimal 1×1 fallback textures when generate() fails.
     * Ensures bindings 7/8/11 are always satisfiable in the bind group.
     */
    bool generateFallback();

    bool isReady() const { return m_ready; }

    rhi::RHITextureView* getAlbedoArrayView()  const { return m_albedoView.get(); }
    rhi::RHITextureView* getNormalArrayView()  const { return m_normalView.get(); }
    rhi::RHISampler*     getAnisoSampler()     const { return m_anisoSampler.get(); }

private:
    rhi::RHIDevice* m_device;
    rhi::RHIQueue*  m_queue;
    bool            m_ready = false;

    std::unique_ptr<rhi::RHITexture>     m_albedoArray;
    std::unique_ptr<rhi::RHITextureView> m_albedoView;
    std::unique_ptr<rhi::RHITexture>     m_normalArray;
    std::unique_ptr<rhi::RHITextureView> m_normalView;
    std::unique_ptr<rhi::RHISampler>     m_anisoSampler;

    // Per-layer pixel data generation (RGBA8, TEX_SIZE × TEX_SIZE)
    std::vector<uint8_t> genConcreteAlbedo();
    std::vector<uint8_t> genGlassTowerAlbedo();
    std::vector<uint8_t> genBrickAlbedo();
    std::vector<uint8_t> genModernOfficeAlbedo();

    std::vector<uint8_t> genConcreteNormal();
    std::vector<uint8_t> genGlassTowerNormal();
    std::vector<uint8_t> genBrickNormal();
    std::vector<uint8_t> genModernOfficeNormal();

    // Upload all layers of one Texture2DArray
    bool uploadArray(rhi::RHITexture* tex,
                     const std::vector<std::vector<uint8_t>>& layers);
};

} // namespace rendering
