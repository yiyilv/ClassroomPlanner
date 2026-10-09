#version 450
#extension GL_ARB_separate_shader_objects : enable

// 1. Get Vertex input attributes
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNorm;
layout(location = 2) in vec2 inUV;

// 2. prepare what outputs to the fragment shader
layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNorm;
layout(location = 2) out vec2 fragUV;

// 3. Get from Local Uniform Buffer Object
layout(binding = 0, set = 1) uniform UniformBufferObject {
    mat4 mvpMat;
    mat4 mMat;
    mat4 nMat;
    vec4 specular;
    vec3 ambient;
} ubo;

void main() {
    // 4. Transform the vertex from local space to clip space
    gl_Position = ubo.mvpMat * vec4(inPos, 1.0);
    // fragment shader uses this information for lighting calculations
    fragPos = (ubo.mMat * vec4(inPos, 1.0)).xyz;
    fragNorm = mat3(ubo.nMat) * inNorm;
    fragUV = inUV;
}
