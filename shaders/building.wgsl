// Building shader with PBR (Cook-Torrance), shadow mapping, procedural textures, and TBN normal mapping
// WebGPU WGSL version

const PI: f32 = 3.14159265359;

struct UniformBufferObject {
    model: mat4x4<f32>,
    view: mat4x4<f32>,
    proj: mat4x4<f32>,
    sunDirection: vec3<f32>,
    sunIntensity: f32,
    sunColor: vec3<f32>,
    ambientIntensity: f32,
    cameraPos: vec3<f32>,
    exposure: f32,
    // Shadow mapping
    lightSpaceMatrix: mat4x4<f32>,
    shadowMapSize: vec2<f32>,
    shadowBias: f32,
    shadowStrength: f32,
}

@group(0) @binding(0)  var<uniform> ubo: UniformBufferObject;
@group(0) @binding(1)  var shadowMapTex: texture_depth_2d;
@group(0) @binding(2)  var shadowMapSampler: sampler;
// IBL textures
@group(0) @binding(3)  var irradianceMap: texture_cube<f32>;
@group(0) @binding(4)  var prefilteredMap: texture_cube<f32>;
@group(0) @binding(5)  var brdfLUT: texture_2d<f32>;
@group(0) @binding(6)  var iblSampler: sampler;
// Procedural building facade textures (Texture2DArray: 4 types × 256×256)
@group(0) @binding(7)  var buildingAlbedoArray: texture_2d_array<f32>;
@group(0) @binding(8)  var buildingNormalArray:  texture_2d_array<f32>;
@group(0) @binding(11) var anisoSampler: sampler;

// Per-object data via SSBO
struct ObjectData {
    worldMatrix: mat4x4<f32>,
    boundingBoxMin: vec4<f32>,
    boundingBoxMax: vec4<f32>,
    colorAndMetallic: vec4<f32>,   // rgb = albedo tint, a = metallic
    roughnessAOPad: vec4<f32>,     // r = roughness, g = ao, b = numFloors, a = priceRate
    texParams: vec4<f32>,          // r = buildingType (0-3), g = uvScale, ba = reserved
}

struct ObjectBuffer {
    objects: array<ObjectData>,
}

@group(1) @binding(0) var<storage, read> objectBuffer: ObjectBuffer;

// Visible indices from GPU frustum culling
struct VisibleIndicesBuffer {
    indices: array<u32>,
}
@group(1) @binding(1) var<storage, read> visibleIndices: VisibleIndicesBuffer;

// Vertex input (per-vertex)
struct VertexInput {
    @builtin(instance_index) instanceIndex: u32,
    @location(0) position: vec3<f32>,
    @location(1) normal:   vec3<f32>,
    @location(2) texCoord: vec2<f32>,
    @location(3) tangent:  vec4<f32>,  // xyz = tangent, w = bitangent sign
}

struct VertexOutput {
    @builtin(position) position:  vec4<f32>,
    @location(0)  color:          vec3<f32>,    // albedo tint (sRGB)
    @location(1)  normal:         vec3<f32>,    // world-space geometric normal
    @location(2)  worldPos:       vec3<f32>,
    @location(3)  posLightSpace:  vec4<f32>,
    @location(4)  metallic:       f32,
    @location(5)  roughness:      f32,
    @location(6)  ao:             f32,
    @location(7)  texCoord:       vec2<f32>,
    @location(8)  numFloors:      f32,
    @location(9)  priceRate:      f32,
    @location(10) worldTangent:   vec4<f32>,    // xyz = tangent, w = bitangent sign
    @location(11) buildingType:   f32,          // texParams.r (0-3)
}

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // Indirection through visible indices from frustum culling
    let actualIndex = visibleIndices.indices[input.instanceIndex];
    let obj = objectBuffer.objects[actualIndex];
    let numFloors = obj.roughnessAOPad.b;

    // Phase 6: Stepped-tower vertex deformation
    // Tall buildings (> 22 floors ≈ 77m) get a smooth setback on their top 30%,
    // creating a tiered silhouette without a separate mesh.
    var localPos = input.position;
    let stepFraction = clamp((localPos.y - 0.70) / 0.30, 0.0, 1.0);
    let isTower      = select(0.0, 1.0, numFloors > 22.0);
    let stepInset    = stepFraction * isTower * 0.18;
    localPos.x      *= (1.0 - stepInset);
    localPos.z      *= (1.0 - stepInset);

    let worldPos4 = obj.worldMatrix * vec4<f32>(localPos, 1.0);
    let worldPos  = worldPos4.xyz;

    output.position     = ubo.proj * ubo.view * ubo.model * worldPos4;
    output.color        = obj.colorAndMetallic.rgb;
    output.normal       = input.normal;
    output.worldPos     = worldPos;
    output.posLightSpace = ubo.lightSpaceMatrix * worldPos4;
    output.metallic     = obj.colorAndMetallic.a;
    output.roughness    = obj.roughnessAOPad.r;
    output.ao           = obj.roughnessAOPad.g;
    output.texCoord     = input.texCoord;
    output.numFloors    = numFloors;
    output.priceRate    = obj.roughnessAOPad.a;
    output.worldTangent = input.tangent;
    output.buildingType = obj.texParams.r;

    return output;
}

