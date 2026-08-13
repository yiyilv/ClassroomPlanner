#version 450
#extension GL_ARB_separate_shader_objects : enable

// ── 输入：每个顶点的属性（来自 VD.init() 定义的格式）──────────────────
// location 0: 顶点位置（和之前的 skeleton 一样）
layout(location = 0) in vec3 inPos;
// location 1: 顶点法线（新增！之前 skeleton 没有法线）
layout(location = 1) in vec3 inNorm;
// location 2: UV 纹理坐标
layout(location = 2) in vec2 inUV;

// ── 输出：传给 Fragment Shader 的插值数据 ─────────────────────────────
layout(location = 0) out vec3 fragPos;   // 世界坐标系下的位置（给光照计算用）
layout(location = 1) out vec3 fragNorm;  // 世界坐标系下的法线
layout(location = 2) out vec2 fragUV;   // UV 坐标（给纹理采样用）

// ── Uniform：从 C++ 传来的矩阵和材质参数（set=1, binding=0）─────────────
// 这是 LOCAL UBO：每个物体独立的数据
layout(binding = 0, set = 1) uniform UniformBufferObject {
    mat4 mvpMat;   // Model * View * Projection：把顶点变换到屏幕空间
    mat4 mMat;     // Model Matrix（世界矩阵）：把顶点变换到世界坐标系
    mat4 nMat;     // Normal Matrix = inverse(transpose(mMat))：变换法线用
    vec4 specular; // xyz = 高光颜色, w = shininess 系数
    vec3 ambient;  // 该物体的环境光系数（per-object）
} ubo;

void main() {
    // 把顶点位置变换到裁剪空间（输出到屏幕）
    gl_Position = ubo.mvpMat * vec4(inPos, 1.0);

    // 把顶点位置变换到世界坐标系（Fragment Shader 需要用来算光照）
    fragPos = (ubo.mMat * vec4(inPos, 1.0)).xyz;

    // 把法线变换到世界坐标系
    // 为什么用 nMat 而不是 mMat？
    // 因为法线方向在非均匀缩放时会被破坏，必须用 inverse(transpose(mMat)) 来修正
    fragNorm = mat3(ubo.nMat) * inNorm;

    // UV 坐标直接透传，不需要变换
    fragUV = inUV;
}
