// Simplificação de malhas por colapso de arestas (erro quádrico). Usada nas qualidades leves para os
// personagens e armas (que vêm com 50 a 95 mil vértices cada) caberem no celular.
#include "procgen/Simplify.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <unordered_map>

namespace eng {
namespace {

struct Quadric {
    double a[10] = {};  // matriz simétrica 4x4 (aa ab ac ad bb bc bd cc cd dd)
    void addPlane(double x, double y, double z, double d, double w) {
        a[0] += w * x * x; a[1] += w * x * y; a[2] += w * x * z; a[3] += w * x * d;
        a[4] += w * y * y; a[5] += w * y * z; a[6] += w * y * d;
        a[7] += w * z * z; a[8] += w * z * d; a[9] += w * d * d;
    }
    void add(const Quadric& q) { for (int i = 0; i < 10; ++i) a[i] += q.a[i]; }
    double eval(const glm::vec3& p) const {
        const double x = p.x, y = p.y, z = p.z;
        return a[0] * x * x + 2 * a[1] * x * y + 2 * a[2] * x * z + 2 * a[3] * x + a[4] * y * y + 2 * a[5] * y * z +
               2 * a[6] * y + a[7] * z * z + 2 * a[8] * z + a[9];
    }
};

struct Candidate {
    float cost;
    uint32_t u, v, version;
    bool operator<(const Candidate& o) const { return cost > o.cost; }  // fila: o menor custo sai primeiro
};

struct PosHash {
    size_t operator()(const glm::vec3& p) const {
        uint32_t h[3];
        std::memcpy(h, &p, 12);
        return size_t(h[0] * 73856093u ^ h[1] * 19349663u ^ h[2] * 83492791u);
    }
};

}  // namespace

void simplifyMesh(std::vector<Vertex>& vs, std::vector<uint32_t>& idx, float ratio, float maxErro) {
    const size_t triCount = idx.size() / 3;
    if (ratio >= 0.999f || triCount < 300 || vs.empty()) return;
    const uint32_t n = uint32_t(vs.size());
    const size_t target = std::max<size_t>(64, size_t(double(triCount) * ratio));

    // vértices na mesma posição (costuras de textura/normal): mesmo grupo
    std::vector<uint32_t> group(n);
    std::vector<uint32_t> groupSize;
    {
        std::unordered_map<glm::vec3, uint32_t, PosHash> byPos;
        byPos.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            auto it = byPos.emplace(vs[i].pos, uint32_t(groupSize.size()));
            if (it.second) groupSize.push_back(0);
            group[i] = it.first->second;
            ++groupSize[group[i]];
        }
    }
    std::vector<uint8_t> locked(n, 0);
    for (uint32_t i = 0; i < n; ++i) if (groupSize[group[i]] > 1) locked[i] = 1;
    // bordas abertas (aresta com um triângulo só): presas também
    {
        std::unordered_map<uint64_t, int> edges;
        edges.reserve(idx.size());
        auto key = [&](uint32_t a, uint32_t b) {
            uint32_t ga = group[a], gb = group[b];
            if (ga > gb) std::swap(ga, gb);
            return (uint64_t(ga) << 32) | gb;
        };
        for (size_t t = 0; t < triCount; ++t)
            for (int k = 0; k < 3; ++k) ++edges[key(idx[t * 3 + k], idx[t * 3 + (k + 1) % 3])];
        std::vector<uint8_t> borderGroup(groupSize.size(), 0);
        for (const auto& [k, c] : edges)
            if (c == 1) { borderGroup[uint32_t(k >> 32)] = 1; borderGroup[uint32_t(k & 0xffffffffu)] = 1; }
        for (uint32_t i = 0; i < n; ++i) if (borderGroup[group[i]]) locked[i] = 1;
    }

    // triângulos de cada vértice e as quádricas (planos dos triângulos, pesados pela área)
    std::vector<std::vector<uint32_t>> vt(n);
    std::vector<Quadric> Q(n);
    glm::vec3 bmin(1e30f), bmax(-1e30f);
    for (const Vertex& v : vs) { bmin = glm::min(bmin, v.pos); bmax = glm::max(bmax, v.pos); }
    const float diag = glm::length(bmax - bmin);
    if (diag <= 0.f) return;
    const double maxCost = double(maxErro * diag) * double(maxErro * diag);
    std::vector<uint8_t> triAlive(triCount, 1);
    for (size_t t = 0; t < triCount; ++t) {
        const uint32_t a = idx[t * 3], b = idx[t * 3 + 1], c = idx[t * 3 + 2];
        if (a >= n || b >= n || c >= n) { triAlive[t] = 0; continue; }
        glm::vec3 nrm = glm::cross(vs[b].pos - vs[a].pos, vs[c].pos - vs[a].pos);
        float len = glm::length(nrm);
        if (len > 0.f) {
            glm::vec3 un = nrm / len;
            double d = -double(glm::dot(un, vs[a].pos));
            for (uint32_t x : {a, b, c}) Q[x].addPlane(un.x, un.y, un.z, d, len * 0.5);
        }
        vt[a].push_back(uint32_t(t)); vt[b].push_back(uint32_t(t)); vt[c].push_back(uint32_t(t));
    }

