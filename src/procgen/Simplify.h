#pragma once
#include <cstdint>
#include <vector>
#include "render/Renderer.h"

namespace eng {

// Diminui os triângulos de uma malha (colapso de arestas por erro quádrico, Garland-Heckbert).
// Bordas abertas e costuras de textura (vértices repetidos na mesma posição) ficam presas, para a malha
// não rasgar nem a textura escorregar. ratio = fração dos triângulos que fica (0.3 = 30%).
// maxErro: distância máxima que a superfície pode se mexer, em fração do tamanho da malha.
// Devolve os triângulos que sobraram; os vértices sem uso saem de v.
void simplifyMesh(std::vector<Vertex>& v, std::vector<uint32_t>& idx, float ratio, float maxErro = 0.012f);

}  // namespace eng
