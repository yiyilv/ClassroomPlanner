#version 450
#extension GL_ARB_separate_shader_objects : enable

// Get Input  from the vertex shader
layout(location = 0) in vec3 fragPos;   // Fragment position in world space
layout(location = 1) in vec3 fragNorm;  // Interpolated normal in world space
layout(location = 2) in vec2 fragUV;    // Texture coordinates

// Final fragment color
layout(location = 0) out vec4 outColor;


// Get local object data
layout(binding = 0, set = 1) uniform UniformBufferObject {
    mat4 mvpMat;
    mat4 mMat;
    mat4 nMat;
    vec4 specular;   // xyz: specular color, w: shininess
    vec3 ambient;    // Ambient material coefficient
    vec4 highlight;  // xyz: highlight color, w: highlight intensity
} ubo;


// Get texture
layout(binding = 1, set = 1) uniform sampler2D tex;


// Get global data
layout(binding = 0, set = 0) uniform GlobalUniformBufferObject {
    vec3 lightDir;      // Direction in which the directional light travels
    vec3 lightColor;    // Light color
    vec3 eyePos;        // Camera position in world space
    vec3 ambientLight;  // Ambient light intensity
} gubo;


void main() {

    // Prepare the vectors used for lighting calculations
    vec3 N = normalize(fragNorm);               // Surface normal
    vec3 V = normalize(gubo.eyePos - fragPos);  // Direction from fragment to camera
    vec3 L = normalize(-gubo.lightDir);          // Direction from fragment toward the light


    // Read the base surface color from the texture
    vec3 albedo = texture(tex, fragUV).rgb;

    // Lambert diffuse lighting:
    float kD = max(dot(N, L), 0.0);
    // Blinn specular
    vec3 H = normalize(V + L);
    // Larger shininess values produce a smaller and sharper highlight
    float kS = pow(max(dot(H, N), 0.0), ubo.specular.w);

    // Combine diffuse, specular, and ambient lighting
    vec3 color =
    (kD * albedo + kS * ubo.specular.rgb)
    * gubo.lightColor
    * (1.0 - gubo.ambientLight)
    + ubo.ambient * gubo.ambientLight * albedo;


    //  mix with selected highlight
    color = mix(color, ubo.highlight.rgb, ubo.highlight.w);


    outColor = vec4(color, 1.0);
}