// =============================================================================
// PBR Functions
// =============================================================================

fn distributionGGX(N: vec3<f32>, H: vec3<f32>, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH = max(dot(N, H), 0.0);
    let NdotH2 = NdotH * NdotH;
    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

fn geometrySmith(N: vec3<f32>, V: vec3<f32>, L: vec3<f32>, roughness: f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

fn fresnelSchlick(cosTheta: f32, F0: vec3<f32>) -> vec3<f32> {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn fresnelSchlickRoughness(cosTheta: f32, F0: vec3<f32>, roughness: f32) -> vec3<f32> {
    return F0 + (max(vec3<f32>(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// =============================================================================
// Shadow Calculation
// =============================================================================

fn calculateShadow(posLightSpace: vec4<f32>, normal: vec3<f32>, lightDir: vec3<f32>) -> f32 {
    var projCoords = posLightSpace.xyz / posLightSpace.w;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = (-projCoords.y) * 0.5 + 0.5;

    // Slope-scaled bias: slightly larger at grazing angles to prevent acne
    let cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
    let bias = ubo.shadowBias * mix(2.0, 1.0, cosTheta);
    let currentDepth = projCoords.z;
    let clampedCoords = clamp(projCoords.xy, vec2<f32>(0.0), vec2<f32>(1.0));
    let texelSize = 1.0 / ubo.shadowMapSize;

    var shadow: f32 = 0.0;
    let d_m1_m1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(-1.0, -1.0) * texelSize);
    let d_0_m1  = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>( 0.0, -1.0) * texelSize);
    let d_p1_m1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>( 1.0, -1.0) * texelSize);
    let d_m1_0  = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(-1.0,  0.0) * texelSize);
    let d_0_0   = textureSample(shadowMapTex, shadowMapSampler, clampedCoords);
    let d_p1_0  = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>( 1.0,  0.0) * texelSize);
    let d_m1_p1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(-1.0,  1.0) * texelSize);
    let d_0_p1  = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>( 0.0,  1.0) * texelSize);
    let d_p1_p1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>( 1.0,  1.0) * texelSize);

    let compDepth = currentDepth - bias;
    shadow += select(0.0, 1.0, compDepth > d_m1_m1);
    shadow += select(0.0, 1.0, compDepth > d_0_m1);
    shadow += select(0.0, 1.0, compDepth > d_p1_m1);
    shadow += select(0.0, 1.0, compDepth > d_m1_0);
    shadow += select(0.0, 1.0, compDepth > d_0_0);
    shadow += select(0.0, 1.0, compDepth > d_p1_0);
    shadow += select(0.0, 1.0, compDepth > d_m1_p1);
    shadow += select(0.0, 1.0, compDepth > d_0_p1);
    shadow += select(0.0, 1.0, compDepth > d_p1_p1);
    shadow /= 9.0;

    let outsideFrustum = projCoords.z > 1.0 || projCoords.z < 0.0 ||
                         projCoords.x < 0.0 || projCoords.x > 1.0 ||
                         projCoords.y < 0.0 || projCoords.y > 1.0;

    return select(shadow * ubo.shadowStrength, 0.0, outsideFrustum);
}

