#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
} pc;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out vec3 outColor;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position   = pc.mvp * vec4(inPosition, 1.0);
    outWorldNormal = mat3(pc.model) * inNormal;
    outWorldPos    = worldPos.xyz;
    outColor       = inColor;
}
