#version 450

layout(location = 0) in  vec3 inNormal;
layout(location = 1) in  vec4 inColor;
layout(location = 0) out vec4 outColor;

void main() {
    // Simple diffuse tint for preview
    vec3 lightDir = normalize(vec3(1.0, 2.0, 3.0));
    float diff = max(dot(normalize(inNormal), lightDir), 0.2);
    outColor = vec4(inColor.rgb * diff, inColor.a);
}
