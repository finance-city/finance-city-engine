#version 450

layout(location = 0) in vec3 fragRayDir;

layout(binding = 0) uniform UniformBufferObject {
    mat4 invViewProj;
    vec3 sunDirection;
    float time;
} ubo;

layout(location = 0) out vec4 outColor;

// Procedural sky: gradient + sun disk, sunset-toned
vec3 proceduralSky(vec3 rayDir, vec3 sunDir) {
    vec3 dir = normalize(rayDir);
    float elev = dir.y;

    // Sky gradient: deep blue at zenith → warm orange at horizon → dark ground
    vec3 zenith  = vec3(0.05, 0.15, 0.40);
    vec3 horizon = vec3(0.60, 0.40, 0.25);
    vec3 ground  = vec3(0.10, 0.08, 0.05);

    vec3 sky;
    if (elev > 0.0)
        sky = mix(horizon, zenith, sqrt(elev));
    else
        sky = mix(horizon, ground, clamp(-elev * 4.0, 0.0, 1.0));

    // Sun disk + glow
    float s = max(0.0, dot(dir, normalize(sunDir)));
    sky += vec3(1.0, 0.95, 0.8) * (pow(s, 512.0) + pow(s, 8.0) * 0.5);

    // Horizon haze (warm tones to match sunset palette)
    float hazeAmount = 1.0 - pow(abs(elev), 0.3);
    vec3 hazeColor = vec3(0.65, 0.50, 0.35);
    sky = mix(sky, hazeColor, hazeAmount * 0.3);

    return sky;
}

void main() {
    vec3 color = proceduralSky(fragRayDir, ubo.sunDirection);
    outColor = vec4(color, 1.0);
}
