# Finance City Engine — TODO

최종 업데이트: 2026-03-30

---

## 전체 상태 요약

```text
✅ 완료   P1-1   WASM 가격 업데이트 수정
✅ 완료   P2-1   Renderer 레거시 파이프라인 제거
✅ 완료   P2-2   Renderer.cpp Phase 주석 정리
✅ 완료   VIS-1  오프스크린 HDR 렌더 타겟 도입 (RGBA16Float)
✅ 완료   VIS-2  FXAA 도입 (HDR → LDR → FXAA → 스왑체인)
✅ 완료   UI-1   빌딩 레이블 (ticker·가격·변화율) — native ImGui + WASM HTML 오버레이
✅ 완료   UI-2   LIVE/MOCK 데이터 소스 배지
✅ 완료   WS-1   WASM WebSocket 지원 (Emscripten WebSocket API)
✅ 완료   WS-2   WebSocket 파서 유연화 (다양한 서버 형식 허용)
✅ 완료   WS-3   wsProvided 로직 버그 수정 (연결 중 mock 덮어쓰기 방지)
✅ 완료   P2-3   instanceCount + 1 의도 확인 (주석으로 ground plane 명시)
✅ 완료   P2-4   validationLayers 파라미터 제거 (Renderer 생성자 bool만 사용)
✅ 완료   P3-1   ScissorRect.extent → Extent2D (Extent2D 타입 추가)
✅ 완료   P3-2   BuildingEntity.rotation → glm::quat (타입 안전성 확보)
✅ 완료   P3-3   initializeFromConfig stub 제거 (미구현 public 함수 삭제)
✅ 완료   VIS-5  Procedural Building Textures (Phase 1) — 창문 격자 + 이미시브
✅ 완료   VIS-3  Bloom — 3-pass (prefilter → blur H → blur V), tonemap에 합산
✅ 완료   VIS-4  SSAO — height-based AO (building.wgsl; 화면공간 SSAO는 VIS-7 이후 고려)

⏳ 대기   VIS-7  텍스처 & 환경 고도화 전체 (VISUAL_UPGRADE.md 참조)
```

---

## ✅ 완료 항목

### P1-1. WASM 가격 업데이트 수정

`mainLoopFrame()`에 `priceUpdateTimer` 기반 mock 업데이트 추가.
WS 연결 상태에 따른 fallback 구조 확립.

### P2-1. Renderer 레거시 파이프라인 제거

레거시 OBJ 렌더링 파이프라인(`rhiVertexShader`, `rhiPipeline` 등) 및
`createRHIPipeline()`, `loadModel()`, `loadTexture()` 제거.

### P2-2. Renderer.cpp Phase 주석 정리

`// Phase 4.5`, `// Phase 7` 등 이력 주석 13개를 동작 설명 주석으로 교체.

### VIS-1. 오프스크린 HDR 렌더 타겟 (WASM 전용)

```text
기하 렌더 → HDR (RGBA16Float) → 토네맵(ACES+감마) → LDR (RGBA8Unorm) → FXAA → 스왑체인
```

- `hdrColorTexture`, `ldrColorTexture` 추가
- `createHDRRenderTarget()`, `createTonemapPipeline()` 구현
- Vulkan 경로 무변경 (building.frag.glsl 인라인 토네맵 유지)

### VIS-2. FXAA

`shaders/fxaa.wgsl` 작성 (FXAA 3.11 간소화: sub-pixel blend + 12-step edge walk).
LDR 중간 텍스처(RGBA8Unorm)에서 읽어 스왑체인에 씀.
리사이즈 시 LDR 텍스처 재생성 + bind group 갱신.

### UI-1. 빌딩 레이블

- **Native:** ImGui background draw list — `"NVDA  $186  +0.69%"` 텍스트 + 반투명 배경
- **WASM:** `EM_JS` HTML div 오버레이, 매 프레임 3D→2D 투영으로 위치 갱신

### UI-2. LIVE/MOCK 배지

- **Native:** ImGui 우상단 배지 (`● LIVE` 초록 / `● MOCK` 주황)
- **WASM:** `js_set_data_source_badge()` JS 함수로 fixed position div

### WS-1. WASM WebSocket

`WebSocketDataFeed`를 `#ifdef __EMSCRIPTEN__` 분기로 양 플랫폼 지원:

- **Native:** IXWebSocket (배경 스레드, mutex 보호)
- **WASM:** `emscripten/websocket.h` + 4개 콜백 (onOpen/onClose/onMessage/onError)

`CMakeLists.txt`: WASM 소스에 추가, `-lwebsocket.js` 링크, nlohmann_json FetchContent.

### WS-2. WebSocket 파서 유연화

다음 서버 형식 모두 지원:

| 형식 | 설명 |
| --- | --- |
| `{"type":"batch_stock_data","payload":[...]}` | 기존 형식 |
| `{"type":"batch_stock_data","data":[...]}` | `data` 키 변형 |
| `[{"c":"AAPL","p":224,...}]` | 배열 직접 전송 |
| 필드명 `c/p/r/vt/t` 또는 `ticker/price/rate/volume/ts` | 축약·풀네임 허용 |

최초 수신 메시지 300자 로그 출력 (형식 디버깅용).

### WS-3. wsProvided 버그 수정

연결 중이지만 이번 프레임에 배치가 없는 경우에도 mock fallback이 실행되던 버그 수정.
`isConnected() == true`이면 mock 타이머 억제.

---

## ⏳ 대기 항목

### VIS-7. 텍스처 & 환경 고도화 전체

세부 계획: **`VISUAL_UPGRADE.md`** 참조

```text
Phase 2: TextureManager + 버텍스 탄젠트 + ObjectData 확장 + 바인딩 확장
Phase 3: 빌딩 파사드 텍스처 (albedo + normal, 4 타입)
Phase 4: 지면 타일링 + 프로시저럴 도로·차선
Phase 5: 노말맵 TBN 적용
Phase 6: 빌딩 메시 다변화 (Box/SteppedTower/SlimTower/WideSlab)
Phase 7: LOD (거리 기반 텍스처 품질 단계)
```

---

## 장기 로드맵

### 렌더링

- **VIS-6. TAA**: jitter + 히스토리 누적 + 재투영. 모션 벡터 버퍼 필요. FXAA 대비 훨씬 높은 품질.
- **SSR**: IBL 이미 있으므로 로컬 반사 보완
- **Volumetric Fog**: 도시 대기 깊이감
- **GPU-driven Occlusion Culling**: 수천 건물 스케일

### 게임 로직 & 데이터

- 섹터 간 상관관계 시각화 (유사 움직임 종목 간 연결선)
- 시계열 히스토리 (색상 그라디언트로 일간·주간 추세 표현)
- `initializeFromConfig()` 구현 (JSON 설정 파일로 섹터·종목 초기화)

### 성능 & 확장성

- LOD 시스템 (VIS-7 Phase 7)
- 동적 건물 밀도 (변동성 높은 섹터 = 더 많은 건물)

---

## 참고 — 잘 된 부분 (변경 불필요)

- RHI 인터페이스 계층 설계
- 게임 로직 계층 분리 (`WorldManager → BuildingManager → BuildingEntity`)
- `InstancedRenderData`를 통한 로직 ↔ 렌더링 경계
- GPU 프러스텀 컬링 + compute shader 구조
- IBL (irradiance, prefiltered env, BRDF LUT) 이미 구현
- Cook-Torrance PBR + PCF 그림자 이미 구현
- RAII 전반 (스마트 포인터, 소멸 순서 관리)
- WASM/네이티브 분기 전반적으로 잘 처리됨
