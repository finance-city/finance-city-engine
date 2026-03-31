#include "TextureManager.hpp"
#include "src/utils/Logger.hpp"
#include <cmath>
#include <algorithm>

namespace rendering {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline uint8_t u8(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// Hash used for deterministic per-brick colour variation
static inline float hash1(uint32_t x) {
    x = (x ^ (x >> 16)) * 0x45d9f3b;
    x = (x ^ (x >> 16)) * 0x45d9f3b;
    x ^= (x >> 16);
    return static_cast<float>(x & 0xFF) / 255.0f;
}

static inline void setPixel(std::vector<uint8_t>& buf, int x, int y, int W,
                             uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    int idx = (y * W + x) * 4;
    buf[idx+0] = r; buf[idx+1] = g; buf[idx+2] = b; buf[idx+3] = a;
}

// Encode tangent-space normal to RGBA8: (N + 1) * 0.5 * 255
static inline void setNormal(std::vector<uint8_t>& buf, int x, int y, int W,
                              float nx, float ny, float nz) {
    setPixel(buf, x, y, W, u8((nx+1.0f)*0.5f), u8((ny+1.0f)*0.5f), u8((nz+1.0f)*0.5f));
}

static std::vector<uint8_t> flatBuffer(int W, int H,
                                        uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> buf(W * H * 4);
    for (int i = 0; i < W * H; ++i) {
        buf[i*4+0] = r; buf[i*4+1] = g; buf[i*4+2] = b; buf[i*4+3] = 255;
    }
    return buf;
}

// ---------------------------------------------------------------------------

TextureManager::TextureManager(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : m_device(device), m_queue(queue) {}

bool TextureManager::generateFallback() {
    if (!m_device || !m_queue) return false;

    // 1×1 mid-gray albedo and flat normal (0,0,1 in tangent space) for all 4 layers
    const uint8_t grayPx[4]   = { 128, 128, 128, 255 };
    const uint8_t normalPx[4] = { 128, 128, 255, 255 };
    const std::vector<uint8_t> grayLayer(grayPx, grayPx + 4);
    const std::vector<uint8_t> normLayer(normalPx, normalPx + 4);
    const std::vector<std::vector<uint8_t>> albedoLayers(NUM_LAYERS, grayLayer);
    const std::vector<std::vector<uint8_t>> normalLayers(NUM_LAYERS, normLayer);

    // Albedo array
    {
        rhi::TextureDesc desc{};
        desc.size            = {1, 1, 1};
        desc.format          = rhi::TextureFormat::RGBA8Unorm;
        desc.usage           = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        desc.arrayLayerCount = NUM_LAYERS;
        desc.mipLevelCount   = 1;
        desc.label           = "FallbackAlbedoArray";
        m_albedoArray = m_device->createTexture(desc);
        if (!m_albedoArray) return false;
        uploadArray(m_albedoArray.get(), albedoLayers);
        rhi::TextureViewDesc vd{};
        vd.format          = rhi::TextureFormat::RGBA8Unorm;
        vd.dimension       = rhi::TextureViewDimension::View2DArray;
        vd.arrayLayerCount = NUM_LAYERS;
        m_albedoView = m_albedoArray->createView(vd);
    }

    // Normal array
    {
        rhi::TextureDesc desc{};
        desc.size            = {1, 1, 1};
        desc.format          = rhi::TextureFormat::RGBA8Unorm;
        desc.usage           = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        desc.arrayLayerCount = NUM_LAYERS;
        desc.mipLevelCount   = 1;
        desc.label           = "FallbackNormalArray";
        m_normalArray = m_device->createTexture(desc);
        if (!m_normalArray) return false;
        uploadArray(m_normalArray.get(), normalLayers);
        rhi::TextureViewDesc vd{};
        vd.format          = rhi::TextureFormat::RGBA8Unorm;
        vd.dimension       = rhi::TextureViewDimension::View2DArray;
        vd.arrayLayerCount = NUM_LAYERS;
        m_normalView = m_normalArray->createView(vd);
    }

    // Basic sampler (no anisotropy)
    {
        rhi::SamplerDesc sd{};
        sd.magFilter     = rhi::FilterMode::Linear;
        sd.minFilter     = rhi::FilterMode::Linear;
        sd.mipmapFilter  = rhi::MipmapMode::Linear;
        sd.addressModeU  = rhi::AddressMode::Repeat;
        sd.addressModeV  = rhi::AddressMode::Repeat;
        sd.addressModeW  = rhi::AddressMode::Repeat;
        sd.maxAnisotropy = 1;
        sd.label         = "FallbackSampler";
        m_anisoSampler   = m_device->createSampler(sd);
        if (!m_anisoSampler) return false;
    }

    m_ready = true;
    LOG_WARN("TextureManager") << "Using 1×1 fallback textures (procedural generation failed)";
    return true;
}

bool TextureManager::generate() {
    if (!m_device || !m_queue) return false;

    // Create albedo Texture2DArray (4 layers, RGBA8Unorm, Sampled | CopyDst)
    {
        rhi::TextureDesc desc{};
        desc.size = {TEX_SIZE, TEX_SIZE, 1};
        desc.format = rhi::TextureFormat::RGBA8Unorm;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        desc.arrayLayerCount = NUM_LAYERS;
        desc.mipLevelCount = 1;
        desc.label = "BuildingAlbedoArray";
        m_albedoArray = m_device->createTexture(desc);
        if (!m_albedoArray) {
            LOG_ERROR("TextureManager") << "Failed to create albedo array texture";
            return false;
        }
        rhi::TextureViewDesc vd{};
        vd.format = rhi::TextureFormat::RGBA8Unorm;
        vd.dimension = rhi::TextureViewDimension::View2DArray;
        vd.baseArrayLayer = 0;
        vd.arrayLayerCount = NUM_LAYERS;
        m_albedoView = m_albedoArray->createView(vd);
    }

    // Create normal Texture2DArray
    {
        rhi::TextureDesc desc{};
        desc.size = {TEX_SIZE, TEX_SIZE, 1};
        desc.format = rhi::TextureFormat::RGBA8Unorm;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        desc.arrayLayerCount = NUM_LAYERS;
        desc.mipLevelCount = 1;
        desc.label = "BuildingNormalArray";
        m_normalArray = m_device->createTexture(desc);
        if (!m_normalArray) {
            LOG_ERROR("TextureManager") << "Failed to create normal array texture";
            return false;
        }
        rhi::TextureViewDesc vd{};
        vd.format = rhi::TextureFormat::RGBA8Unorm;
        vd.dimension = rhi::TextureViewDimension::View2DArray;
        vd.baseArrayLayer = 0;
        vd.arrayLayerCount = NUM_LAYERS;
        m_normalView = m_normalArray->createView(vd);
    }

    // Create anisotropic sampler (x4 clamp-to-edge for facade tiling)
    {
        rhi::SamplerDesc sd{};
        sd.magFilter = rhi::FilterMode::Linear;
        sd.minFilter = rhi::FilterMode::Linear;
        sd.mipmapFilter = rhi::MipmapMode::Linear;
        sd.addressModeU = rhi::AddressMode::Repeat;
        sd.addressModeV = rhi::AddressMode::Repeat;
        sd.addressModeW = rhi::AddressMode::Repeat;
        sd.maxAnisotropy = 4;
        sd.label = "BuildingAnisoSampler";
        m_anisoSampler = m_device->createSampler(sd);
    }

    // Generate pixel data and upload
    std::vector<std::vector<uint8_t>> albedoLayers = {
        genConcreteAlbedo(),
        genGlassTowerAlbedo(),
        genBrickAlbedo(),
        genModernOfficeAlbedo()
    };
    std::vector<std::vector<uint8_t>> normalLayers = {
        genConcreteNormal(),
        genGlassTowerNormal(),
        genBrickNormal(),
        genModernOfficeNormal()
    };

    if (!uploadArray(m_albedoArray.get(), albedoLayers)) {
        LOG_ERROR("TextureManager") << "Failed to upload albedo array";
        return false;
    }
    if (!uploadArray(m_normalArray.get(), normalLayers)) {
        LOG_ERROR("TextureManager") << "Failed to upload normal array";
        return false;
    }

    m_ready = true;
    LOG_INFO("TextureManager") << "Procedural building textures generated ("
                               << TEX_SIZE << "x" << TEX_SIZE << "x" << NUM_LAYERS << ")";
    return true;
}

// ---------------------------------------------------------------------------
// GPU Upload
// ---------------------------------------------------------------------------

bool TextureManager::uploadArray(rhi::RHITexture* tex,
                                  const std::vector<std::vector<uint8_t>>& layers) {
    const uint32_t W = TEX_SIZE;
    const uint32_t H = TEX_SIZE;
    const uint32_t bytesPerLayer = W * H * 4;

    auto encoder = m_device->createCommandEncoder();
    if (!encoder) return false;

    // Transition texture to transfer destination
    encoder->transitionTextureLayout(tex,
        rhi::TextureLayout::Undefined,
        rhi::TextureLayout::TransferDst);

    for (uint32_t layer = 0; layer < static_cast<uint32_t>(layers.size()); ++layer) {
        const auto& pixels = layers[layer];

        // Staging buffer for this layer
        rhi::BufferDesc stagingDesc{};
        stagingDesc.size = bytesPerLayer;
        stagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
        stagingDesc.label = "Texture Staging";
        auto staging = m_device->createBuffer(stagingDesc);
        if (!staging) return false;
        staging->write(pixels.data(), bytesPerLayer);

        rhi::BufferTextureCopyInfo bufInfo{};
        bufInfo.buffer = staging.get();
        bufInfo.offset = 0;
        bufInfo.bytesPerRow = W * 4;
        bufInfo.rowsPerImage = H;

        rhi::TextureCopyInfo texInfo{};
        texInfo.texture = tex;
        texInfo.mipLevel = 0;
        texInfo.origin = {0, 0, 0};
        texInfo.arrayLayer = layer;

        encoder->copyBufferToTexture(bufInfo, texInfo, rhi::Extent3D(W, H, 1));

        // Submit per-layer to avoid keeping all staging buffers alive at once
        auto cmdBuf = encoder->finish();
        m_queue->submit(cmdBuf.get());
        m_queue->waitIdle();

        // Reset encoder for next layer
        encoder = m_device->createCommandEncoder();
        if (!encoder) return false;
    }

    // Final transition to shader read
    encoder->transitionTextureLayout(tex,
        rhi::TextureLayout::TransferDst,
        rhi::TextureLayout::ShaderReadOnly);

    auto cmdBuf = encoder->finish();
    m_queue->submit(cmdBuf.get());
    m_queue->waitIdle();

    return true;
}

// ---------------------------------------------------------------------------
// Albedo Generation
// ---------------------------------------------------------------------------

std::vector<uint8_t> TextureManager::genConcreteAlbedo() {
    const int W = TEX_SIZE, H = TEX_SIZE;
    // Base color: light gray concrete
    auto buf = flatBuffer(W, H, 143, 143, 148);

    // Horizontal panel joints every 64px — slightly darker
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            bool hJoint = (y % 64 < 2);
            bool vJoint = (x % 32 < 2);
            if (hJoint || vJoint) {
                uint8_t shade = hJoint && vJoint ? 110 : (hJoint ? 118 : 122);
                setPixel(buf, x, y, W, shade, shade, shade+3);
            }
            // Subtle surface noise based on position hash
            float n = hash1(static_cast<uint32_t>(x * 17 + y * 31)) * 0.06f - 0.03f;
            int idx = (y * W + x) * 4;
            buf[idx+0] = u8((buf[idx+0]/255.0f) + n);
            buf[idx+1] = u8((buf[idx+1]/255.0f) + n);
            buf[idx+2] = u8((buf[idx+2]/255.0f) + n);
        }
    }
    return buf;
}

std::vector<uint8_t> TextureManager::genGlassTowerAlbedo() {
    const int W = TEX_SIZE, H = TEX_SIZE;
    auto buf = flatBuffer(W, H, 38, 65, 115);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            bool mullion  = (x % 16 < 2);    // vertical aluminum frame
            bool spandrel = (y % 32 < 4);    // horizontal spandrel band

            uint8_t r, g, b;
            if (mullion) {
                // Silver-gray aluminum
                r = 88; g = 105; b = 118;
            } else if (spandrel) {
                // Opaque spandrel — slightly lighter blue-gray
                r = 52; g = 78; b = 128;
            } else {
                // Glass panel — add subtle brightness variation per panel
                uint32_t px = static_cast<uint32_t>(x / 16);
                uint32_t py = static_cast<uint32_t>(y / 32);
                float bright = 0.85f + hash1(px * 37 + py * 71) * 0.15f;
                r = u8(0.150f * bright);
                g = u8(0.255f * bright);
                b = u8(0.450f * bright);
            }
            setPixel(buf, x, y, W, r, g, b);
        }
    }
    return buf;
}

