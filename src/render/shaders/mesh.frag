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
const vec3  kLight1Dir = normalize(vec3( 1.0,  1.5,  1.0));
const vec3  kLight1Col = vec3(1.00, 0.97, 0.92);
const float kDiff1     = 0.65;

// Fill light: left-mid, slightly cool
const vec3  kLight2Dir = normalize(vec3(-2.0,  1.0,  1.6));
const vec3  kLight2Col = vec3(0.80, 0.88, 1.00);
const float kDiff2     = 0.20;

// Back light: from behind and above — adds depth as the model rotates
const vec3  kLight3Dir = normalize(vec3( 0.2,  1.0,  0.6));
const vec3  kLight3Col = vec3(0.65, 0.75, 1.00);
const float kDiff3     = 0.22;

// Rim light — camera-relative fresnel glow at silhouette edges
const vec3  kRimCol    = vec3(0.90, 0.95, 1.00);
const float kRimStr    = 0.55;

// Ambient
const float kAmbient   = 0.08;

// Specular (Blinn-Phong, key light only)
const float kSpecStr   = 0.30;
const float kSpecPow   = 64.0;

// ACES filmic tone mapping (Hill approximation)
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 N = normalize(inWorldNormal);
    vec3 V = normalize(pc.eyePos.xyz - inWorldPos);

    float d1 = max(dot(N,  kLight1Dir), 0.0);
    float d2 = max(dot(N,  kLight2Dir), 0.0);
    float d3 = max(dot(N,  kLight3Dir), 0.0);

    // Blinn-Phong specular on key light
    vec3  H    = normalize(kLight1Dir + V);
    float spec = pow(max(dot(N, H), 0.0), kSpecPow) * kSpecStr * d1;

    // Rim: pow(1-NdotV) peaks at silhouette edges, follows camera automatically
    float rim  = pow(1.0 - max(dot(N, V), 0.0), 4.0) * kRimStr;

    vec3 color = kBase * (kAmbient
                 + d1 * kDiff1 * kLight1Col
                 + d2 * kDiff2 * kLight2Col
                 + d3 * kDiff3 * kLight3Col)
                 + vec3(spec)
                 + kRimCol * rim;

    outColor = vec4(aces(color), 1.0);
}
