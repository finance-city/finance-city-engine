// Building shader with PBR (Cook-Torrance) and shadow mapping
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

@group(0) @binding(0) var<uniform> ubo: UniformBufferObject;
@group(0) @binding(1) var shadowMapTex: texture_depth_2d;
@group(0) @binding(2) var shadowMapSampler: sampler;
// IBL textures
@group(0) @binding(3) var irradianceMap: texture_cube<f32>;
@group(0) @binding(4) var prefilteredMap: texture_cube<f32>;
@group(0) @binding(5) var brdfLUT: texture_2d<f32>;
@group(0) @binding(6) var iblSampler: sampler;

// Phase 2.1: Per-object data via SSBO
struct ObjectData {
    worldMatrix: mat4x4<f32>,
    boundingBoxMin: vec4<f32>,
    boundingBoxMax: vec4<f32>,
    colorAndMetallic: vec4<f32>,   // rgb = albedo, a = metallic
    roughnessAOPad: vec4<f32>,     // r = roughness, g = ao
}

struct ObjectBuffer {
    objects: array<ObjectData>,
}

@group(1) @binding(0) var<storage, read> objectBuffer: ObjectBuffer;

// Phase 2.2: Visible indices from GPU frustum culling
struct VisibleIndicesBuffer {
    indices: array<u32>,
}
@group(1) @binding(1) var<storage, read> visibleIndices: VisibleIndicesBuffer;

