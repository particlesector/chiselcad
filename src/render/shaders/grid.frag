#version 450

layout(location = 0) in  vec3 inNearPoint;
layout(location = 1) in  vec3 inFarPoint;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 invViewProj;
    vec3 cameraPos;
    float pad;
} pc;

float gridLine(vec2 coord, float scale) {
    vec2 grid = abs(fract(coord / scale - 0.5) - 0.5) / fwidth(coord / scale);
    return 1.0 - min(min(grid.x, grid.y), 1.0);
}

void main() {
    // Intersect ray with y=0 plane
    float t = -inNearPoint.y / (inFarPoint.y - inNearPoint.y);
    if (t < 0.0) discard;

    vec3 hit = inNearPoint + t * (inFarPoint - inNearPoint);

    float g1  = gridLine(hit.xz, 1.0);
    float g10 = gridLine(hit.xz, 10.0);
    float line = max(g1 * 0.3, g10 * 0.7);

    // Fade with distance
    float dist  = length(hit - pc.cameraPos);
    float alpha = line * (1.0 - smoothstep(40.0, 80.0, dist));
    if (alpha < 0.01) discard;

    outColor = vec4(vec3(0.5), alpha);
}
