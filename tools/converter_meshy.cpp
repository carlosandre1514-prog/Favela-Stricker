// Converte um modelo da Meshy (FBX de ~1 milhão de triângulos, uma malha, um material) num .glb leve para o jogo.
//
// A Meshy pinta a textura em milhares de pedacinhos espalhados (UV picado): simplificar mantendo esse UV para em
// ~94 mil triângulos (cada borda de pedaço fica presa). Então aqui:
//   1. junta os vértices pela posição e simplifica até o alvo (motor/procgen/Simplify.cpp), sem se importar com o UV
//   2. faz um UV novo, limpo, para a malha leve (xatlas)
//   3. "assa" as texturas: para cada pixel da textura nova acha o ponto da malha original logo embaixo (raio na
//      direção da normal, com uma árvore de caixas para ser rápido) e copia a cor, o metal, a rugosidade e a normal
//      (a normal do modelo detalhado + o normal map dele, escrita no espaço tangente da malha leve)
//   4. grava o .glb com as texturas em JPEG embutidas (cor, normal, ORM do glTF e, com --brilho, a emissiva: as
//      partes bem coloridas e claras, para skins de energia que acendem no escuro)
//      Com --skin <prefixo> (skin de arma com modelo próprio): o .glb leva só a malha e o normal map; a cor, o ORM e
//      a emissiva vão para <prefixo>.jpg, <prefixo>_orm.jpg e <prefixo>_emi.jpg, a pintura de skin que o jogo já
//      aplica (com o <prefixo>_efeito.json: aura e cor do tiro)
// Arma (--arma): deita com o cano em +Z (a ponta mais fina é a do cano) e fica com o comprimento de --comprimento.
//
// Compilar (ufbx, glm e stb que o CMake baixa em build/_deps; xatlas: pip download xatlas --no-binary :all:, a
// pasta extern/xatlas/source/xatlas do pacote):
//   g++ -O2 -std=c++17 -DGLM_ENABLE_EXPERIMENTAL -Imotor -Ibuild/_deps/glm-src -Ibuild/_deps/ufbx-src \
//       -Ibuild/_deps/tinygltf-src -I<xatlas> tools/converter_meshy.cpp motor/procgen/Simplify.cpp \
//       build/_deps/ufbx-src/ufbx.c <xatlas>/xatlas.cpp -o converter_meshy -lpthread
// Uso: ./converter_meshy <pasta_do_zip> saida.glb triangulos [--tam 1024] [--arma] [--comprimento 0.95] [--brilho 0.55]
//                        [--skin shaders/kit/armas/skins/ak47_cosmica]
#include <ufbx.h>
#include <xatlas.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "assets/MeshUtils.h"
#include "procgen/Simplify.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using eng::Vertex;
using glm::vec2;
using glm::vec3;
using glm::vec4;

