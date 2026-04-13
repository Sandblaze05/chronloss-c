#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

out vec2 TexCoord;
out vec3 vNormal;

uniform mat4 MVP;

void main() {
    vNormal = aNormal;
    gl_Position = MVP * vec4(aPos, 1.0);
    TexCoord = aUV; // Pass it to the fragment shader
}