// =============================================================================
// Main
// =============================================================================

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let ao = input.ao;
    let N_geom = normalize(input.normal);
    let V      = normalize(ubo.cameraPos - input.worldPos);

    // -------------------------------------------------------------------------
    // Phase 7: Distance-based LOD
    // At > 300m: skip normal map + window emissive (expensive per-pixel work)
    // -------------------------------------------------------------------------
    let distToCam = length(input.worldPos - ubo.cameraPos);
    let isClose   = distToCam < 300.0;

    // -------------------------------------------------------------------------
    // Height-based ambient occlusion (darkens building bases near ground)
    // -------------------------------------------------------------------------
    let heightAO    = clamp(input.worldPos.y / 8.0, 0.0, 1.0);
    let effectiveAO = ao * mix(0.2, 1.0, heightAO);

    // -------------------------------------------------------------------------
    // Detect ground plane (numFloors == 0)
    // -------------------------------------------------------------------------
    let isGround = input.numFloors < 0.5;

    // -------------------------------------------------------------------------
    // Texture array sampling
    // layer 0 = concrete, 1 = glass tower, 2 = brick, 3 = modern office
    // Clamp to valid [0,3] so the ground (type=4) uses layer 3 as fallback.
    // -------------------------------------------------------------------------
    let layerIdx = i32(clamp(input.buildingType, 0.0, 3.0));
    let uv       = input.texCoord;

    // Scale UV by face height (vertical faces tile once per ≈10m, horizontal once per full span)
    let isVertical = abs(N_geom.y) < 0.5;
    let uvScaleH   = select(1.0, 4.0, isVertical);  // vertical faces: 4× along Y (tighter tiles)
    let scaledUV   = vec2<f32>(uv.x * 2.0, uv.y * uvScaleH);

    // Sample albedo texture; blend with game-data tint (65% texture + 35% tint)
    // If texture returns zero the tint still contributes, so buildings always show color.
    let texAlbedoLinear = textureSample(buildingAlbedoArray, anisoSampler, scaledUV, layerIdx).rgb;
    let tintLinear      = pow(input.color, vec3<f32>(2.2));
    let albedoBase      = mix(texAlbedoLinear, tintLinear, 0.35);

    // -------------------------------------------------------------------------
    // TBN normal mapping (LOD-gated: only when close enough)
    // -------------------------------------------------------------------------
    var N = N_geom;
    if (isClose && isVertical) {
        let T   = normalize(input.worldTangent.xyz);
        let B   = cross(N_geom, T) * input.worldTangent.w;
        let TBN = mat3x3<f32>(T, B, N_geom);

        let rawN    = textureSampleLevel(buildingNormalArray, anisoSampler, scaledUV, layerIdx, 0.0).rgb;
        let tangentN = rawN * 2.0 - vec3<f32>(1.0);
        N = normalize(TBN * tangentN);
    }

    // -------------------------------------------------------------------------
    // Phase 4: Procedural road grid (ground plane only)
    // -------------------------------------------------------------------------
    let blockSize = 90.0;
    let roadWidth = 12.0;
    let halfRoad  = roadWidth / (2.0 * blockSize);
    let bFrac     = fract(input.worldPos.xz / blockSize);

    let isRoadV = step(bFrac.x, roadWidth / blockSize);
    let isRoadH = step(bFrac.y, roadWidth / blockSize);
    let isRoad  = max(isRoadV, isRoadH);

    let dashFracZ = fract(input.worldPos.z / 6.0);
    let dashFracX = fract(input.worldPos.x / 6.0);
    let isDashV   = step(0.3, dashFracZ) * step(dashFracZ, 0.7);
    let isDashH   = step(0.3, dashFracX) * step(dashFracX, 0.7);
    let isCenterV = step(abs(bFrac.x - halfRoad), 0.007) * isDashV * isRoadV;
    let isCenterH = step(abs(bFrac.y - halfRoad), 0.007) * isDashH * isRoadH;
    let isCenterLine = max(isCenterV, isCenterH);

    let asphaltLinear = pow(vec3<f32>(0.22, 0.22, 0.26), vec3<f32>(2.2));
    let groundRoadMix = select(0.0, isRoad, isGround);
    let albedo        = mix(albedoBase, asphaltLinear, groundRoadMix);
    let roadEmissive  = vec3<f32>(0.55, 0.50, 0.0) * isCenterLine * select(0.0, 1.0, isGround);

    // -------------------------------------------------------------------------
    // Procedural window grid (vertical faces, LOD-gated, buildings only)
    // -------------------------------------------------------------------------
    let hasWindows = input.numFloors > 0.5 && isVertical && isClose;

    let fyf    = fract(uv.y * input.numFloors);
    let fxf    = fract(uv.x * 4.0);
    let wMaskY = step(0.1, fyf) * step(fyf, 0.85);
    let wMaskX = step(0.1, fxf) * step(fxf, 0.85);
    let isWindow = select(0.0, wMaskX * wMaskY, hasWindows);

    // Window overrides surface material (glass is smooth + metallic)
    let metallic  = mix(input.metallic, 0.9,  isWindow);
    let roughness = mix(0.8,            0.05, isWindow);

    // Window emissive proportional to price surge; towers glow brighter
    let towerBoost     = select(1.0, 1.6, input.numFloors > 22.0);
    let windowEmissive = vec3<f32>(0.2, 0.45, 1.0) * isWindow
                       * max(0.0, input.priceRate * 5.0) * towerBoost;

    let emissive = windowEmissive + roadEmissive;

    // -------------------------------------------------------------------------
    // PBR Lighting
    // -------------------------------------------------------------------------
    let L     = normalize(ubo.sunDirection);
    let H     = normalize(V + L);
    let NdotL = max(dot(N, L), 0.0);
    let NdotV = max(dot(N, V), 0.0);

    let F0 = mix(vec3<f32>(0.04), albedo, metallic);

    let D = distributionGGX(N, H, roughness);
    let G = geometrySmith(N, V, L, roughness);
    let F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    let numerator   = D * G * F;
    let denominator = 4.0 * NdotV * NdotL + 0.0001;
    let specular    = numerator / denominator;

    let kS = F;
    let kD = (vec3<f32>(1.0) - kS) * (1.0 - metallic);

    let radiance = ubo.sunColor * ubo.sunIntensity;
    let Lo       = (kD * albedo / PI + specular) * radiance * NdotL;

    // IBL Ambient
    let F_ibl   = fresnelSchlickRoughness(NdotV, F0, roughness);
    let kS_ibl  = F_ibl;
    let kD_ibl  = (1.0 - kS_ibl) * (1.0 - metallic);

    let irradiance       = textureSample(irradianceMap, iblSampler, N).rgb;
    let diffuseIBL       = irradiance * albedo;

    let MAX_REFLECTION_LOD: f32 = 4.0;
    let R                = reflect(-V, N);
    let prefilteredColor = textureSampleLevel(prefilteredMap, iblSampler, R, roughness * MAX_REFLECTION_LOD).rgb;
    let brdf             = textureSample(brdfLUT, iblSampler, vec2<f32>(NdotV, roughness)).rg;
    let specularIBL      = prefilteredColor * (F_ibl * brdf.x + brdf.y);

    let iblStrength     = max(irradiance.r, max(irradiance.g, irradiance.b));
    let iblAmbient      = (kD_ibl * diffuseIBL + specularIBL) * effectiveAO;
    let fallbackAmbient = albedo * effectiveAO;
    let ambient         = mix(fallbackAmbient, iblAmbient, step(0.001, iblStrength)) * ubo.ambientIntensity;

    let shadow = calculateShadow(input.posLightSpace, N, L);

    var color = ambient + (1.0 - shadow) * Lo + emissive;

    // Apply exposure (tonemapping happens in the tonemap pass)
    let exp = select(ubo.exposure, 1.0, ubo.exposure <= 0.0);
    color   = color * exp;

    return vec4<f32>(color, 1.0);
}