namespace {

struct Imagem {
    int w = 0, h = 0, c = 0;
    std::vector<unsigned char> px;
    bool ler(const std::string& p, int canais) {
        unsigned char* d = stbi_load(p.c_str(), &w, &h, &c, canais);
        if (!d) return false;
        c = canais;
        px.assign(d, d + size_t(w) * h * canais);
        stbi_image_free(d);
        return true;
    }
    // bilinear, uv do FBX (v para cima): a linha 0 da imagem é o alto
    vec4 amostra(vec2 uv) const {
        float x = (uv.x - std::floor(uv.x)) * w - 0.5f, y = (1.f - (uv.y - std::floor(uv.y))) * h - 0.5f;
        int x0 = int(std::floor(x)), y0 = int(std::floor(y));
        float fx = x - x0, fy = y - y0;
        auto px1 = [&](int xx, int yy) {
            xx = std::clamp(xx, 0, w - 1); yy = std::clamp(yy, 0, h - 1);
            const unsigned char* p = &px[(size_t(yy) * w + xx) * c];
            vec4 r(0.f, 0.f, 0.f, 1.f);
            for (int k = 0; k < c && k < 4; ++k) r[k] = p[k] / 255.f;
            return r;
        };
        return glm::mix(glm::mix(px1(x0, y0), px1(x0 + 1, y0), fx), glm::mix(px1(x0, y0 + 1), px1(x0 + 1, y0 + 1), fx), fy);
    }
};

// árvore de caixas (BVH) dos triângulos do modelo detalhado
struct Arvore {
    struct No { vec3 mn, mx; uint32_t ini, n, esq; };
    std::vector<No> nos;
    std::vector<uint32_t> tris;
    const std::vector<vec3>* P = nullptr;
    const std::vector<uint32_t>* I = nullptr;
    std::vector<vec3> centro;
    void montar(const std::vector<vec3>& pos, const std::vector<uint32_t>& idx) {
        P = &pos; I = &idx;
        const uint32_t nt = uint32_t(idx.size() / 3);
        tris.resize(nt);
        centro.resize(nt);
        for (uint32_t t = 0; t < nt; ++t) {
            tris[t] = t;
            centro[t] = (pos[idx[t * 3]] + pos[idx[t * 3 + 1]] + pos[idx[t * 3 + 2]]) / 3.f;
        }
        nos.reserve(nt / 2);
        nos.push_back({});
        dividir(0, 0, nt);
    }
    void dividir(uint32_t ni, uint32_t ini, uint32_t n) {
        vec3 mn(1e30f), mx(-1e30f), cmn(1e30f), cmx(-1e30f);
        for (uint32_t k = ini; k < ini + n; ++k) {
            const uint32_t t = tris[k];
            for (int j = 0; j < 3; ++j) { const vec3& p = (*P)[(*I)[t * 3 + j]]; mn = glm::min(mn, p); mx = glm::max(mx, p); }
            cmn = glm::min(cmn, centro[t]); cmx = glm::max(cmx, centro[t]);
        }
        nos[ni].mn = mn; nos[ni].mx = mx; nos[ni].ini = ini; nos[ni].n = n; nos[ni].esq = 0;
        if (n <= 4) return;
        const vec3 e = cmx - cmn;
        const int eixo = e.x > e.y && e.x > e.z ? 0 : e.y > e.z ? 1 : 2;
        const uint32_t meio = ini + n / 2;
        std::nth_element(tris.begin() + ini, tris.begin() + meio, tris.begin() + ini + n,
                         [&](uint32_t a, uint32_t b) { return centro[a][eixo] < centro[b][eixo]; });
        const uint32_t esq = uint32_t(nos.size());
        nos.push_back({}); nos.push_back({});
        nos[ni].esq = esq; nos[ni].n = 0;
        dividir(esq, ini, meio - ini);
        dividir(esq + 1, meio, ini + n - meio);
    }
    static bool caixa(const vec3& o, const vec3& inv, const vec3& mn, const vec3& mx, float tmax) {
        vec3 t0 = (mn - o) * inv, t1 = (mx - o) * inv;
        vec3 a = glm::min(t0, t1), b = glm::max(t0, t1);
        float tn = std::max(std::max(a.x, a.y), std::max(a.z, 0.f)), tf = std::min(std::min(b.x, b.y), std::min(b.z, tmax));
        return tn <= tf;
    }
    // acerto com t em [0, tmax] mais perto de tAlvo (o ponto da malha leve fica em tAlvo)
    bool raio(vec3 o, vec3 d, float tmax, float tAlvo, uint32_t& triOut, float& b1, float& b2) const {
        const vec3 inv(1.f / (std::abs(d.x) > 1e-12f ? d.x : 1e-12f), 1.f / (std::abs(d.y) > 1e-12f ? d.y : 1e-12f),
                       1.f / (std::abs(d.z) > 1e-12f ? d.z : 1e-12f));
        uint32_t pilha[128];
        int sp = 0;
        pilha[sp++] = 0;
        float melhor = 1e30f;
        bool achou = false;
        while (sp) {
            const No& no = nos[pilha[--sp]];
            if (!caixa(o, inv, no.mn, no.mx, tmax)) continue;
            if (no.n == 0) { pilha[sp++] = no.esq; pilha[sp++] = no.esq + 1; continue; }
            for (uint32_t k = no.ini; k < no.ini + no.n; ++k) {
                const uint32_t t = tris[k];
                const vec3 &p0 = (*P)[(*I)[t * 3]], &p1 = (*P)[(*I)[t * 3 + 1]], &p2 = (*P)[(*I)[t * 3 + 2]];
                const vec3 e1 = p1 - p0, e2 = p2 - p0, pv = glm::cross(d, e2);
                const float det = glm::dot(e1, pv);
                if (std::abs(det) < 1e-14f) continue;
                const float id = 1.f / det;
                const vec3 tv = o - p0;
                const float u = glm::dot(tv, pv) * id;
                if (u < 0.f || u > 1.f) continue;
                const vec3 qv = glm::cross(tv, e1);
                const float v = glm::dot(d, qv) * id;
                if (v < 0.f || u + v > 1.f) continue;
                const float tt = glm::dot(e2, qv) * id;
                if (tt < 0.f || tt > tmax) continue;
                const float dist = std::abs(tt - tAlvo);
                if (dist < melhor) { melhor = dist; triOut = t; b1 = u; b2 = v; achou = true; }
            }
        }
        return achou;
    }
};

std::vector<std::string> pngsDa(const std::string& pasta) {
    std::vector<std::string> r;
    if (DIR* d = opendir(pasta.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 4 && (n.substr(n.size() - 4) == ".png" || n.substr(n.size() - 4) == ".fbx")) r.push_back(pasta + "/" + n);
        }
        closedir(d);
    }
    std::sort(r.begin(), r.end());
    return r;
}
bool terminaCom(const std::string& s, const std::string& f) { return s.size() >= f.size() && s.compare(s.size() - f.size(), f.size(), f) == 0; }

