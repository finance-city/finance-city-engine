# Visual Upgrade Plan — 텍스처 & 환경 고도화

작성: 2026-03-30
기반 분석: 현재 렌더링 아키텍처 전수 조사 결과

---

## 현재 렌더링 파이프라인 상태

```
라이팅: Cook-Torrance PBR + IBL (Irradiance/Prefiltered/BRDF LUT) + PCF 3×3 그림자 ← 고품질
텍스처: 빌딩/지면 없음 — 단색 albedo만 (가격 변화에 따른 녹·적·회)
메시:   모든 빌딩 = 동일한 단위 큐브 (24 verts), 지면 = 같은 큐브를 100km로 스케일
UV:     per-face UV 존재하지만 텍스처 샘플링 없음
도로:   별도 지오메트리 없음
```

### 핵심 구조 요약

| 항목 | 현재 값 |
| --- | --- |
| 버텍스 구조 | `pos(12) + normal(12) + texCoord(8)` = 32 bytes |
| ObjectData (SSBO) | `worldMatrix(64) + bbMin(16) + bbMax(16) + colorMetallic(16) + roughAOPad(16)` = 128 bytes |
| Group 0 바인딩 | 0=UBO, 1=ShadowTex, 2=ShadowSampler, 3=Irradiance, 4=Prefiltered, 5=BRDF LUT, 6=IBLSampler |
| Group 1 바인딩 | 0=ObjectBuffer(SSBO), 1=VisibleIndices(SSBO) |
| 사용 가능한 패딩 | `roughAOPad.ba` = 8 bytes 현재 0으로 낭비 중 |

---

## 단계별 작업 계획

### 의존 관계

```
Phase 1 (프로시저럴 창문/이미시브)  → 독립 실행 가능 ← 가장 빠른 시각 개선
Phase 2 (텍스처 인프라)             → Phase 3·4·5 전제조건
  ├─ Phase 3 (빌딩 파사드 텍스처)
  ├─ Phase 4 (지면 타일링 + 도로 그리드)
  └─ Phase 5 (노말맵 + 버텍스 탄젠트)
Phase 6 (빌딩 메시 다변화)          → Phase 2 이후 병행 가능
Phase 7 (LOD)                       → Phase 3–5 완료 후
```

---

## Phase 1 — 프로시저럴 창문 & 이미시브 (셰이더만)

**수정 파일:** `shaders/building.wgsl`, `src/rendering/InstancedRenderData.hpp`, `src/game/managers/BuildingManager.cpp`
**신규 자산:** 없음

### ObjectData 패딩 재활용

```cpp
// 현재: roughnessAOPad = vec4(roughness, ao, 0, 0)
// 변경: roughnessAOPad = vec4(roughness, ao, numFloors, priceRate)
//                                             ↑ 패딩 → 활용
```

### WGSL 창문 그리드

```wgsl
// UV 기반 창문 마스크
let floors   = objectData.roughnessAOPad.z;
let wMaskY   = step(0.1, fract(uv.y * floors)) * step(fract(uv.y * floors), 0.85);
let wMaskX   = step(0.1, fract(uv.x * 4.0))   * step(fract(uv.x * 4.0),   0.85);
let isWindow = wMaskX * wMaskY;

// 재질 분기: 콘크리트(벽) vs 유리(창)
let roughness = mix(0.8,  0.05, isWindow);
let metallic  = mix(0.1,  0.9,  isWindow);

// 가격 변화율 연동 이미시브 (급등 = 창문 밝게)
let priceRate = objectData.roughnessAOPad.w;
let emissive  = isWindow * vec3(0.2, 0.4, 0.8) * max(0.0, priceRate * 5.0);
```

### 변경 포인트

- `BuildingManager::createBuilding()` — `numFloors = currentHeight / 3.5f` (층고 3.5m 기준)
- `BuildingManager::updateObjectBuffer()` — `roughAOPad.z = floors`, `.w = priceChangePercent`

---

## Phase 2 — 텍스처 인프라 구축

**신규 파일:** `src/rendering/TextureManager.hpp`, `src/rendering/TextureManager.cpp`
**수정 파일:** `src/utils/Vertex.hpp`, `src/rendering/InstancedRenderData.hpp`, `src/rendering/Renderer.hpp/.cpp`, `CMakeLists.txt`

### 2-1. TextureManager 클래스

```cpp
class TextureManager {
public:
    // stb_image로 PNG/JPG 로드 → GPU 텍스처 + 밉맵 자동 생성
    uint32_t loadTexture2D(const std::string& path);

    // 여러 이미지 → Texture2DArray (빌딩 파사드 타입별)
    uint32_t loadTexture2DArray(const std::vector<std::string>& paths);

    rhi::RHITexture*     getTexture(uint32_t handle) const;
    rhi::RHITextureView* getView(uint32_t handle) const;
    rhi::RHISampler*     getAnisoSampler() const;  // x16 anisotropic

private:
    void generateMipmaps(rhi::RHITexture*, uint32_t w, uint32_t h, uint32_t layers);
};
```

