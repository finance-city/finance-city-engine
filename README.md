# Finance City Engine

> 실시간 금융 데이터를 3D 도시 시각화로 표현하는 렌더링 엔진

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![WebGPU](https://img.shields.io/badge/WebGPU-WGSL-orange.svg)
![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Web%20(WASM)-lightgrey.svg)

---

## 소개

Finance City Engine은 주식·자산 시세를 **3D 빌딩 높이와 색상**으로 표현하는 실시간 도시 시뮬레이션 엔진입니다.
WebSocket으로 수신한 실시간 금융 데이터를 매 프레임 반영하며, WebAssembly 빌드를 통해 브라우저에서 바로 실행할 수 있습니다.

- **빌딩 높이** → 종목 가격 또는 시가총액
- **빌딩 색상** → 가격 변화율 (상승: 녹색, 하락: 적색)
- **실시간 업데이트** → WebSocket 시세 스트림 or Mock 데이터 폴백

---

## 주요 기능

### 렌더링

| 기능 | 설명 |
| ------ | ------ |
| **PBR (Physically Based Rendering)** | GGX BRDF, 금속성·거칠기 기반 재질 |
| **IBL (Image-Based Lighting)** | HDR 환경맵 → 디퓨즈 조명 + 스페큘러 반사 |
| **Shadow Mapping** | 직교 투영 깊이맵, 슬로프-스케일 편향으로 피터패닝 최소화 |
| **GPU Frustum Culling** | 컴퓨트 셰이더 기반 비동기 컬링 (최대 4096 오브젝트) |
| **Instanced Rendering** | SSBO per-object 데이터, 단일 드로우콜로 수백 개 빌딩 렌더링 |
| **Procedural Building Textures** | CPU에서 생성하는 창문 격자 + 법선 맵 (PNG 없이 동작) |
| **HDR + Tone Mapping** | RGBA16Float 오프스크린 렌더타겟, ACES 톤맵 |
| **Bloom** | 3-패스 (프리필터 → H blur → V blur), 톤맵과 합산 |
| **FXAA** | LDR 중간 렌더타겟 → 스왑체인 안티에일리어싱 |
| **Skybox** | 큐브맵 기반 하늘 배경 |
| **Particle Effects** | 빌딩 이벤트 연동 파티클 시스템 |

### 도시 생성

- **프로시저럴 도로 그리드** — 90 m 블록, 12 m 도로폭, 황색 점선 차선
- **Stepped Tower** — 22층 이상 빌딩에 상부 18% 세백(setback) 자동 적용
- **LOD** — 카메라 거리 300 m 이상에서 창문·이미시브 비활성화

### 금융 데이터 연동

- **WebSocket 시세 스트림** — 실시간 가격 수신 및 빌딩 즉시 반영
- **Mock 데이터 폴백** — 서버 미연결 시 자동 Mock 시세 생성
- **UI 오버레이** — 빌딩마다 티커·가격·변화율 레이블 (Native ImGui / WASM HTML)
- **LIVE / MOCK 배지** — 현재 데이터 소스 실시간 표시

---

## 아키텍처

```text
┌──────────────────────────────────────────────────────────────┐
│                     Application Layer                        │
│  Application.cpp  ·  BuildingManager  ·  Game Logic          │
└──────────────────────────────┬───────────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────────┐
│               High-Level Rendering (API-Agnostic)            │
│  Renderer  ·  ShadowRenderer  ·  SkyboxRenderer              │
│  IBLManager  ·  TextureManager  ·  ParticleRenderer          │
└──────────────────────────────┬───────────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────────┐
│              RHI (Render Hardware Interface)                  │
│  RHIDevice  ·  RHISwapchain  ·  RHIBuffer  ·  RHITexture     │
│  RHIPipeline  ·  RHICommandEncoder  ·  RHIBindGroup          │
└──────────┬──────────────────────────────────┬────────────────┘
           │                                  │
┌──────────▼──────────┐            ┌──────────▼──────────┐
│   Vulkan Backend    │            │   WebGPU Backend     │
│  (Desktop/Linux)    │            │  (Web/WASM)          │
│  rhi-vulkan/        │            │  rhi-webgpu/         │
└─────────────────────┘            └─────────────────────┘
```

상위 레이어는 그래픽스 API에 완전히 독립적이며, 백엔드는 `RHIFactory`를 통해 런타임에 선택됩니다.

---

## 프로젝트 구조

```text
finance-city-engine/
├── src/
│   ├── main.cpp
│   ├── Application.cpp/hpp         # 메인 루프, GLFW 윈도우
│   ├── rhi/                        # RHI 추상 인터페이스
│   ├── rhi-vulkan/                 # Vulkan 백엔드 (데스크탑)
│   ├── rhi-webgpu/                 # WebGPU 백엔드 (웹/WASM)
│   ├── rendering/
│   │   ├── Renderer.cpp/hpp        # 렌더링 총괄
│   │   ├── ShadowRenderer.cpp/hpp  # 그림자 맵
│   │   ├── SkyboxRenderer.cpp/hpp  # 스카이박스
│   │   ├── IBLManager.cpp/hpp      # 이미지 기반 조명
│   │   ├── TextureManager.cpp/hpp  # 프로시저럴 텍스처
│   │   └── InstancedRenderData.hpp # 인스턴스 렌더 데이터
│   ├── game/
│   │   └── managers/
│   │       └── BuildingManager.cpp/hpp  # 빌딩 생성·업데이트
│   ├── effects/
│   │   └── ParticleRenderer.cpp/hpp     # 파티클 시스템
│   ├── scene/                      # SceneManager, Mesh, Camera
│   ├── resources/                  # ResourceManager (GPU 버퍼·텍스처)
│   └── utils/
│       └── Vertex.hpp              # Vertex 구조체 (48 bytes)
│
├── shaders/
│   ├── building.wgsl               # PBR + Shadow + 프로시저럴 텍스처
│   ├── shadow.wgsl                 # 그림자 패스 (깊이 전용)
│   ├── frustum_cull.comp.wgsl      # GPU 프러스텀 컬링 컴퓨트
│   ├── skybox.wgsl
│   ├── tonemap.wgsl                # ACES 톤맵 + Bloom 합산
│   ├── fxaa.wgsl
│   └── bloom_*.wgsl                # Bloom 3-패스
│
├── tests/
│   └── wasm_shell.html             # WASM 셸 HTML
├── scripts/
│   └── setup_emscripten.sh         # Emscripten SDK 자동 설치
├── Makefile
└── CMakeLists.txt
```

---

## 빌드 및 실행

### 사전 요구 사항

| 항목 | 버전 | 용도 |
| ------ | ------ | ------ |
| CMake | 3.28+ | 모든 빌드 |
| C++ 컴파일러 | GCC 12+ / Clang 15+ | 모든 빌드 |
| Vulkan SDK | 1.3+ | 데스크탑 빌드 |
| vcpkg | 최신 | 의존성 관리 |
| Emscripten SDK | 3.1.71+ | 웹 빌드 (선택) |

### 데스크탑 빌드 (Vulkan)

```bash
export VCPKG_ROOT=/path/to/vcpkg
export VULKAN_SDK=/path/to/vulkansdk

make build
./build/vulkanGLFW
```

### 웹 빌드 (WebGPU + WASM)

```bash
# Emscripten SDK 자동 설치 (최초 1회)
make setup-emscripten

# WASM 빌드
make wasm

# 로컬 서버 실행 후 브라우저에서 확인
make serve-wasm
# → http://localhost:8000  (Chrome 113+ / Edge 113+ 필요)
```

### Makefile 타겟 전체 목록

```bash
make build              # 데스크탑 빌드
make clean              # 빌드 아티팩트 정리
make setup-emscripten   # Emscripten SDK 설치
make wasm               # WebAssembly 빌드
make serve-wasm         # WASM 로컬 서버 (port 8000)
make clean-wasm         # WASM 빌드 아티팩트 정리
make help               # 전체 타겟 목록
```

---

## 카메라 조작

| 입력 | 동작 |
| ------ | ------ |
| 마우스 드래그 | 카메라 회전 |
| 스크롤 | 줌 인/아웃 |
| WASD | 카메라 이동 |
| ESC | 종료 |

---

## 기술 스택

- **언어**: C++20
- **그래픽스 API**: Vulkan 1.3 (데스크탑), WebGPU (웹)
- **셰이더**: WGSL (WebGPU), GLSL/SPIR-V (Vulkan)
- **수학**: GLM
- **UI**: Dear ImGui (네이티브) / HTML 오버레이 (WASM)
- **윈도우**: GLFW
- **웹 런타임**: Emscripten
- **의존성 관리**: vcpkg

---

## 라이선스

학습 및 포트폴리오 목적으로 제작되었습니다.
사용 시 출처를 표기해 주세요.
