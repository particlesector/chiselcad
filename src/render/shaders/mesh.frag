#version 450

layout(location = 0) in  vec3 inWorldNormal;
layout(location = 1) in  vec3 inWorldPos;
layout(location = 0) out vec4 outColor;

const vec3  kBaseColor   = vec3(0.72, 0.72, 0.72);
const vec3  kLightDir    = vec3(0.577, 0.577, 0.577);
const vec3  kLightColor  = vec3(1.0);
const float kAmbient     = 0.15;

void main() {
    vec3 N    = normalize(inWorldNormal);
    float NdL = max(dot(N, kLightDir), 0.0);
    vec3 col  = kBaseColor * (kAmbient + NdL * kLightColor);
    outColor  = vec4(col, 1.0);
}
