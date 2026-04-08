#version 450

// Infinite ground grid via full-screen triangle trick
const vec2 kPositions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

layout(push_constant) uniform PushConstants {
    mat4 invViewProj;
    vec3 cameraPos;
    float pad;
} pc;

layout(location = 0) out vec3 outNearPoint;
layout(location = 1) out vec3 outFarPoint;

vec3 unproject(vec2 ndc, float depth) {
    vec4 clip = vec4(ndc, depth, 1.0);
    vec4 world = pc.invViewProj * clip;
    return world.xyz / world.w;
}

void main() {
    vec2 p = kPositions[gl_VertexIndex];
    gl_Position  = vec4(p, 0.0, 1.0);
    outNearPoint = unproject(p, 0.0);
    outFarPoint  = unproject(p, 1.0);
}
