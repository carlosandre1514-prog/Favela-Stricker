#pragma once
// Ferramentas do motor para montar mapas (usadas pelos geradores de mapa que o jogo cadastra):
// terreno com colisão, construções agrupadas por região (poucos objetos), ruas, limites e peças soltas.
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "engine/Engine.h"

namespace eng {

// ---- cadastro de mapas: o jogo cadastra os dele; o projeto escolhe pelo nome ("mapa": {"tipo": "<nome>"})
struct MapGenerator {
    std::string name;
    void (*generate)(Engine* e, Rng& r, glm::vec3& spawn, float& spawnYaw) = nullptr;  // monta o mapa na cena
    float (*height)(Engine* e, float x, float z) = nullptr;                            // altura do chão (robôs, objetos)
};
void registerMap(const MapGenerator& g);
int mapIndexOf(const std::string& name);   // MAP_None se não cadastrado
const char* mapNameOf(int kind);
bool mapIs(const Engine* e, const char* name);  // o mapa aberto é este?

// ---- ferramentas
using HeightFn = std::function<float(float, float)>;
float smooth01(float a, float b, float x);                                   // 0 em a, 1 em b (suave)
float segDist2(glm::vec2 p, glm::vec2 a, glm::vec2 b, float& t);             // distância² de p ao segmento ab
// Malha de terreno res x res sobre [x0, x0+size]²; heights (opcional) recebe as alturas da grade;
// hint (opcional): tipo de chão para o shader de terreno (estrada/cidade, mata, roça)
void terrainMesh(std::vector<Vertex>& v, std::vector<uint32_t>& idx, float x0, float z0, float size, int res, const HeightFn& H,
                 std::vector<float>* heights, const std::function<glm::vec3(float, float)>* hint = nullptr);
void addHeightfield(Engine* e, TerrainChunk& c, float x0, float z0, float size, int res);  // colisão do terreno
Prototype protoFromGeo(Engine* e, Geo& g);
void setBounds(Engine* e, glm::vec2 mn, glm::vec2 mx);                      // paredes invisíveis do limite do mapa
void roadStrip(MeshBuilder& b, glm::vec2 a, glm::vec2 c, float w, float y);  // faixa de rua
void groundBox(Geo& g, Mat m, glm::vec2 mn, glm::vec2 mx, float top, float tile);
void segBox(MeshBuilder& m, glm::vec3 a, glm::vec3 b, float t, float t2 = -1.f);  // barra de a até b
void yawBox(MeshBuilder& m, glm::vec3 base, glm::vec3 size, float yaw);
void wire(MeshBuilder& m, glm::vec3 a, glm::vec3 b, float sag);             // fio caído entre dois pontos
void muralQuad(Geo& g, int idx, glm::vec3 c, glm::vec3 n, float w);         // pichação/placa numa parede

struct Cells {
    float size;
    int group;
    float detailFar = kDetailFar;  // até onde o detalhe aparece (dali em diante, a versão "de longe")
    std::map<int64_t, Geo> m;
    Cells(float s, int grp = 1) : size(s), group(grp) {}
    static int64_t key(int64_t cx, int64_t cz) { return (cx << 32) ^ (cz & 0xffffffff); }
    Geo& at(glm::vec3 p) {
        Geo& g = m[key(int64_t(std::floor(p.x / size)), int64_t(std::floor(p.z / size)))];
        return g;
    }
    void flush(Engine* e);   // cria os objetos do mapa (poucos por região) e limpa
};

}  // namespace eng
