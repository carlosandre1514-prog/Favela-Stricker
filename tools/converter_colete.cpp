// Converte o colete do Sketchfab ("Armor vest" de 3DWorkbench, CC BY 4.0) de FBX para uma malha leve (OBJ) que o
// tools/encaixar_colete.py ajusta ao tronco do boneco de blocos.
//   - pula o manequim e a base que vêm no arquivo (grupos DUM*)
//   - as duas texturas (colete e bolsos) viram uma só: colete à esquerda (u 0..0,5), bolsos à direita (0,5..1)
//   - junta os vértices iguais e simplifica com o mesmo simplificador do motor (motor/procgen/Simplify.cpp)
// Compilar (com o ufbx e o glm que o CMake baixa em build/_deps):
//   g++ -O2 -std=c++17 -DGLM_ENABLE_EXPERIMENTAL -Imotor -Ibuild/_deps/glm-src -Ibuild/_deps/ufbx-src tools/converter_colete.cpp \
//       motor/procgen/Simplify.cpp build/_deps/ufbx-src/ufbx.c -o converter_colete
// Uso: ./converter_colete av.fbx colete.obj [fração dos triângulos = 0.12] [erro máximo = 0.06]
#include <ufbx.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>
#include "procgen/Simplify.h"

using eng::Vertex;

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("uso: %s entrada.fbx saida.obj [fração]\n", argv[0]); return 1; }
    const float fracao = argc > 3 ? float(std::atof(argv[3])) : 0.12f;
    const float erroMax = argc > 4 ? float(std::atof(argv[4])) : 0.06f;  // quanto a superfície pode mexer (fração do tamanho)
    ufbx_load_opts o = {};
    o.target_axes = ufbx_axes_right_handed_y_up;
    o.target_unit_meters = 1.0f;
    ufbx_error err;
    ufbx_scene* sc = ufbx_load_file(argv[1], &o, &err);
    if (!sc) { std::printf("erro: %s\n", err.description.data); return 1; }
    // uma malha por material (colete / bolsos), vértices iguais juntos
    struct Parte { std::vector<Vertex> v; std::vector<uint32_t> idx; std::map<std::tuple<float, float, float, float, float>, uint32_t> mapa; };
    Parte partes[2];
    size_t antes = 0;
    for (size_t ni = 0; ni < sc->nodes.count; ++ni) {
        ufbx_node* n = sc->nodes.data[ni];
        if (!n->mesh || std::strncmp(n->name.data, "DUM", 3) == 0) continue;  // manequim e base: fora
        ufbx_mesh* m = n->mesh;
        std::vector<uint32_t> tri(m->max_face_triangles * 3);
        const ufbx_matrix nm = ufbx_matrix_for_normals(&n->geometry_to_world);
        for (size_t pi = 0; pi < m->material_parts.count; ++pi) {
            const ufbx_mesh_part& part = m->material_parts.data[pi];
            const char* mat = pi < m->materials.count && m->materials.data[pi] ? m->materials.data[pi]->name.data : "";
            const int k = std::strcmp(mat, "pouch") == 0 ? 1 : 0;
            Parte& P = partes[k];
            for (size_t fi = 0; fi < part.face_indices.count; ++fi) {
                const ufbx_face face = m->faces.data[part.face_indices.data[fi]];
                const uint32_t nt = ufbx_triangulate_face(tri.data(), tri.size(), m, face);
                antes += nt;
                for (uint32_t t = 0; t < nt * 3; ++t) {
                    const uint32_t ix = tri[t];
                    const ufbx_vec3 p = ufbx_transform_position(&n->geometry_to_world, ufbx_get_vertex_vec3(&m->vertex_position, ix));
                    ufbx_vec3 nn = m->vertex_normal.exists ? ufbx_get_vertex_vec3(&m->vertex_normal, ix) : ufbx_vec3{0, 1, 0};
                    nn = ufbx_vec3_normalize(ufbx_transform_direction(&nm, nn));
                    const ufbx_vec2 uv = m->vertex_uv.exists ? ufbx_get_vertex_vec2(&m->vertex_uv, ix) : ufbx_vec2{0, 0};
                    const float u = float(k) * 0.5f + float(uv.x - std::floor(uv.x)) * 0.5f, vv = 1.f - float(uv.y - std::floor(uv.y));
                    // juntos só pela posição (a textura do colete é quase lisa): sem isso as costuras de textura travam o
                    // simplificador e a malha não desce de ~10 mil triângulos
                    auto r = [](double x) { return float(std::round(x * 2000.0) / 2000.0); };
                    auto chave = std::make_tuple(r(p.x), r(p.y), r(p.z), 0.f, 0.f);
                    auto it = P.mapa.find(chave);
                    if (it == P.mapa.end()) {
                        Vertex x{};
                        x.pos = {float(p.x), float(p.y), float(p.z)};
                        x.normal = {float(nn.x), float(nn.y), float(nn.z)};
                        x.uv = {u, vv};
                        x.tangent = {1, 0, 0, 1};
                        x.weights = {1, 0, 0, 0};
                        it = P.mapa.emplace(chave, uint32_t(P.v.size())).first;
                        P.v.push_back(x);
                    }
                    P.idx.push_back(it->second);
                }
            }
        }
    }
    FILE* f = std::fopen(argv[2], "w");
    size_t base = 1, depois = 0;
    for (int k = 0; k < 2; ++k) {
        Parte& P = partes[k];
        eng::simplifyMesh(P.v, P.idx, fracao, erroMax);
        std::fprintf(f, "g %s\n", k ? "bolsos" : "colete");
        for (const Vertex& x : P.v) std::fprintf(f, "v %f %f %f\nvn %f %f %f\nvt %f %f\n", x.pos.x, x.pos.y, x.pos.z, x.normal.x, x.normal.y, x.normal.z, x.uv.x, x.uv.y);
        for (size_t i = 0; i + 2 < P.idx.size(); i += 3) {
            const size_t a = base + P.idx[i], b = base + P.idx[i + 1], c = base + P.idx[i + 2];
            std::fprintf(f, "f %zu/%zu/%zu %zu/%zu/%zu %zu/%zu/%zu\n", a, a, a, b, b, b, c, c, c);
        }
        base += P.v.size();
        depois += P.idx.size() / 3;
    }
    std::fclose(f);
    std::printf("triângulos: %zu -> %zu\n", antes, depois);
    return 0;
}