    std::vector<uint32_t> version(n, 0);
    std::vector<uint8_t> alive(n, 1);
    std::priority_queue<Candidate> heap;
    // o melhor vizinho para o vértice u se juntar
    auto evaluate = [&](uint32_t u) {
        if (locked[u] || !alive[u]) return;
        float best = 1e30f; uint32_t bv = u;
        for (uint32_t t : vt[u]) {
            if (!triAlive[t]) continue;
            for (int k = 0; k < 3; ++k) {
                uint32_t w = idx[t * 3 + k];
                if (w == u) continue;
                Quadric q = Q[u]; q.add(Q[w]);
                float c = float(std::max(0.0, q.eval(vs[w].pos)));
                if (c < best) { best = c; bv = w; }
            }
        }
        if (bv != u && best <= maxCost) heap.push({best, u, bv, version[u]});
    };
    for (uint32_t u = 0; u < n; ++u) evaluate(u);

    size_t live = 0;
    for (uint8_t a : triAlive) live += a;
    std::vector<uint32_t> touched;
    while (live > target && !heap.empty()) {
        Candidate c = heap.top(); heap.pop();
        const uint32_t u = c.u, v = c.v;
        if (!alive[u] || !alive[v] || c.version != version[u]) continue;
        // o mesmo v (mesma cópia) em todos os triângulos que u divide com o grupo de v; nada pode virar do avesso
        bool ok = true, shares = false;
        for (uint32_t t : vt[u]) {
            if (!triAlive[t]) continue;
            uint32_t* tri = &idx[size_t(t) * 3];
            bool hasV = false, hasGroup = false;
            for (int k = 0; k < 3; ++k) {
                if (tri[k] == v) hasV = true;
                else if (tri[k] != u && group[tri[k]] == group[v]) hasGroup = true;
            }
            if (hasGroup) { ok = false; break; }
            if (hasV) { shares = true; continue; }
            glm::vec3 p[3], q[3];
            for (int k = 0; k < 3; ++k) { p[k] = vs[tri[k]].pos; q[k] = tri[k] == u ? vs[v].pos : p[k]; }
            glm::vec3 n0 = glm::cross(p[1] - p[0], p[2] - p[0]), n1 = glm::cross(q[1] - q[0], q[2] - q[0]);
            float l0 = glm::length(n0), l1 = glm::length(n1);
            if (l1 < 1e-12f || (l0 > 0.f && glm::dot(n0, n1) < 0.25f * l0 * l1)) { ok = false; break; }
        }
        if (!ok || !shares) { ++version[u]; continue; }
        // junta: triângulos com u e v somem, os outros passam a usar v
        for (uint32_t t : vt[u]) {
            if (!triAlive[t]) continue;
            uint32_t* tri = &idx[size_t(t) * 3];
            if (tri[0] == v || tri[1] == v || tri[2] == v) { triAlive[t] = 0; --live; continue; }
            for (int k = 0; k < 3; ++k) if (tri[k] == u) tri[k] = v;
            vt[v].push_back(t);
        }
        alive[u] = 0;
        vt[u].clear();
        Q[v].add(Q[u]);
        // v e os vizinhos precisam de nova escolha
        touched.clear();
        auto& lv = vt[v];
        lv.erase(std::remove_if(lv.begin(), lv.end(), [&](uint32_t t) { return !triAlive[t]; }), lv.end());
        for (uint32_t t : lv) for (int k = 0; k < 3; ++k) touched.push_back(idx[size_t(t) * 3 + k]);
        std::sort(touched.begin(), touched.end());
        touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
        for (uint32_t w : touched) { ++version[w]; evaluate(w); }
    }

    // monta a malha final só com os vértices usados
    std::vector<uint32_t> remap(n, UINT32_MAX);
    std::vector<Vertex> outV;
    std::vector<uint32_t> outI;
    outI.reserve(live * 3);
    for (size_t t = 0; t < triCount; ++t) {
        if (!triAlive[t]) continue;
        for (int k = 0; k < 3; ++k) {
            uint32_t a = idx[t * 3 + k];
            if (remap[a] == UINT32_MAX) { remap[a] = uint32_t(outV.size()); outV.push_back(vs[a]); }
            outI.push_back(remap[a]);
        }
    }
    vs.swap(outV);
    idx.swap(outI);
}

}  // namespace eng
