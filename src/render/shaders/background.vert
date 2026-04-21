#version 450

layout(location = 0) out float outGradientT;

void main() {
    // Fullscreen triangle via gl_VertexIndex — no VBO needed
    vec2 pos = vec2(
        (gl_VertexIndex == 1) ? 3.0 : -1.0,
        (gl_VertexIndex == 2) ? 3.0 : -1.0
    );
    gl_Position = vec4(pos, 0.0, 1.0);

    // Vulkan native NDC: y=-1 is screen top, y=+1 is screen bottom
    // Map so that T=1 at top and T=0 at bottom for the gradient
    outGradientT = 1.0 - (pos.y * 0.5 + 0.5);
}
