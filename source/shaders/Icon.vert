#version 450
#extension GL_ARB_separate_shader_objects : enable

//Input data for each 2D icon vertex
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

void main() {
    //Use the 2D NDC position directly as the final clip-space position
    gl_Position = vec4(inPos, 0.0, 1.0);
    fragUV = inUV;
}