### 2-2. 버텍스 탄젠트 추가 (노말맵 전제조건)

```cpp
// src/utils/Vertex.hpp
// 현재: 32 bytes
struct Vertex {
    glm::vec3 pos;       // 12
    glm::vec3 normal;    // 12
    glm::vec2 texCoord;  // 8
};

// 변경: 48 bytes
struct Vertex {
    glm::vec3 pos;       // 12
    glm::vec3 normal;    // 12
    glm::vec2 texCoord;  // 8
    glm::vec4 tangent;   // 16  ← xyz=tangent, w=bitangent sign (±1)
};
```

**연쇄 수정:** `createDefaultMesh()` 탄젠트 계산, `createBuildingPipeline()` attribute location 3 추가

### 2-3. ObjectData SSBO 확장

```cpp
// src/rendering/InstancedRenderData.hpp
// 현재: 128 bytes
struct alignas(16) ObjectData {
    glm::mat4 worldMatrix;        // 64
    glm::vec4 boundingBoxMin;     // 16
    glm::vec4 boundingBoxMax;     // 16
    glm::vec4 colorAndMetallic;   // 16
    glm::vec4 roughnessAOPad;     // 16  (ba = 패딩)
};

// 변경: 144 bytes (+16)
struct alignas(16) ObjectData {
    glm::mat4 worldMatrix;        // 64
    glm::vec4 boundingBoxMin;     // 16
    glm::vec4 boundingBoxMax;     // 16
    glm::vec4 colorAndMetallic;   // 16
    glm::vec4 roughAOFloors;      // 16  r=rough, g=ao, b=numFloors, a=priceRate
    glm::vec4 texParams;          // 16  r=textureIndex, g=uvScale, ba=reserved
};
```

### 2-4. 빌딩 파이프라인 바인딩 확장

```
Group 0 현재:  binding 0–6
Group 0 추가:
    binding 7   texture_2d_array  ← 빌딩 albedo 아틀라스 (N 타입)
    binding 8   texture_2d_array  ← 빌딩 normal 아틀라스
    binding 9   texture_2d        ← 지면 albedo (tiled)
    binding 10  texture_2d        ← 지면 normal (tiled)
    binding 11  sampler           ← anisotropic x16
```

---

## Phase 3 — 빌딩 파사드 텍스처

**신규 자산:** PNG 파일 8종 (1024×1024)
**수정 파일:** `shaders/building.wgsl`, `src/game/managers/BuildingManager.cpp`

### 텍스처 타입 구성

```
textures/buildings/
├── glass_tower_albedo.png    — 유리 커튼월 고층 (metallic, low roughness)
├── glass_tower_normal.png
├── concrete_slab_albedo.png  — 콘크리트 판상형 (rough)
├── concrete_slab_normal.png
├── brick_mid_albedo.png      — 벽돌 중층
├── brick_mid_normal.png
├── modern_office_albedo.png  — 현대 오피스 (유리+콘크리트 혼합)
└── modern_office_normal.png
```

### WGSL 텍스처 샘플링

```wgsl
@group(0) @binding(7)  var buildingAlbedoArray: texture_2d_array<f32>;
@group(0) @binding(8)  var buildingNormalArray:  texture_2d_array<f32>;
@group(0) @binding(11) var anisoSampler: sampler;

// fragment
let texIdx     = u32(objectData.texParams.r);
let uvScale    = objectData.texParams.g;
let scaledUV   = uv * uvScale;

// 텍스처 albedo + 가격 tint 혼합 (35%)
let texAlbedo   = textureSample(buildingAlbedoArray, anisoSampler, scaledUV, texIdx).rgb;
let tintColor   = objectData.colorAndMetallic.rgb;
let finalAlbedo = mix(texAlbedo, texAlbedo * tintColor, 0.35);

// TBN 노말 매핑
let N   = normalize(in.worldNormal);
let T   = normalize(in.worldTangent.xyz);
let B   = cross(N, T) * in.worldTangent.w;
let TBN = mat3x3<f32>(T, B, N);
let rawN   = textureSample(buildingNormalArray, anisoSampler, scaledUV, texIdx).rgb;
let mappedN = normalize(TBN * (rawN * 2.0 - 1.0));
```

### 텍스처 타입 선택 로직

```cpp
// BuildingManager::createBuilding() 내
BuildingTexType selectTexType(float height, glm::vec3 footprint) {
    float aspect = footprint.x / footprint.z;
    if (height > 80.0f)             return BuildingTexType::GlassTower;
    if (height > 40.0f)             return BuildingTexType::ModernOffice;
    if (aspect > 1.8f)              return BuildingTexType::ConcreteSlab;
    return BuildingTexType::BrickMid;
}
```

---

## Phase 4 — 지면 타일링 + 도로 그리드