// preenche os pixels vazios com a média dos vizinhos cheios (para o filtro e os mipmaps não puxarem o fundo)
void dilatar(std::vector<unsigned char>& img, std::vector<unsigned char>& cheio, int T, int c, int vezes) {
    for (int it = 0; it < vezes; ++it) {
        std::vector<unsigned char> novo = cheio;
        for (int y = 0; y < T; ++y)
            for (int x = 0; x < T; ++x) {
                if (cheio[size_t(y) * T + x]) continue;
                int s[4] = {0, 0, 0, 0}, n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= T || yy >= T || !cheio[size_t(yy) * T + xx]) continue;
                        for (int k = 0; k < c; ++k) s[k] += img[(size_t(yy) * T + xx) * c + k];
                        ++n;
                    }
                if (!n) continue;
                for (int k = 0; k < c; ++k) img[(size_t(y) * T + x) * c + k] = (unsigned char)(s[k] / n);
                novo[size_t(y) * T + x] = 1;
            }
        cheio.swap(novo);
    }
}

void jpgNaMemoria(void* ctx, void* d, int n) {
    auto* v = static_cast<std::vector<char>*>(ctx);
    v->insert(v->end(), (char*)d, (char*)d + n);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> pos;
    bool arma = false;
    int T = 1024;
    float comprimento = 0.f, brilho = -1.f;
    std::string skin;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--arma") arma = true;
        else if (a == "--tam" && i + 1 < argc) T = std::atoi(argv[++i]);
        else if (a == "--comprimento" && i + 1 < argc) comprimento = float(std::atof(argv[++i]));
        else if (a == "--brilho" && i + 1 < argc) brilho = float(std::atof(argv[++i]));
        else if (a == "--skin" && i + 1 < argc) skin = argv[++i];
        else pos.push_back(a);
    }
    if (pos.size() < 3) { std::printf("uso: %s <pasta_do_zip> saida.glb triangulos [--tam 1024] [--arma] [--comprimento 0.95] [--brilho 0.55]\n", argv[0]); return 1; }
    const size_t alvo = size_t(std::atol(pos[2].c_str()));

    // arquivos da pasta: o .fbx, a cor (o .png sem sufixo), _normal, _metallic, _roughness
    std::string fbx, fCor, fNormal, fMetal, fRug;
    for (const std::string& p : pngsDa(pos[0])) {
        if (terminaCom(p, ".fbx")) fbx = p;
        else if (terminaCom(p, "_normal.png")) fNormal = p;
        else if (terminaCom(p, "_metallic.png")) fMetal = p;
        else if (terminaCom(p, "_roughness.png")) fRug = p;
        else if (!terminaCom(p, "_metallic_roughness.png")) fCor = p;
    }
    if (fbx.empty() || fCor.empty()) { std::printf("erro: não achei o .fbx e a cor em %s\n", pos[0].c_str()); return 1; }

    // ---- modelo detalhado
    ufbx_load_opts o = {};
    o.target_axes = ufbx_axes_right_handed_y_up;
    o.target_unit_meters = 1.0f;
    ufbx_error err;
    ufbx_scene* sc = ufbx_load_file(fbx.c_str(), &o, &err);
    if (!sc) { std::printf("erro: %s\n", err.description.data); return 1; }
    std::vector<vec3> hp, hn;  // por canto (3 por triângulo)
    std::vector<vec2> huv;
    {
        std::vector<uint32_t> tri;
        for (size_t ni = 0; ni < sc->nodes.count; ++ni) {
            ufbx_node* n = sc->nodes.data[ni];
            if (!n->mesh) continue;
            ufbx_mesh* m = n->mesh;
            tri.resize(m->max_face_triangles * 3);
            const ufbx_matrix nm = ufbx_matrix_for_normals(&n->geometry_to_world);
            for (size_t fi = 0; fi < m->faces.count; ++fi) {
                const uint32_t nt = ufbx_triangulate_face(tri.data(), tri.size(), m, m->faces.data[fi]);
                for (uint32_t k = 0; k < nt * 3; ++k) {
                    const uint32_t c = tri[k];
                    ufbx_vec3 p = ufbx_transform_position(&n->geometry_to_world, ufbx_get_vertex_vec3(&m->vertex_position, c));
                    ufbx_vec3 nn = m->vertex_normal.exists ? ufbx_transform_direction(&nm, ufbx_get_vertex_vec3(&m->vertex_normal, c)) : ufbx_vec3{0, 1, 0};
                    ufbx_vec2 uv = m->vertex_uv.exists ? ufbx_get_vertex_vec2(&m->vertex_uv, c) : ufbx_vec2{0, 0};
                    hp.push_back(vec3(float(p.x), float(p.y), float(p.z)));
                    vec3 nv(float(nn.x), float(nn.y), float(nn.z));
                    hn.push_back(glm::length(nv) > 1e-12f ? glm::normalize(nv) : vec3(0, 1, 0));
                    huv.push_back(vec2(float(uv.x), float(uv.y)));
                }
            }
        }
    }
    ufbx_free_scene(sc);
    const size_t antes = hp.size() / 3;
    std::printf("lido: %zu triângulos\n", antes);

    // arma: eixo maior vira Z, cano (ponta mais baixa) em +Z, centro na origem, comprimento pedido
    vec3 mn(1e30f), mx(-1e30f);
    for (const vec3& p : hp) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
    {
        vec3 c = (mn + mx) * 0.5f;
        float esc = 1.f;
        int eixo = 2;
        bool inverte = false;
        if (arma) {
            const vec3 ext = mx - mn;
            eixo = ext.x >= ext.z ? 0 : 2;
            auto altura = [&](bool fim) {
                float a = 1e30f, b = -1e30f;
                const float lo = mn[eixo] + ext[eixo] * (fim ? 0.92f : 0.f), hi = mn[eixo] + ext[eixo] * (fim ? 1.f : 0.08f);
                for (const vec3& p : hp) if (p[eixo] >= lo && p[eixo] <= hi) { a = std::min(a, p.y); b = std::max(b, p.y); }
                return b - a;
            };
            inverte = !(altura(true) < altura(false));
            if (comprimento > 0.f) esc = comprimento / ext[eixo];
            std::printf("arma: comprido no eixo %c, cano na ponta %s, escala %.3f\n", eixo == 0 ? 'X' : 'Z', inverte ? "-" : "+", esc);
        } else {
            c = vec3(c.x, mn.y, c.z);  // personagem/objeto: pé no chão, centro em X/Z
        }
        auto gira = [&](vec3 v, bool ponto) {
            if (ponto) v -= c;
            float z = eixo == 0 ? v.x : v.z, x = eixo == 0 ? -v.z : v.x;
            if (inverte) { z = -z; x = -x; }
            v = vec3(x, v.y, z);
            return ponto ? v * esc : v;
        };
        for (vec3& p : hp) p = gira(p, true);
        for (vec3& n : hn) n = gira(n, false);
        mn = vec3(1e30f); mx = vec3(-1e30f);
        for (const vec3& p : hp) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
    }
    const float tamanho = glm::length(mx - mn);

    // ---- malha leve: vértices juntos pela posição, simplificada
    std::vector<Vertex> lv;
    std::vector<uint32_t> li;
    {
        struct H { size_t operator()(const vec3& p) const { uint32_t h[3]; std::memcpy(h, &p, 12); return h[0] * 73856093u ^ h[1] * 19349663u ^ h[2] * 83492791u; } };
        std::unordered_map<vec3, uint32_t, H> mapa;
        mapa.reserve(hp.size() / 3);
        for (size_t k = 0; k < hp.size(); ++k) {
            auto it = mapa.emplace(hp[k], uint32_t(lv.size()));
            if (it.second) { Vertex v{}; v.pos = hp[k]; v.color = vec4(1.f); v.weights = vec4(1, 0, 0, 0); lv.push_back(v); }
            li.push_back(it.first->second);
        }
        // tira triângulos que viraram linha/ponto
        std::vector<uint32_t> ok;
        for (size_t t = 0; t + 2 < li.size(); t += 3)
            if (li[t] != li[t + 1] && li[t + 1] != li[t + 2] && li[t] != li[t + 2]) ok.insert(ok.end(), {li[t], li[t + 1], li[t + 2]});
        li.swap(ok);
        eng::generateNormals(lv, li);
        for (int passo = 0; passo < 6 && li.size() / 3 > alvo * 11 / 10; ++passo) {
            const float erro = 0.004f * float(1 << passo);
            eng::simplifyMesh(lv, li, float(alvo) / float(li.size() / 3), erro);
            std::printf("  simplificando (erro %.3f): %zu triângulos\n", erro, li.size() / 3);
        }
        eng::generateNormals(lv, li);
    }

    // ---- UV novo (xatlas), sempre num atlas só
    std::vector<Vertex> fv;
    std::vector<uint32_t> fi;
    {
        std::vector<float> P, N;
        for (const Vertex& v : lv) { P.insert(P.end(), {v.pos.x, v.pos.y, v.pos.z}); N.insert(N.end(), {v.normal.x, v.normal.y, v.normal.z}); }
        float tpu = 0.f;
        // os pedaços (charts) são calculados uma vez só (a parte demorada em malhas grandes); o encaixe na textura
        // é que se repete até usar a textura toda sem passar dela
        xatlas::Atlas* at = xatlas::Create();
        xatlas::MeshDecl md;
        md.vertexCount = uint32_t(lv.size());
        md.vertexPositionData = P.data(); md.vertexPositionStride = 12;
        md.vertexNormalData = N.data(); md.vertexNormalStride = 12;
        md.indexCount = uint32_t(li.size()); md.indexData = li.data(); md.indexFormat = xatlas::IndexFormat::UInt32;
        if (xatlas::AddMesh(at, md) != xatlas::AddMeshError::Success) { std::printf("erro no xatlas\n"); return 1; }
        xatlas::ChartOptions co;
        co.maxIterations = li.size() / 3 > 100000 ? 1 : 2;
        xatlas::ComputeCharts(at, co);
        std::printf("  pedaços do UV: %u\n", at->chartCount);
        std::fflush(stdout);
        for (int tent = 0; tent < 20; ++tent) {
            xatlas::PackOptions po;
            po.resolution = uint32_t(T);
            po.padding = 3;
            po.texelsPerUnit = tpu;
            po.bilinear = true;
            xatlas::PackCharts(at, po);
            if (at->atlasCount > 1 || at->width > uint32_t(T) || at->height > uint32_t(T)) {  // não coube: diminui
                tpu = at->texelsPerUnit * (at->atlasCount > 1 ? 0.8f : 0.93f);
                continue;
            }
            const uint32_t lado = std::max(at->width, at->height);
            if (tent < 19 && lado < uint32_t(T) * 9 / 10) {  // sobrou muito espaço: aumenta para usar a textura toda
                tpu = at->texelsPerUnit * float(T) / float(lado) * 0.97f;
                continue;
            }
            const xatlas::Mesh& m = at->meshes[0];
            for (uint32_t k = 0; k < m.vertexCount; ++k) {
                Vertex v = lv[m.vertexArray[k].xref];
                v.uv = vec2(m.vertexArray[k].uv[0] / float(T), m.vertexArray[k].uv[1] / float(T));
                fv.push_back(v);
            }
            fi.assign(m.indexArray, m.indexArray + m.indexCount);
            std::printf("  UV novo: %u pedaços, %.0f%% da textura usada\n", at->chartCount, at->utilization ? at->utilization[0] * 100.f : 0.f);
            break;
        }
        xatlas::Destroy(at);
        if (fv.empty()) { std::printf("erro: UV não coube\n"); return 1; }
    }
    eng::generateTangents(fv, fi);  // a mesma conta do motor: o normal map assado fica no espaço tangente que o motor usa

    // ---- texturas do modelo detalhado
    Imagem iCor, iNormal, iMetal, iRug;
    if (!iCor.ler(fCor, 3)) { std::printf("erro: cor\n"); return 1; }
    const bool temN = !fNormal.empty() && iNormal.ler(fNormal, 3);
    const bool temM = !fMetal.empty() && iMetal.ler(fMetal, 1);
    const bool temR = !fRug.empty() && iRug.ler(fRug, 1);
    std::printf("  texturas: cor %dx%d, normal %s, metal %s, rugosidade %s\n", iCor.w, iCor.h, temN ? "sim" : "não", temM ? "sim" : "não", temR ? "sim" : "não");

    // ---- assar
    std::vector<uint32_t> hidx(hp.size());
    for (uint32_t k = 0; k < hidx.size(); ++k) hidx[k] = k;
    Arvore arv;
    arv.montar(hp, hidx);
    std::vector<unsigned char> oCor(size_t(T) * T * 3, 0), oNor(size_t(T) * T * 3, 0), oOrm(size_t(T) * T * 3, 0), cheio(size_t(T) * T, 0);
    size_t acertos = 0, faltas = 0;
    const float D = tamanho * 0.02f;
    for (size_t t = 0; t + 2 < fi.size(); t += 3) {
        const Vertex &A = fv[fi[t]], &B = fv[fi[t + 1]], &C = fv[fi[t + 2]];
        const vec2 a = A.uv * float(T), b = B.uv * float(T), c = C.uv * float(T);
        const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (std::abs(area) < 1e-9f) continue;
        const int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x})))), x1 = std::min(T - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y})))), y1 = std::min(T - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const vec2 p(x + 0.5f, y + 0.5f);
                float w1 = ((p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y)) / area;
                float w2 = ((b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y)) / area;
                float w0 = 1.f - w1 - w2;
                const float tol = -0.02f;
                if (w0 < tol || w1 < tol || w2 < tol) continue;
                const size_t pi = size_t(y) * T + x;
                if (cheio[pi]) continue;
                w0 = std::max(w0, 0.f); w1 = std::max(w1, 0.f); w2 = std::max(w2, 0.f);
                const float s = w0 + w1 + w2;
                w0 /= s; w1 /= s; w2 /= s;
                const vec3 P = A.pos * w0 + B.pos * w1 + C.pos * w2;
                vec3 N = glm::normalize(A.normal * w0 + B.normal * w1 + C.normal * w2);
                uint32_t ht = 0; float u = 0, v = 0;
                bool hit = arv.raio(P + N * D, -N, 2.f * D, D, ht, u, v);
                if (!hit) { ++faltas; continue; }
                ++acertos;
                const float u0 = 1.f - u - v;
                const uint32_t i0 = ht * 3, i1 = i0 + 1, i2 = i0 + 2;
                const vec2 uvH = huv[i0] * u0 + huv[i1] * u + huv[i2] * v;
                const vec4 cor = iCor.amostra(uvH);
                // normal: a do modelo detalhado com o normal map dele
                vec3 nH = glm::normalize(hn[i0] * u0 + hn[i1] * u + hn[i2] * v);
                if (temN) {
                    const vec3 e1 = hp[i1] - hp[i0], e2 = hp[i2] - hp[i0];
                    const vec2 d1 = huv[i1] - huv[i0], d2 = huv[i2] - huv[i0];
                    const float det = d1.x * d2.y - d2.x * d1.y;
                    if (std::abs(det) > 1e-14f) {
                        const float r = 1.f / det;
                        vec3 tt = (e1 * d2.y - e2 * d1.y) * r, bb = (e2 * d1.x - e1 * d2.x) * r;
                        tt = tt - nH * glm::dot(nH, tt);
                        if (glm::length(tt) > 1e-10f) {
                            tt = glm::normalize(tt);
                            const vec3 bo = glm::cross(nH, tt) * (glm::dot(glm::cross(nH, tt), bb) < 0.f ? -1.f : 1.f);
                            const vec4 nm = iNormal.amostra(uvH);
                            const vec3 nt = vec3(nm) * 2.f - 1.f;
                            const vec3 nw = tt * nt.x + bo * nt.y + nH * nt.z;
                            if (glm::length(nw) > 1e-6f) nH = glm::normalize(nw);
                        }
                    }
                }
                // no espaço tangente da malha leve (como o shader do motor monta: T, B = cross(N, T) * w, N)
                vec3 Tl = vec3(A.tangent) * w0 + vec3(B.tangent) * w1 + vec3(C.tangent) * w2;
                Tl = Tl - N * glm::dot(N, Tl);
                Tl = glm::length(Tl) > 1e-8f ? glm::normalize(Tl) : glm::normalize(glm::cross(N, std::abs(N.y) < 0.99f ? vec3(0, 1, 0) : vec3(1, 0, 0)));
                const vec3 Bl = glm::cross(N, Tl) * (A.tangent.w < 0.f ? -1.f : 1.f);
                vec3 nt(glm::dot(nH, Tl), glm::dot(nH, Bl), glm::dot(nH, N));
                if (nt.z < 0.05f) nt.z = 0.05f;
                nt = glm::normalize(nt);
                // rugosidade mínima: metal liso demais vira um ponto de luz estourado (o brilho espalha em volta da mira)
                const float metal = temM ? iMetal.amostra(uvH).x : 0.f, rug = std::max(temR ? iRug.amostra(uvH).x : 0.7f, 0.3f);
                for (int k = 0; k < 3; ++k) {
                    oCor[pi * 3 + k] = (unsigned char)std::clamp(int(cor[k] * 255.f + 0.5f), 0, 255);
                    oNor[pi * 3 + k] = (unsigned char)std::clamp(int((nt[k] * 0.5f + 0.5f) * 255.f + 0.5f), 0, 255);
                }
                oOrm[pi * 3] = 255;
                oOrm[pi * 3 + 1] = (unsigned char)std::clamp(int(rug * 255.f + 0.5f), 0, 255);
                oOrm[pi * 3 + 2] = (unsigned char)std::clamp(int(metal * 255.f + 0.5f), 0, 255);
                cheio[pi] = 1;
            }
    }
    std::printf("  assado: %zu pixels (%.0f%%), %zu sem acerto\n", acertos, 100.0 * acertos / (double(T) * T), faltas);
    {
        std::vector<unsigned char> c1 = cheio, c2 = cheio, c3 = cheio;
        dilatar(oCor, c1, T, 3, 12);
        dilatar(oNor, c2, T, 3, 12);
        dilatar(oOrm, c3, T, 3, 12);
    }
    // emissiva: o que é claro e bem colorido acende (energia, raios); o resto fica apagado
    std::vector<unsigned char> oEmi;
    if (brilho >= 0.f) {
        oEmi.resize(oCor.size());
        size_t acesos = 0;
        for (size_t k = 0; k < size_t(T) * T; ++k) {
            const float r = oCor[k * 3] / 255.f, g = oCor[k * 3 + 1] / 255.f, b = oCor[k * 3 + 2] / 255.f;
            const float mxc = std::max({r, g, b}), mnc = std::min({r, g, b}), sat = (mxc - mnc) / std::max(mxc, 1e-4f);
            float m = std::clamp((mxc * (0.35f + 0.65f * sat) - brilho) / (1.f - brilho) * 1.6f, 0.f, 1.f);
            m = std::pow(m, 1.5f);
            // dourado (detalhes de metal) não acende: só a energia (roxo, azul, vermelho); com a aura junto, o
            // dourado aceso estourava em bolas de luz nas pontas finas (massa e alça de mira)
            if (mxc - mnc > 1e-3f) {
                float h = mxc == r ? std::fmod((g - b) / (mxc - mnc), 6.f) : mxc == g ? (b - r) / (mxc - mnc) + 2.f : (r - g) / (mxc - mnc) + 4.f;
                h *= 60.f;
                if (h < 0.f) h += 360.f;
                if (h > 28.f && h < 75.f) m *= 0.1f;
            }
            if (m > 0.05f) ++acesos;
            for (int j = 0; j < 3; ++j) oEmi[k * 3 + j] = (unsigned char)(oCor[k * 3 + j] * m);
        }
        std::printf("  emissiva: %.1f%% da textura acende\n", 100.0 * acesos / (double(T) * T));
    }

    // ---- GLB
    std::vector<char> bin;
    auto pad4 = [&](std::vector<char>& b, char ch) { while (b.size() % 4) b.push_back(ch); };
    auto add = [&](const void* d, size_t n) { size_t off = bin.size(); bin.insert(bin.end(), (const char*)d, (const char*)d + n); pad4(bin, 0); return off; };
    std::vector<float> P, N, U, G;
    mn = vec3(1e30f); mx = vec3(-1e30f);
    for (const Vertex& v : fv) {
        P.insert(P.end(), {v.pos.x, v.pos.y, v.pos.z});
        N.insert(N.end(), {v.normal.x, v.normal.y, v.normal.z});
        U.insert(U.end(), {v.uv.x, v.uv.y});
        G.insert(G.end(), {v.tangent.x, v.tangent.y, v.tangent.z, v.tangent.w});
        mn = glm::min(mn, v.pos); mx = glm::max(mx, v.pos);
    }
    const size_t oP = add(P.data(), P.size() * 4), oN = add(N.data(), N.size() * 4), oU = add(U.data(), U.size() * 4), oG = add(G.data(), G.size() * 4);
    const size_t oI = add(fi.data(), fi.size() * 4);
    std::vector<std::pair<size_t, size_t>> imgs;
    auto jpg = [&](const std::vector<unsigned char>& img, int q) {
        std::vector<char> d;
        stbi_write_jpg_to_func(jpgNaMemoria, &d, T, T, 3, img.data(), q);
        imgs.push_back({add(d.data(), d.size()), d.size()});
    };
    if (!skin.empty()) {  // pintura de skin ao lado; no .glb só o normal map
        stbi_write_jpg((skin + ".jpg").c_str(), T, T, 3, oCor.data(), 88);
        stbi_write_jpg((skin + "_orm.jpg").c_str(), T, T, 3, oOrm.data(), 90);
        if (!oEmi.empty()) stbi_write_jpg((skin + "_emi.jpg").c_str(), T, T, 3, oEmi.data(), 85);
        std::printf("  pintura: %s.jpg, _orm.jpg%s\n", skin.c_str(), oEmi.empty() ? "" : ", _emi.jpg");
        jpg(oNor, 92);
    } else {
        jpg(oCor, 88);
        jpg(oNor, 92);
        jpg(oOrm, 90);
        if (!oEmi.empty()) jpg(oEmi, 85);
    }
    char tmp[512];
    std::string js = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"converter_meshy\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],";
    js += "\"nodes\":[{\"mesh\":0,\"name\":\"modelo\"}],";
    js += "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"TANGENT\":3},\"indices\":4,\"material\":0}]}],";
    if (!skin.empty())
        js += "\"materials\":[{\"pbrMetallicRoughness\":{\"metallicFactor\":0.5,\"roughnessFactor\":0.5},\"normalTexture\":{\"index\":0}";
    else {
        js += "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},\"metallicRoughnessTexture\":{\"index\":2},"
              "\"metallicFactor\":1.0,\"roughnessFactor\":1.0},\"normalTexture\":{\"index\":1}";
        if (!oEmi.empty()) js += ",\"emissiveTexture\":{\"index\":3},\"emissiveFactor\":[1.0,1.0,1.0]";
    }
    js += "}],\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":33071,\"wrapT\":33071}],\"textures\":[";
    for (size_t i = 0; i < imgs.size(); ++i) { std::snprintf(tmp, sizeof(tmp), "%s{\"sampler\":0,\"source\":%zu}", i ? "," : "", i); js += tmp; }
    js += "],\"images\":[";
    for (size_t i = 0; i < imgs.size(); ++i) { std::snprintf(tmp, sizeof(tmp), "%s{\"bufferView\":%zu,\"mimeType\":\"image/jpeg\"}", i ? "," : "", 5 + i); js += tmp; }
    js += "],\"accessors\":[";
    std::snprintf(tmp, sizeof(tmp), "{\"bufferView\":0,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\",\"min\":[%g,%g,%g],\"max\":[%g,%g,%g]},",
                  fv.size(), mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
    js += tmp;
    std::snprintf(tmp, sizeof(tmp), "{\"bufferView\":1,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\"},", fv.size()); js += tmp;
    std::snprintf(tmp, sizeof(tmp), "{\"bufferView\":2,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC2\"},", fv.size()); js += tmp;
    std::snprintf(tmp, sizeof(tmp), "{\"bufferView\":3,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC4\"},", fv.size()); js += tmp;
    std::snprintf(tmp, sizeof(tmp), "{\"bufferView\":4,\"componentType\":5125,\"count\":%zu,\"type\":\"SCALAR\"}]", fi.size()); js += tmp;
    js += ",\"bufferViews\":[";
    const size_t offs[4] = {oP, oN, oU, oG}, lens[4] = {P.size() * 4, N.size() * 4, U.size() * 4, G.size() * 4};
    for (int k = 0; k < 4; ++k) { std::snprintf(tmp, sizeof(tmp), "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34962},", offs[k], lens[k]); js += tmp; }
    std::snprintf(tmp, sizeof(tmp), "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34963}", oI, fi.size() * 4); js += tmp;
    for (auto& im : imgs) { std::snprintf(tmp, sizeof(tmp), ",{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", im.first, im.second); js += tmp; }
    std::snprintf(tmp, sizeof(tmp), "],\"buffers\":[{\"byteLength\":%zu}]}", bin.size()); js += tmp;
    std::vector<char> J(js.begin(), js.end());
    pad4(J, ' ');
    std::ofstream out(pos[1], std::ios::binary);
    auto u32 = [&](uint32_t x) { out.write((const char*)&x, 4); };
    u32(0x46546C67); u32(2); u32(uint32_t(12 + 8 + J.size() + 8 + bin.size()));
    u32(uint32_t(J.size())); u32(0x4E4F534A); out.write(J.data(), J.size());
    u32(uint32_t(bin.size())); u32(0x004E4942); out.write(bin.data(), bin.size());
    std::printf("%s: %zu triângulos (de %zu), %zu vértices, %.2f x %.2f x %.2f m, %zu KB\n", pos[1].c_str(), fi.size() / 3, antes, fv.size(),
                mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, size_t(12 + 8 + J.size() + 8 + bin.size()) / 1024);
    return 0;
}
