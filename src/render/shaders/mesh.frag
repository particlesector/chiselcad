#version 450

layout(location = 0) in  vec3 inWorldNormal;
layout(location = 1) in  vec3 inWorldPos;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushFrag {
    layout(offset = 128) vec4 eyePos;
} pc;

// Base color — slightly cool neutral for CAD geometry
const vec3  kBase      = vec3(0.74, 0.74, 0.78);

// Key light: upper-right-front, warm white
const vec3  kLight1Dir = vec3(0.4851, 0.7276, 0.4851); // normalize(1,1.5,1)
const vec3  kLight1Col = vec3(1.00, 0.98, 0.95);
const float kDiff1     = 0.65;

// Fill light: left-mid-front, slightly cool
const vec3  kLight2Dir = vec3(-0.7273, 0.3636, 0.5819); // normalize(-2,1,1.6)
const vec3  kLight2Col = vec3(0.85, 0.90, 1.00);
const float kDiff2     = 0.20;

// Ambient
const float kAmbient   = 0.10;

// Specular (Blinn-Phong, key light only)
const float kSpecStr   = 0.25;
const float kSpecPow   = 48.0;

void main() {
    vec3 N = normalize(inWorldNormal);
    vec3 V = normalize(pc.eyePos.xyz - inWorldPos);

    float d1 = max(dot(N, kLight1Dir), 0.0);
    float d2 = max(dot(N, kLight2Dir), 0.0);

    vec3 H    = normalize(kLight1Dir + V);
    float spec = pow(max(dot(N, H), 0.0), kSpecPow) * kSpecStr * d1;

    vec3 color = kBase * (kAmbient
                 + d1 * kDiff1 * kLight1Col
                 + d2 * kDiff2 * kLight2Col)
                 + vec3(spec);

    outColor = vec4(color, 1.0);
}
