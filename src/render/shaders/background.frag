#version 450

layout(location = 0) in  float inGradientT;
layout(location = 0) out vec4  outColor;

// Studio backdrop: dark near-black at bottom, dark steel-blue at top
const vec3 kTop    = vec3(0.16, 0.18, 0.23);
const vec3 kBottom = vec3(0.05, 0.05, 0.07);

void main() {
    outColor = vec4(mix(kBottom, kTop, inGradientT), 1.0);
}
