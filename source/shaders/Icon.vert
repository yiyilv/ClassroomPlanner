#version 450
#extension GL_ARB_separate_shader_objects : enable

// 图标栏的顶点：位置已经是屏幕 NDC 坐标了（跟 TextMaker 画字的做法一样），
// 不需要任何 View/Projection 矩阵——这是纯 2D 的屏幕叠加层，不参与 3D 场景的相机变换
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

void main() {
    gl_Position = vec4(inPos, 0.0, 1.0);
    fragUV = inUV;
}
