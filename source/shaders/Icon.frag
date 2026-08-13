#version 450
#extension GL_ARB_separate_shader_objects : enable

// 图标栏用的片元着色器：比黑板/课桌那套 LambertBlinn 简单很多，
// 因为这是纯 2D UI，不需要法线、光照，直接采样贴图颜色输出就行
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D iconTex;

void main() {
    outColor = texture(iconTex, fragUV);
}
