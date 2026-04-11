#version 330 core

out vec4 FragColor;

uniform vec3 color;
uniform float ambientStrength;

void main() {
    vec3 ambient = ambientStrength * color;
    FragColor = vec4(ambient, 1.0);
}