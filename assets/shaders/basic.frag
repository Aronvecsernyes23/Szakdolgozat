#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3 uColor;
uniform bool uUnlit;
uniform vec3 uLightDir = normalize(vec3(-0.4, -1.0, -0.3));

out vec4 FragColor;

void main()
{
    vec3 n = normalize(vNormal);
    float diff = uUnlit ? 1.0 : max(dot(n, -uLightDir), 0.15);
    vec3 color = uColor * diff;
    FragColor = vec4(color, 1.0);
}
