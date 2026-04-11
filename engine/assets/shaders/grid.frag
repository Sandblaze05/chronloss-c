#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform vec3 camPos;
uniform vec3 camRight;
uniform vec3 camUp;
uniform vec3 camForward;
uniform float halfWidth;
uniform float halfHeight;
uniform float zoom;
uniform vec3 gridColor;
uniform float lineThickness;

void main() {
    vec3 origin = camPos + camRight * (vUV.x * halfWidth) + camUp * (vUV.y * halfHeight);
    vec3 dir = camForward;
    if (abs(dir.y) < 1e-6) discard;
    float t = (0.0 - origin.y) / dir.y;
    vec3 pos = origin + dir * t;

    float base = max(1e-3, zoom);
    float p = floor(log2(base));
    float spacing = pow(2.0, p);

    float mdx = abs(fract(pos.x / spacing + 0.5) - 0.5) * spacing;
    float mdz = abs(fract(pos.z / spacing + 0.5) - 0.5) * spacing;
    float mind = min(mdx, mdz);

    float pixelSize = fwidth(mind);
        
    float screenPixelThickness = 1.5; 
        
    float line = 1.0 - smoothstep(0.0, pixelSize * screenPixelThickness, mind);

    float dist = length(pos.xz - camPos.xz);
    float fade = clamp(1.0 - dist / (zoom * 6.0), 0.0, 1.0);

    float intensity = line * fade;
    FragColor = vec4(gridColor, intensity);
}