std::vector<uint8_t> TextureManager::genBrickAlbedo() {
    const int W = TEX_SIZE, H = TEX_SIZE;
    auto buf = flatBuffer(W, H, 0, 0, 0);

    const int brickH = 10;   // brick row height (8px brick + 2px mortar)
    const int brickW = 24;   // brick width (22px + 2px mortar)

    for (int y = 0; y < H; ++y) {
        int row    = y / brickH;
        int localY = y % brickH;
        bool isMortarH = (localY >= brickH - 2);

        // Running bond: odd rows offset by half brick
        int offsetX = (row % 2) * (brickW / 2);

        for (int x = 0; x < W; ++x) {
            int adjX   = (x + offsetX) % W;
            int col    = adjX / brickW;
            int localX = adjX % brickW;
            bool isMortarV = (localX >= brickW - 2);

            uint8_t r, g, b;
            if (isMortarH || isMortarV) {
                // Mortar: off-white
                r = 200; g = 195; b = 185;
            } else {
                // Brick color with per-brick variation
                float var = hash1(static_cast<uint32_t>(row * 23 + col * 41)) * 0.15f;
                r = u8(0.745f + var);
                g = u8(0.314f + var * 0.4f);
                b = u8(0.176f + var * 0.1f);
            }
            setPixel(buf, x, y, W, r, g, b);
        }
    }
    return buf;
}

