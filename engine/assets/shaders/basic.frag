#version 330 core
in vec3 aNormal;
in vec3 vNormal;
in vec2 TexCoord;
out vec4 FragColor;

uniform vec3 color;
uniform float ambientStrength;

void main() {
    vec3 n = normalize(vNormal);

    float shade;
    if (n.y > 0.5) shade = 1.0; // top  
    else if (n.y < -0.5) shade = 0.4; // bottom
    else if (abs(n.z) > 0.5) shade = 0.75; // front/back
    else shade = 0.6; // left/right

    float edgeX = min(TexCoord.x, 1.0 - TexCoord.x);
    float edgeY = min(TexCoord.y, 1.0 - TexCoord.y);
    float minEdge = min(edgeX, edgeY);

    vec3 finalColor = color * ambientStrength * shade;

    if (minEdge < 0.01) {
        finalColor = vec3(0.0, 0.0, 0.0);
    }

    FragColor = vec4(finalColor, 1.0);
}