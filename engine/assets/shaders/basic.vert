#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV; // We are adding this!

out vec2 TexCoord;
uniform mat4 MVP;

void main() {
    gl_Position = MVP * vec4(aPos, 1.0);
    TexCoord = aUV; // Pass it to the fragment shader
}