std::vector<uint8_t> TextureManager::genModernOfficeAlbedo() {
    const int W = TEX_SIZE, H = TEX_SIZE;
    auto buf = flatBuffer(W, H, 0, 0, 0);

    const int bandH     = 32;  // total band height
    const int glassH    = 24;  // glass portion
    // const int spandrelH = 8; // spandrel/concrete portion (the rest)

    for (int y = 0; y < H; ++y) {
        int localY   = y % bandH;
        bool isGlass = (localY < glassH);

        for (int x = 0; x < W; ++x) {
            uint8_t r, g, b;
            if (isGlass) {
                // Glass with subtle horizontal shimmer
                float shine = 0.9f + hash1(static_cast<uint32_t>(y * 7 + x / 8)) * 0.1f;
                r = u8(0.216f * shine);
                g = u8(0.353f * shine);
                b = u8(0.569f * shine);
            } else {
                // Spandrel: light warm concrete
                r = 210; g = 205; b = 195;
            }
            setPixel(buf, x, y, W, r, g, b);
        }
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Normal Map Generation
// ---------------------------------------------------------------------------

std::vector<uint8_t> TextureManager::genConcreteNormal() {
    const int W = TEX_SIZE, H = TEX_SIZE;
    // Mostly flat with slight inward bump at panel seams
    auto buf = flatBuffer(W, H, 128, 128, 255);  // flat (0,0,1)

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            // At horizontal joints: slight upward slope on leading edge
            int hy = y % 64;
            int vx = x % 32;
            float ny = 0.0f, nx_ = 0.0f;
            if (hy == 0) ny = -0.3f; else if (hy == 1) ny = 0.3f;
            if (vx == 0) nx_ = -0.3f; else if (vx == 1) nx_ = 0.3f;
            float nz = std::sqrt(std::max(0.0f, 1.0f - nx_*nx_ - ny*ny));
            if (nx_ != 0.0f || ny != 0.0f)
                setNormal(buf, x, y, W, nx_, ny, nz);
        }
    }
    return buf;
}

