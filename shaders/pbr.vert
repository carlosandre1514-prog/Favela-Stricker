#version 320 es
// Variantes: -DINSTANCED (matriz por instância) ou -DSKINNED (paleta de joints, sem instancing); -DLEVE (gráfico Leve)
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;
layout(location = 4) in uvec4 aJoints;
layout(location = 5) in vec4 aWeights;
layout(location = 6) in mat4 aInstance;  // ocupa locations 6..9
layout(location = 10) in vec4 aColor;

layout(std140, binding = 1) uniform JointBlock { mat4 uJoints[128]; };

uniform mat4 uModel;  // usado quando não é INSTANCED
// skin de energia: x veias, y pedaços soltos (ativação), z 1 = ligado; centro do núcleo no espaço do modelo
uniform vec4 uEnergia;
uniform vec3 uEnergiaNuc;
uniform float uTime;
out vec3 vObjPos;     // posição no espaço do modelo (veias de energia)
out float vSolto;     // quanto o pedaço se soltou (ativação da skin de energia)
// pedaço de cada vértice: o triângulo com os 3 vértices no mesmo pedaço tem o valor interpolado igual ao do vértice
// que manda no triângulo (flat); o que está na divisa entre dois pedaços esticaria entre eles e some (pbr.frag)
out vec3 vCelula;
flat out vec3 vCelulaTri;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
#if !defined(LEVE)
out vec4 vTangent;    // xyz tangente, w sinal da bitangente (leve: sem mapa de normais)
#endif
out float vViewDepth;
out vec4 vColor;
flat out uint vCut;      // aberturas que cortam o objeto

void main() {
#if defined(SKINNED)
    mat4 skin = aWeights.x * uJoints[aJoints.x] + aWeights.y * uJoints[aJoints.y] +
                aWeights.z * uJoints[aJoints.z] + aWeights.w * uJoints[aJoints.w];
    mat4 model = uModel * skin;
#elif defined(INSTANCED)
    mat4 model = aInstance;
#else
    mat4 model = uModel;
#endif
    // bits das aberturas que cortam este objeto vêm na linha de baixo da matriz (sempre 0 numa matriz afim)
    vCut = uint(model[0][3] + 0.5) | (uint(model[1][3] + 0.5) << 16);
    model[0][3] = 0.0; model[1][3] = 0.0;
    vec3 pos = aPos;
    vObjPos = aPos;
    vSolto = 0.0;
    vCelula = vec3(0.0);
    vCelulaTri = vec3(0.0);
    if (uEnergia.y > 0.0) {
        // a arma se solta em pedaços (células de 7 cm) que saem do núcleo girando um pouco e voltam
        vec3 cel = floor(aPos / 0.07);
        vec3 r = fract(cel * vec3(0.1031, 0.1030, 0.0973));
        r += dot(r, r.yxz + 33.33);
        r = fract((r.xxy + r.yxx) * r.zyx);
        vec3 centro = (cel + 0.5) * 0.07;
        vec3 dir = normalize(centro - uEnergiaNuc + (r - 0.5) * 0.06 + vec3(0.0, 0.01, 0.0));
        float f = uEnergia.y * (0.55 + 0.9 * r.x);
        vec3 k = normalize(r - 0.5 + 1e-3);
        float ang = f * (r.y - 0.5) * 2.4;
        vec3 rel = pos - centro;
        rel = rel * cos(ang) + cross(k, rel) * sin(ang) + k * dot(k, rel) * (1.0 - cos(ang));
        pos = centro + rel + dir * f * 0.16 + vec3(0.0, sin(uTime * 2.0 + r.z * 6.0) * 0.004 * f, 0.0);
        vSolto = f;
        vCelula = cel;
        vCelulaTri = cel;
    }
    vec4 wp = model * vec4(pos, 1.0);
    // normais corretas mesmo com escala não uniforme: a matriz dos cofatores é a inversa-transposta vezes o
    // determinante (some ao normalizar). 3 produtos vetoriais em vez de inverter a matriz em cada vértice.
    mat3 m3 = mat3(model);
    mat3 cof = mat3(cross(m3[1], m3[2]), cross(m3[2], m3[0]), cross(m3[0], m3[1]));
    float det = dot(m3[0], cof[0]);
    vWorldPos = wp.xyz;
    vNormal = normalize(cof * aNormal) * (det < 0.0 ? -1.0 : 1.0);  // espelhado: o determinante negativo virava a normal
#if !defined(LEVE)
    vTangent = vec4(normalize(m3 * aTangent.xyz), aTangent.w);
#endif
    vUV = aUV;
    vColor = aColor;
    gl_Position = uViewProj * wp;
    vViewDepth = gl_Position.w;  // profundidade de view para escolher a cascata
}
