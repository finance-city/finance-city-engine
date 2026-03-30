# Finance City Engine — 변경 이력

프로젝트 완성도 리뷰(2026-03-06) 기반 작업 결과 정리.

---

## P1-1. WASM 가격 업데이트 수정

**파일**: `src/Application.cpp`

**문제**: WASM 빌드에서 `mockDataGen->generateUpdates()`가 실제로 호출되지 않아 건물 높이가 변하지 않았다. `priceUpdateTimer` / `priceUpdateInterval` 필드가 선언만 되고 사용되지 않았다.

**수정**: `mainLoopFrame()` 내부에 WS 연결 상태에 따른 분기 추가.
- WebSocket 연결 중 → `wsDataFeed->drainUpdates()` 사용
- 비연결(WASM 포함) → `priceUpdateTimer`로 주기적 mock fallback

```cpp
// WS 연결 중이면 실시간 데이터, 아니면 mock 주기 업데이트
bool wsProvided = false;
#ifndef __EMSCRIPTEN__
if (wsDataFeed && wsDataFeed->isConnected()) {
    auto updates = wsDataFeed->drainUpdates();
    if (!updates.empty()) {
        worldManager->updateMarketData(updates);
        wsProvided = true;
    }
}
#endif
if (!wsProvided && mockDataGen) {
    priceUpdateTimer += deltaTime;
    if (priceUpdateTimer >= priceUpdateInterval) {
        priceUpdateTimer = 0.0f;
        worldManager->updateMarketData(mockDataGen->generateUpdates());
    }
}
```

---

## P2-1. Renderer 레거시 파이프라인 제거

**파일**: `src/rendering/Renderer.hpp`, `src/rendering/Renderer.cpp`

OBJ 모델 렌더링용 레거시 리소스 및 메서드 전부 제거.

**제거된 멤버 (Renderer.hpp)**:
- `rhiVertexShader`, `rhiFragmentShader`
- `rhiPipelineLayout`, `rhiPipeline`
- `rhiVertexBuffer`, `rhiIndexBuffer`, `rhiIndexCount`

**제거된 메서드 (Renderer.hpp / .cpp)**:
- `createRHIPipeline()` — `slang.spv` 로드, WASM에서 `#ifndef __EMSCRIPTEN__`으로 우회하던 코드
- `createRHIBuffers()` — 스테이징 버퍼를 통한 정점/인덱스 업로드
- `loadModel()`, `loadTexture()` — SceneManager / ResourceManager 경유 OBJ 경로

---

## P2-2. Renderer.cpp Phase 주석 정리

**파일**: `src/rendering/Renderer.cpp`

개발 이력 주석(`// Phase 4.5`, `// Phase 7` 등) 13개 이상을 실제 동작 설명 주석으로 교체하거나 제거.

| 이전 | 이후 |
|------|------|
| `// Phase 4: RHI Resource Creation` | `// RHI Resource Creation` |
| `// Phase 8: RHI Uniform Buffer Update` | `// Uniform Buffer Update` |
| `// Phase 7: Primary RHI Render Loop` | `// Frame Rendering` |
| `// Phase 3.1: Particle Renderer Creation` | `// Particle Renderer Creation` |
| `// Phase 3.3: Skybox Renderer Creation` | `// Skybox Renderer Creation` |
| `// Phase 2.2: GPU Frustum Culling Pipeline` | `// GPU Frustum Culling Pipeline` |

---

## VIS-1. 오프스크린 HDR 렌더 타겟 도입 (WebGPU 경로)

**관련 파일**: `src/rendering/Renderer.hpp`, `src/rendering/Renderer.cpp`,
`shaders/building.wgsl`, `shaders/tonemap.wgsl`, `shaders/tonemap.vert.glsl`,
`shaders/tonemap.frag.glsl`, `CMakeLists.txt`

### 아키텍처 변경

```
[이전] 기하 렌더 (ACES 내장) ──────────────────────→ 스왑체인
[이후] 기하 렌더 (선형 HDR) → HDR 텍스처 → 토네맵 패스 → 스왑체인
```

WebGPU(WASM) 경로에만 적용. Vulkan 경로는 기존 방식 유지.

### 새 셰이더 파일

| 파일 | 설명 |
|------|------|
| `shaders/tonemap.wgsl` | WebGPU 풀스크린 삼각형 + ACES + 감마 보정 |
| `shaders/tonemap.vert.glsl` | Vulkan 풀스크린 삼각형 (정점 버퍼 없음) |
| `shaders/tonemap.frag.glsl` | Vulkan ACES 토네맵 (sRGB 포맷이므로 감마 불필요) |

### building.wgsl 수정

`fs_main`에서 ACESFilm + 감마 보정 제거 → 선형 HDR 출력.

```wgsl
// 이전
color = ACESFilm(color * exp);
color = pow(color, vec3<f32>(1.0 / 2.2));
return vec4<f32>(color, 1.0);

// 이후 — 토네맵은 tonemap 패스에서 처리
color = color * exp;
return vec4<f32>(color, 1.0);
```

### Renderer 변경 요약

**Renderer.hpp** — `#ifdef __EMSCRIPTEN__` 가드 아래 추가:
- `hdrColorTexture`, `hdrColorView`, `hdrSampler` (HDR 렌더 타겟)
- `tonemapVertexShader`, `tonemapFragmentShader`, `tonemapBindGroupLayout`,
  `tonemapBindGroup`, `tonemapPipelineLayout`, `tonemapPipeline`