// Vertex input (per-vertex only)
struct VertexInput {
    @builtin(instance_index) instanceIndex: u32,
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
}

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) worldPos: vec3<f32>,
    @location(3) posLightSpace: vec4<f32>,
    @location(4) metallic: f32,
    @location(5) roughness: f32,
    @location(6) ao: f32,
    @location(7) texCoord: vec2<f32>,
    @location(8) numFloors: f32,   // roughnessAOPad.b — number of floors for window grid
    @location(9) priceRate: f32,   // roughnessAOPad.a — price change ratio for emissive
}

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // Phase 2.2: Indirection through visible indices from frustum culling
    let actualIndex = visibleIndices.indices[input.instanceIndex];
    let obj = objectBuffer.objects[actualIndex];
    let worldPos4 = obj.worldMatrix * vec4<f32>(input.position, 1.0);
    let worldPos = worldPos4.xyz;

    output.position = ubo.proj * ubo.view * ubo.model * worldPos4;
    output.color = obj.colorAndMetallic.rgb;
    output.normal = input.normal;
    output.worldPos = worldPos;
    output.posLightSpace = ubo.lightSpaceMatrix * worldPos4;
    output.metallic = obj.colorAndMetallic.a;
    output.roughness = obj.roughnessAOPad.r;
    output.ao = obj.roughnessAOPad.g;
    output.texCoord = input.texCoord;
    output.numFloors = obj.roughnessAOPad.b;
    output.priceRate = obj.roughnessAOPad.a;

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

    let bias = ubo.shadowBias * 0.01;
    let currentDepth = projCoords.z;
    let clampedCoords = clamp(projCoords.xy, vec2<f32>(0.0), vec2<f32>(1.0));
    let texelSize = 1.0 / ubo.shadowMapSize;

    var shadow: f32 = 0.0;

    // Unrolled PCF 3x3
    let d_m1_m1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(-1.0, -1.0) * texelSize);
    let d_0_m1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(0.0, -1.0) * texelSize);
    let d_p1_m1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(1.0, -1.0) * texelSize);
    let d_m1_0 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(-1.0, 0.0) * texelSize);
    let d_0_0 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords);
    let d_p1_0 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(1.0, 0.0) * texelSize);
    let d_m1_p1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(-1.0, 1.0) * texelSize);
    let d_0_p1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(0.0, 1.0) * texelSize);
    let d_p1_p1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(1.0, 1.0) * texelSize);

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
    // sRGB to linear conversion for albedo
    let albedo = pow(input.color, vec3<f32>(2.2));
    let ao = input.ao;

    let N = normalize(input.normal);
    let V = normalize(ubo.cameraPos - input.worldPos);

    // -------------------------------------------------------------------------
    // Height-based ambient occlusion (darkens building bases near ground)
    // Replaces the flat ao=1.0 from SSBO with a physically-motivated fade:
    //   y=0 (ground level) → ao * 0.2  (heavily occluded)
    //   y≥8m              → ao * 1.0  (fully lit)
    // -------------------------------------------------------------------------
    let heightAO    = clamp(input.worldPos.y / 8.0, 0.0, 1.0);
    let effectiveAO = ao * mix(0.2, 1.0, heightAO);

    // -------------------------------------------------------------------------
    // Procedural window grid (vertical faces only, buildings with numFloors > 0)
    // -------------------------------------------------------------------------
    let uv = input.texCoord;
    let isVerticalFace = abs(N.y) < 0.5;
    let hasWindows = input.numFloors > 0.5 && isVerticalFace;

    // Per-window-cell UV mask: 4 columns, numFloors rows; 10% border on each side
    let fyf = fract(uv.y * input.numFloors);
    let fxf = fract(uv.x * 4.0);
    let wMaskY = step(0.1, fyf) * step(fyf, 0.85);
    let wMaskX = step(0.1, fxf) * step(fxf, 0.85);
    let isWindow = select(0.0, wMaskX * wMaskY, hasWindows);

    // Material: concrete wall vs glass window
    let metallic  = mix(input.metallic, 0.9,  isWindow);
    let roughness = mix(0.8,            0.05, isWindow);

    // Emissive: windows glow blue-white proportional to positive price surge
    let emissive = vec3<f32>(0.2, 0.45, 1.0) * isWindow * max(0.0, input.priceRate * 5.0);
    let L = normalize(ubo.sunDirection);
    let H = normalize(V + L);

    let NdotL = max(dot(N, L), 0.0);
    let NdotV = max(dot(N, V), 0.0);

    // Base reflectivity
    let F0 = mix(vec3<f32>(0.04), albedo, metallic);

    // Cook-Torrance BRDF
    let D = distributionGGX(N, H, roughness);
    let G = geometrySmith(N, V, L, roughness);
    let F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    let numerator = D * G * F;
    let denominator = 4.0 * NdotV * NdotL + 0.0001;
    let specular = numerator / denominator;

    // Energy conservation
    let kS = F;
    let kD = (vec3<f32>(1.0) - kS) * (1.0 - metallic);

    // Direct lighting (sun)
    let radiance = ubo.sunColor * ubo.sunIntensity;
    let Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // IBL Ambient Lighting
    let F_ibl = fresnelSchlickRoughness(NdotV, F0, roughness);
    let kS_ibl = F_ibl;
    let kD_ibl = (1.0 - kS_ibl) * (1.0 - metallic);

    // Diffuse IBL: irradiance map lookup
    let irradiance = textureSample(irradianceMap, iblSampler, N).rgb;
    let diffuseIBL = irradiance * albedo;

    // Specular IBL: prefiltered env map + BRDF LUT
    let MAX_REFLECTION_LOD: f32 = 4.0;
    let R = reflect(-V, N);
    let prefilteredColor = textureSampleLevel(prefilteredMap, iblSampler, R, roughness * MAX_REFLECTION_LOD).rgb;
    let brdf = textureSample(brdfLUT, iblSampler, vec2<f32>(NdotV, roughness)).rg;
    let specularIBL = prefilteredColor * (F_ibl * brdf.x + brdf.y);

    // Fallback: when no HDR environment is loaded, irradiance cubemap is black
    let iblStrength = max(irradiance.r, max(irradiance.g, irradiance.b));
    let iblAmbient = (kD_ibl * diffuseIBL + specularIBL) * effectiveAO;
    let fallbackAmbient = albedo * effectiveAO;
    let ambient = mix(fallbackAmbient, iblAmbient, step(0.001, iblStrength)) * ubo.ambientIntensity;

    // Shadow
    let shadow = calculateShadow(input.posLightSpace, N, L);

    // Final color (emissive added before exposure so it tones with the scene)
    var color = ambient + (1.0 - shadow) * Lo + emissive;

    // Apply exposure — tonemapping and gamma correction happen in the tonemap pass
    let exp = select(ubo.exposure, 1.0, ubo.exposure <= 0.0);
    color = color * exp;

    return vec4<f32>(color, 1.0);
}
