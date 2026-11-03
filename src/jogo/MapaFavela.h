#pragma once
// Favela Stricker — o mapa da Favela (gerador cadastrado no motor com o nome "favela")
#include <vector>
#include <glm/glm.hpp>

namespace eng {
struct Engine;
void registrarMapaFavela();
// Caminhos da favela para os robôs (escadarias, patamares, rua em volta); `extra` (bases) entram ligados ao mais perto
void favelaNavGraph(Engine* e, std::vector<glm::vec3>& nodes, std::vector<std::vector<int>>& adj, const std::vector<glm::vec3>& extra);
}  // namespace eng
