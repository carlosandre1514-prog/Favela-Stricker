#version 320 es
in vec2 vUV;
in vec4 vColor;
flat in int vShape;
in float vSeed;
out vec4 oColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}

void main() {
    float d = length(vUV);
    float m;
    if (vShape == 0) {                       // brilho suave
        m = pow(max(0.0, 1.0 - d), 2.0);
    } else if (vShape == 1) {                // fumaça (borda irregular)
        float n = noise(vUV * 2.5 + vSeed * 13.0) * 0.55 + noise(vUV * 5.0 - vSeed * 7.0) * 0.45;
        m = smoothstep(1.0, 0.15, d) * (0.35 + 0.8 * n);
    } else if (vShape == 2) {                // faísca / bala (o vertex shader alonga)
        m = pow(max(0.0, 1.0 - d), 3.0);
    } else if (vShape == 3) {                // anel de onda de choque
        m = exp(-pow((d - 0.72) / 0.12, 2.0));
    } else if (vShape == 4) {                // estrela / clarão
        float rays = max(exp(-abs(vUV.x) * 16.0), exp(-abs(vUV.y) * 16.0)) * max(0.0, 1.0 - d);
        m = max(pow(max(0.0, 1.0 - d), 4.0) * 1.5, rays);
    } else if (vShape == 5) {                // cristal de gelo
        float k = abs(vUV.x) * 1.9 + abs(vUV.y);
        m = smoothstep(1.0, 0.75, k) * (0.7 + 0.3 * step(0.0, vUV.x * vUV.y));
    } else if (vShape == 7) {                // núcleo de energia (skin): centro quente, raios, anéis girando e redemoinho
        float t = vSeed, ang = atan(vUV.y, vUV.x) / 6.2832;
        float c0 = exp(-d * d * 22.0) * 2.8;
        float rays = pow(noise(vec2(ang * 24.0, t * 1.3) * 6.4), 3.0) * exp(-d * 2.6) * 2.6;
        float r1 = smoothstep(0.04, 0.0, abs(d - 0.52)) * (0.5 + 0.5 * sin(ang * 6.2832 * 6.0 + t * 2.5));
        float r2 = smoothstep(0.02, 0.0, abs(d - 0.76)) * step(0.0, sin(ang * 6.2832 * 14.0 - t * 1.7));
        float r3 = smoothstep(0.05, 0.0, abs(d - 0.31)) * (0.4 + 0.6 * noise(vec2(ang * 18.0 - t, 3.0) * 6.4));
        float sw = noise(vec2(ang * 12.0 + d * 8.0 - t * 1.8, d * 7.0 + t * 0.6) * 6.4) * exp(-d * 3.0) * 1.3;
        float a = clamp(c0 + rays + r1 + r2 + r3 + sw, 0.0, 4.0) * (1.0 - smoothstep(0.9, 1.0, d));
        // (a cor já vem multiplicada pela força; o branco do centro na mesma força: o maior canal da cor)
        float forca = max(vColor.r, max(vColor.g, vColor.b));
        vec3 c = mix(vColor.rgb, vec3(forca), clamp(c0 * 0.45 + rays * 0.12, 0.0, 1.0)) * 1.7;
        if (a * forca < 0.004) discard;
        oColor = vec4(c * a, 0.0);
        return;
    } else {                                 // chama (gota com ruído subindo)
        vec2 q = vec2(vUV.x * (1.0 + (vUV.y + 1.0) * 0.65), vUV.y * 0.9 + 0.1);
        float n = noise(vec2(vUV.x * 3.0, vUV.y * 2.0 - vSeed * 20.0));
        m = smoothstep(1.0, 0.25, length(q) + n * 0.4);
    }
    if (m < 0.004) discard;
    oColor = vColor * m;  // pré-multiplicado: partícula aditiva tem alpha 0
}