std::vector<uint8_t> TextureManager::genGlassTowerNormal() {
    const int W = TEX_SIZE, H = TEX_SIZE;
    // Mostly flat; slight inward groove at mullion edges
    auto buf = flatBuffer(W, H, 128, 128, 255);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int mx = x % 16;
            float nx_ = 0.0f;
            if (mx == 0) nx_ = -0.25f;
            else if (mx == 1) nx_ = 0.25f;
            if (nx_ != 0.0f) {
                float nz = std::sqrt(std::max(0.0f, 1.0f - nx_*nx_));
                setNormal(buf, x, y, W, nx_, 0.0f, nz);
            }
        }
    }
    return buf;
}

std::vector<uint8_t> TextureManager::genBrickNormal() {
    const int W = TEX_SIZE, H = TEX_SIZE;
    const int brickH = 10;
    const int brickW = 24;

    auto buf = flatBuffer(W, H, 128, 128, 255);

    for (int y = 0; y < H; ++y) {
        int row    = y / brickH;
        int localY = y % brickH;
        int offsetX = (row % 2) * (brickW / 2);

        for (int x = 0; x < W; ++x) {
            int adjX   = (x + offsetX) % W;
            int localX = adjX % brickW;

            float nx_ = 0.0f, ny = 0.0f;
            // Mortar edge slope
            if (localY == 0) ny = 0.35f;      // bottom edge: slope up
            else if (localY == brickH-3) ny = -0.35f;  // top edge: slope down
            if (localX == 0) nx_ = 0.25f;
            else if (localX == brickW-3) nx_ = -0.25f;

            if (nx_ != 0.0f || ny != 0.0f) {
                float nz = std::sqrt(std::max(0.0f, 1.0f - nx_*nx_ - ny*ny));
                setNormal(buf, x, y, W, nx_, ny, nz);
            }
        }
    }
    return buf;
}

std::vector<uint8_t> TextureManager::genModernOfficeNormal() {
    const int W = TEX_SIZE, H = TEX_SIZE;
    const int bandH = 32;
    const int glassH = 24;

    auto buf = flatBuffer(W, H, 128, 128, 255);

    for (int y = 0; y < H; ++y) {
        int localY = y % bandH;
        float ny = 0.0f;
        if (localY == glassH - 1) ny = -0.3f;    // glass/spandrel top edge
        else if (localY == glassH)   ny = 0.3f;   // spandrel leading edge
        if (ny != 0.0f) {
            float nz = std::sqrt(std::max(0.0f, 1.0f - ny*ny));
            for (int x = 0; x < W; ++x)
                setNormal(buf, x, y, W, 0.0f, ny, nz);
        }
    }
    return buf;
}

} // namespace rendering
