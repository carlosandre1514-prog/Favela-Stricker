#version 320 es
in vec2 vNdc;
uniform mat4 uInvViewProj;  // inversa de proj * view SEM translação
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uGround;
uniform float uSkyIntensity;
uniform int uCapture;       // 1 = captura para o cubemap do IBL (HDR linear, sem sol)
out vec4 oColor;

// Mesma fórmula de Environment::sample() em src/render/Lights.h
vec3 skyColor(vec3 d) {
    vec3 c;
    if (d.y >= 0.0) c = mix(uHorizon, uZenith, pow(min(d.y, 1.0), 0.45));
    else c = mix(uHorizon * 0.6, uGround, pow(min(-d.y, 1.0), 0.35));
    return c * uSkyIntensity;
}

void main() {
    vec4 p = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 d = normalize(p.xyz / p.w);
    vec3 c = skyColor(d);
    if (uCapture == 1) { oColor = vec4(c, 1.0); return; }
    vec3 L = normalize(-uDirDir.xyz);
    float sd = max(dot(d, L), 0.0);
    c += uDirColor.rgb * (smoothstep(0.9993, 0.9997, sd) * 12.0 + pow(sd, 96.0) * 0.25);
    // estrelas: só com o céu escuro e azul (de dia, ou com o céu fechado da chuva, somem)
    float noite = 1.0 - smoothstep(0.1, 0.22, uSkyIntensity);
    float limpo = smoothstep(0.35, 0.6, (uZenith.b - uZenith.r) / max(uZenith.b, 1e-4));
    if (noite * limpo > 0.01 && d.y > 0.02) {
        vec3 g = d * 150.0;
        vec3 cell = floor(g);
        float h = fract(sin(dot(cell, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        if (h > 0.985) {
            float k = smoothstep(0.32, 0.0, length(fract(g) - 0.5));
            float brilho = (h - 0.985) / 0.015;
            c += vec3(0.8, 0.85, 1.0) * k * (0.15 + 0.6 * brilho * brilho) * noite * limpo * smoothstep(0.02, 0.25, d.y);
        }
    }
    oColor = vec4(toDisplay(c), 1.0);
}
