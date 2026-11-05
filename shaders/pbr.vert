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
    vec4 wp = model * vec4(aPos, 1.0);
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