- `createHDRRenderTarget()`, `createTonemapPipeline()` 메서드 선언

**Renderer.cpp** — 초기화 순서:
```
createRHIDepthResources()
createHDRRenderTarget()      ← 신규 (WASM 전용)
createBuildingPipeline()     ← 컬러 포맷: RGBA16Float (WASM) / 스왑체인 (Vulkan)
createCullingPipeline()
createParticleRenderer()     ← 컬러 포맷: RGBA16Float (WASM) / 스왑체인 (Vulkan)
createSkyboxRenderer()       ← 동일
createShadowRenderer()
createTonemapPipeline()      ← 신규 (WASM 전용)
```

**drawFrame()** — WASM 경로 렌더 루프:
1. 기하 렌더 패스: 스카이박스 → 건물 → 파티클 → `hdrColorTexture`
2. 토네맵 패스: `hdrColorTexture` 샘플링 → ACES + 감마 → 스왑체인

**리사이즈 시**: `hdrColorTexture` 재생성 + `tonemapBindGroup` 갱신 (뷰 포인터 변경).

### CMakeLists.txt

`tonemap.vert.spv`, `tonemap.frag.spv` 컴파일 커맨드 및 `building_shaders` 타겟 의존성 추가.

---

## VIS-2. FXAA 도입 (WebGPU 경로)

**관련 파일**: `src/rendering/Renderer.hpp`, `src/rendering/Renderer.cpp`,
`shaders/fxaa.wgsl`, `CMakeLists.txt`

### VIS-2 파이프라인 구조

```
[이전] 기하 렌더 → HDR 텍스처 → 토네맵 → 스왑체인
[이후] 기하 렌더 → HDR 텍스처 → 토네맵 → LDR 텍스처 → FXAA → 스왑체인
```

### 새 파일: fxaa.wgsl

| 파일 | 설명 |
| --- | --- |
| `shaders/fxaa.wgsl` | FXAA 3.11 (간소화) — sub-pixel blend + 12-step edge walk |

**알고리즘 단계:**

1. 5-tap 카디널 샘플 (uniform flow) → 루마 대비 검사 → 조기 종료
2. 4-tap 코너 샘플 → 가중 평균으로 sub-pixel blend factor 계산
3. Sobel-like 연산자로 엣지 방향(isHorizontal) 결정
4. 12-step edge walk로 엣지 끝점 탐색
5. 거리 기반 edge blend + sub-pixel blend → max → 최종 UV 오프셋 적용

```wgsl
// WGSL 비-uniform 흐름(루프/조건) 안에서 반드시 textureSampleLevel 사용
fn smp(uv: vec2<f32>) -> vec3<f32> {
    return textureSampleLevel(ldrTexture, ldrSampler, uv, 0.0).rgb;
}
// textureDimensions()로 texel 크기 계산 — UBO 불필요
let ts = 1.0 / vec2<f32>(textureDimensions(ldrTexture));
```

### VIS-2 Renderer 변경 요약

**Renderer.hpp** — `#ifdef __EMSCRIPTEN__` 가드 아래 추가:

- `ldrColorTexture`, `ldrColorView` (RGBA8Unorm LDR 중간 타겟)
- `fxaaVertexShader`, `fxaaFragmentShader`, `fxaaBindGroupLayout`,
  `fxaaBindGroup`, `fxaaPipelineLayout`, `fxaaPipeline`
- `createFXAAPipeline()` 메서드 선언

**Renderer.cpp** — 초기화 순서:

```
createTonemapPipeline()    ← 출력 포맷 RGBA8Unorm으로 변경 (LDR 중간 타겟)
createFXAAPipeline()       ← 신규 (WASM 전용)
```

**createHDRRenderTarget()** — LDR 중간 텍스처 추가 생성:

```cpp
ldrDesc.format = rhi::TextureFormat::RGBA8Unorm;
ldrDesc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled;
```

**drawFrame()** — WASM 경로 렌더 루프:

1. 기하 렌더 패스 → `hdrColorTexture`
2. 토네맵 패스: `hdrColorTexture` → ACES + 감마 → `ldrColorTexture`
3. FXAA 패스: `ldrColorTexture` → 안티에일리어싱 → 스왑체인

**리사이즈 시**: LDR 텍스처 재생성 + `fxaaBindGroup` 갱신 (두 resize 핸들러 모두).

### VIS-2 CMakeLists.txt

`fxaa.wgsl` WASM `copy_if_different` 목록에 추가.

---

## 다음 작업 (TODO 기준)

| 우선순위 | 항목 | 전제조건 |
| --- | --- | --- |
| VIS-3 | Bloom | VIS-1 완료 ✓ |
| VIS-4 | SSAO | VIS-1 완료 ✓ |
| P2-3 | `instanceCount + 1` 의도 확인 | — |
| P2-4 | `validationLayers` 파라미터 정리 | — |
| P3-1 | `ScissorRect.extent` → `Extent2D` | — |
| P3-2 | `BuildingEntity.rotation` → `glm::quat` | — |
| P3-3 | `initializeFromConfig` stub 처리 | — |
