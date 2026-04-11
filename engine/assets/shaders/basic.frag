#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform vec3 color;
uniform float ambientStrength;

void main() {
    // Calculate how close we are to the nearest edge (0.0 or 1.0)
    float edgeX = min(TexCoord.x, 1.0 - TexCoord.x);
    float edgeY = min(TexCoord.y, 1.0 - TexCoord.y);
    float minEdge = min(edgeX, edgeY);

    vec3 finalColor = color * ambientStrength;

    // If we are within 0.03 units of the edge, draw black!
    // (Increase 0.03 for a thicker border, decrease for thinner)
    if (minEdge < 0.03) {
        finalColor = vec3(0.0, 0.0, 0.0);
    }

    FragColor = vec4(finalColor, 1.0);
}