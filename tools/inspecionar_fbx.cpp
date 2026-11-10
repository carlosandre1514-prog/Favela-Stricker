// Mostra o que tem num FBX (modelos da Meshy e outros): malhas, triângulos, tamanho, materiais, esqueleto e animações.
// Compilar (com o ufbx que o CMake baixa em build/_deps):
//   g++ -O2 -std=c++17 -Ibuild/_deps/ufbx-src tools/inspecionar_fbx.cpp build/_deps/ufbx-src/ufbx.c -o inspecionar_fbx
// Uso: ./inspecionar_fbx modelo.fbx [outro.fbx ...]
#include <ufbx.h>
#include <cfloat>
#include <cstdio>

int main(int argc, char** argv) {
    for (int a = 1; a < argc; ++a) {
        ufbx_load_opts o = {};
        o.target_axes = ufbx_axes_right_handed_y_up;
        o.target_unit_meters = 1.0f;
        ufbx_error err;
        ufbx_scene* sc = ufbx_load_file(argv[a], &o, &err);
        std::printf("== %s\n", argv[a]);
        if (!sc) { std::printf("   erro: %s\n", err.description.data); continue; }
        size_t tris = 0, verts = 0;
        double mn[3] = {DBL_MAX, DBL_MAX, DBL_MAX}, mx[3] = {-DBL_MAX, -DBL_MAX, -DBL_MAX};
        for (size_t i = 0; i < sc->nodes.count; ++i) {
            ufbx_node* n = sc->nodes.data[i];
            if (!n->mesh) continue;
            ufbx_mesh* m = n->mesh;
            tris += m->num_triangles;
            verts += m->num_vertices;
            for (size_t v = 0; v < m->num_vertices; ++v) {
                ufbx_vec3 p = ufbx_transform_position(&n->geometry_to_world, m->vertices.data[v]);
                const double c[3] = {p.x, p.y, p.z};
                for (int k = 0; k < 3; ++k) { if (c[k] < mn[k]) mn[k] = c[k]; if (c[k] > mx[k]) mx[k] = c[k]; }
            }
            std::printf("   malha '%s': %zu triângulos, %zu vértices, UV %s, %zu materiais, pele (skin) %zu\n", n->name.data, m->num_triangles,
                        m->num_vertices, m->vertex_uv.exists ? "sim" : "não", m->materials.count, m->skin_deformers.count);
        }
        size_t ossos = 0;
        for (size_t i = 0; i < sc->nodes.count; ++i) if (sc->nodes.data[i]->bone) ++ossos;
        std::printf("   total: %zu triângulos, %zu vértices · tamanho %.2f x %.2f x %.2f m · ossos %zu · animações %zu\n", tris, verts,
                    mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], ossos, sc->anim_stacks.count);
        ufbx_free_scene(sc);
    }
    return 0;
}