**신규 자산:** 지면 텍스처 PNG 2종
**수정 파일:** `shaders/building.wgsl`, `src/game/managers/BuildingManager.cpp`

### 지면 텍스처 타일링

```wgsl
// 월드 XZ 좌표 기반 (10m당 1 타일)
let worldUV      = in.worldPos.xz * 0.1;
let groundAlbedo = textureSample(groundAlbedoTex, anisoSampler, worldUV).rgb;
let groundNormal = textureSample(groundNormalTex,  anisoSampler, worldUV).rgb;
```

### 프로시저럴 도로 & 차선 (별도 메시 불필요)

```wgsl
// 블록 경계에 도로 생성 (빌딩 간격 90m, 도로폭 12m)
let blockSize  = 90.0;
let roadWidth  = 12.0;
let blockUV    = fract(in.worldPos.xz / blockSize);
let isRoadX    = step(blockUV.x, roadWidth / blockSize);
let isRoadZ    = step(blockUV.y, roadWidth / blockSize);
let isRoad     = max(isRoadX, isRoadZ);

// 재질 블렌드
let albedo    = mix(groundAlbedo, vec3(0.18, 0.18, 0.20), isRoad);
let roughness = mix(baseRoughness, 0.88, isRoad);

// 중앙선 (3m 반복 점선, 노란색)
let laneT      = fract(in.worldPos.z / 3.0);
let isDash     = step(0.4, laneT) * step(laneT, 0.9);
let centerDist = abs(fract(in.worldPos.x / blockSize) - 0.5 * roadWidth / blockSize);
let isCenterLine = step(centerDist, 0.008) * isDash * isRoadZ;
let emissiveLine = isCenterLine * vec3(0.8, 0.7, 0.0);
```

---

## Phase 5 — 노말맵 + 버텍스 탄젠트

Phase 2에서 탄젠트 인프라 완성 후 Phase 3·4 텍스처에 적용하는 단계.

**`createDefaultMesh()` 탄젠트 값:**

```cpp
// Front face (normal = 0,0,-1): tangent = 1,0,0
{{-0.5f, 0.0f, -0.5f}, {0,0,-1}, {0,0}, {1,0,0,1}},

// Top face (normal = 0,1,0): tangent = 1,0,0
{{-0.5f, 1.0f, -0.5f}, {0,1,0}, {0,0}, {1,0,0,1}},

// Left face (normal = -1,0,0): tangent = 0,0,1
{{-0.5f, 0.0f,  0.5f}, {-1,0,0}, {0,0}, {0,0,1,1}},
```

---

## Phase 6 — 빌딩 메시 다변화

**수정 파일:** `src/game/managers/BuildingManager.hpp/.cpp`, `src/game/entities/BuildingEntity.hpp`

```cpp
enum class BuildingMeshType { Box, SteppedTower, SlimTower, WideSlab };

// BuildingManager 멤버 변경
// 현재: std::unique_ptr<Mesh> buildingMesh;
// 변경: std::unique_ptr<Mesh> buildingMeshes[4];
```

| 타입 | 특징 | 선택 기준 |
| --- | --- | --- |
| `Box` | 현재 큐브 | 기본값 |
| `SteppedTower` | 3단 세트백 | height > 40m |
| `SlimTower` | 가늘고 높음 | height > 80m, 좁은 footprint |
| `WideSlab` | 낮고 넓음 | footprint aspect > 2.0 |

---

## Phase 7 — LOD

**수정 파일:** `shaders/building.wgsl`, `src/rendering/Renderer.cpp`

```wgsl
// vertex → fragment로 LOD 레벨 전달
let distToCam = length(worldPos - ubo.cameraPos);
let lodLevel  = u32(clamp(distToCam / 200.0, 0.0, 2.0));

// fragment에서 LOD별 분기
// LOD 0 (0–200m):   텍스처 + 노말맵
// LOD 1 (200–600m): albedo 텍스처만
// LOD 2 (600m+):    단색 albedo (현재 방식)
```

---

## 수정 파일 전체 목록

| Phase | 신규 파일 | 수정 파일 |
| --- | --- | --- |
| 1 | — | `building.wgsl`, `InstancedRenderData.hpp`, `BuildingManager.cpp` |
| 2 | `TextureManager.hpp/.cpp` | `Vertex.hpp`, `InstancedRenderData.hpp`, `Renderer.hpp/.cpp`, `CMakeLists.txt` |
| 3 | 텍스처 PNG 8종 | `building.wgsl`, `BuildingManager.cpp` |
| 4 | 지면 PNG 2종 | `building.wgsl`, `BuildingManager.cpp` |
| 5 | — | `building.wgsl`, `BuildingManager.cpp`, `Renderer.cpp` |
| 6 | — | `BuildingManager.cpp/.hpp`, `BuildingEntity.hpp` |
| 7 | — | `building.wgsl`, `Renderer.cpp` |
