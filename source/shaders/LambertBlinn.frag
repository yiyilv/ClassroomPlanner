#version 450
#extension GL_ARB_separate_shader_objects : enable

// ── 输入：从 Vertex Shader 插值过来的数据 ─────────────────────────────
layout(location = 0) in vec3 fragPos;   // 世界坐标系下的像素位置
layout(location = 1) in vec3 fragNorm;  // 世界坐标系下的法线（已经插值）
layout(location = 2) in vec2 fragUV;   // UV 坐标

// ── 输出：这个像素最终的颜色 ───────────────────────────────────────────
layout(location = 0) out vec4 outColor;

// ── Uniform：Local UBO（set=1, binding=0）── 和 Vertex Shader 共享 ────
layout(binding = 0, set = 1) uniform UniformBufferObject {
    mat4 mvpMat;
    mat4 mMat;
    mat4 nMat;
    vec4 specular;  // xyz = 高光颜色, w = shininess
    vec3 ambient;
    vec4 highlight; // xyz = 高亮颜色, w = 强度（0 = 未选中，不影响颜色）
} ubo;

// ── Uniform：纹理采样器（set=1, binding=1）────────────────────────────
layout(binding = 1, set = 1) uniform sampler2D tex;

// ── Uniform：Global UBO（set=0, binding=0）── 灯光和相机全局数据 ──────
layout(binding = 0, set = 0) uniform GlobalUniformBufferObject {
    vec3 lightDir;      // 平行光方向（世界坐标系，光线传播方向，从光源射向场景，使用时需取反）
    vec3 lightColor;    // 光源颜色（RGB）
    vec3 eyePos;        // 相机（观察者）位置（世界坐标系）
    vec3 ambientLight;  // 环境光强度（RGB，通常很小，如 0.05）
} gubo;

void main() {
    // ── 准备向量 ──────────────────────────────────────────────────────
    vec3 N = normalize(fragNorm);               // 法线方向（归一化）
    vec3 V = normalize(gubo.eyePos - fragPos);  // 视线方向（从片元指向相机）
    // 注意这里要取反：gubo.lightDir 在 C++ 侧表示"光线传播的方向"（从太阳射向地面），
    // 而我们在这里需要的是"从片元指向光源"的方向，所以要加负号
    vec3 L = normalize(-gubo.lightDir);         // 光线方向（从片元指向光源）

    // ── 从纹理读取 albedo（漫反射颜色）────────────────────────────────
    vec3 albedo = texture(tex, fragUV).rgb;

    // ── Lambert 漫反射 ────────────────────────────────────────────────
    // 法线和光线方向的点积，表示有多少光照射到这个面
    // max(., 0) 保证背光面不产生负值
    float kD = max(dot(N, L), 0.0);

    // ── Blinn 高光（Specular）─────────────────────────────────────────
    // H 是 V 和 L 的"半程向量"，Blinn 用 H 代替 Phong 的反射向量
    vec3 H = normalize(V + L);
    // ubo.specular.w 是 shininess（光泽度），越大高光越集中
    float kS = pow(max(dot(H, N), 0.0), ubo.specular.w);

    // ── 合成最终颜色 ──────────────────────────────────────────────────
    // = (漫反射 * albedo + 高光 * 高光颜色) * 光颜色 * (1 - 环境光比例)
    //   + 环境光 * 环境光比例 * albedo
    // 为什么这样写？
    // 直接光 (1 - ambientLight) 和 间接光（环境近似）ambientLight 加起来 = 1
    // 这样调节 ambientLight 不会让总亮度超标
    vec3 color = (kD * albedo + kS * ubo.specular.rgb) * gubo.lightColor * (1.0 - gubo.ambientLight)
               + ubo.ambient * gubo.ambientLight * albedo;

    // ── 选中高亮 ──────────────────────────────────────────────────────
    // 这是纯 2D UI 之外的另一种"叠加层"思路：不改变光照计算本身，
    // 而是在算完最终颜色之后，往上面混一层高亮色。
    // ubo.highlight.w 是强度：0 表示没选中，完全不影响颜色；
    // >0 时用 mix() 把颜色往 highlight.rgb（暖黄色）方向拉一部分，
    // 强度是 C++ 那边用 sin() 算出来的"呼吸"值，所以选中的物体会有轻微的明暗跳动，比固定颜色更显眼
    color = mix(color, ubo.highlight.rgb, ubo.highlight.w);

    outColor = vec4(color, 1.0);
}
