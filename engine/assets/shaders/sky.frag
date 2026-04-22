#version 330 core
in vec2 vNDC;
out vec4 FragColor;

uniform vec3 camForward;
uniform vec3 camRight;
uniform vec3 camUp;
uniform float fov;
uniform vec3 aspect;

void main() {
    float tanHalfFov = tan(fov / 2.0);
    vec3 rayDir = normalize(camForward + camRight * vNDC.x * aspect * tanHalfFov + camUp * vNDC.y * tanHalfFov);

    // calculate gradient using the y component of ray
    // rayDir.y belongs to [-1, 1] 
    float t = clamp(rayDir.y * 0.5 + 0.5, 0.0, 1.0);

    vec3 horizonColor = vec3(0.7, 0.85, 0.95);
    vec3 zenithColor = vec3(0.15, 0.4, 0.8);

    vec3 skyColor = mix(horizonColor, zenithColor, t);

    FragColor = vec4(skyColor, 1.0);
}
