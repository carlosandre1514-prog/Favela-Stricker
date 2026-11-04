// Mapas do motor: cadastro de geradores (o jogo cadastra os dele), montagem do mapa escolhido e as ferramentas
// de montar mapa (terreno, regiões, ruas, limites) que os geradores usam (engine/MapKit.h).
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include "assets/MeshUtils.h"
#include "engine/Engine.h"
#include "engine/MapKit.h"
#include "procgen/Noise.h"
#include <glm/gtx/matrix_decompose.hpp>

namespace eng {

namespace {
std::vector<MapGenerator>& registry() { static std::vector<MapGenerator> r; return r; }

void releaseChunk(Engine* e, TerrainChunk& c) {
    for (auto h : c.objects)
        if (GameObject* g = e->scene.get(h)) {
            Scene::forEachInSubtree(*g, [&](GameObject& o) { releaseComponents(e, o); });
            e->scene.destroy(h);
        }
    c.objects.clear();
    e->freeGeoMeshes.insert(e->freeGeoMeshes.end(), c.geoMeshes.begin(), c.geoMeshes.end());
    c.geoMeshes.clear();
    // portas/elevadores de objetos que sumiram
    e->doors.erase(std::remove_if(e->doors.begin(), e->doors.end(), [&](const DoorRT& d) { return !e->scene.get(d.h); }), e->doors.end());
    e->teles.erase(std::remove_if(e->teles.begin(), e->teles.end(), [&](const TeleRT& t) { return !e->scene.get(t.root); }), e->teles.end());
    if (GameObject* g = e->scene.get(c.ground)) { releaseComponents(e, *g); e->scene.destroy(c.ground); }
    c.ground = {};
    if (c.hfBody) e->physics.removeStatic(c.hfBody);
    delete c.hfShape;
    c.hfBody = nullptr; c.hfShape = nullptr;
    freeStatic(e, c.props);
    c.used = false;
}
}  // namespace

// ---------------- cadastro de mapas ----------------
void registerMap(const MapGenerator& g) {
    for (MapGenerator& m : registry()) if (m.name == g.name) { m = g; return; }
    registry().push_back(g);
}
int mapIndexOf(const std::string& name) {
    for (size_t i = 0; i < registry().size(); ++i) if (registry()[i].name == name) return int(i);
    return MAP_None;
}
const char* mapNameOf(int kind) { return kind >= 0 && kind < int(registry().size()) ? registry()[kind].name.c_str() : "vazio"; }
bool mapIs(const Engine* e, const char* name) { return e->mapKind >= 0 && e->mapKind < int(registry().size()) && registry()[e->mapKind].name == name; }

// ---------------- ferramentas de montar mapa ----------------
float smooth01(float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); }

float segDist2(glm::vec2 p, glm::vec2 a, glm::vec2 b, float& t) {
    glm::vec2 ab = b - a;
    t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.f, 1.f);
    return glm::length(p - (a + ab * t));
}

void terrainMesh(std::vector<Vertex>& v, std::vector<uint32_t>& idx, float x0, float z0, float size, int res, const HeightFn& H,
                 std::vector<float>* heights, const std::function<glm::vec3(float, float)>* hint) {
    v.clear(); idx.clear();
    const float cell = size / res;
    v.reserve((res + 1) * (res + 1));
    if (heights) heights->resize((res + 1) * (res + 1));
    // alturas numa grade com 1 célula de borda: cada ponto calculado uma vez (as normais saem dos vizinhos)
    const int G = res + 3;
    std::vector<float> hg(G * G);
    for (int j = 0; j < G; ++j)
        for (int i = 0; i < G; ++i) hg[j * G + i] = H(x0 + (i - 1) * cell, z0 + (j - 1) * cell);
    for (int j = 0; j <= res; ++j)
        for (int i = 0; i <= res; ++i) {
            float x = x0 + i * cell, z = z0 + j * cell;
            const float* row = &hg[(j + 1) * G + (i + 1)];
            float h = row[0];
            if (heights) (*heights)[j * (res + 1) + i] = h;
            float hx = row[1] - row[-1], hz = row[G] - row[-G];
            glm::vec3 n = glm::normalize(glm::vec3(-hx, 2.f * cell, -hz));
            Vertex vx = makeVertex({x, h, z}, n, {x / 5.f, z / 5.f});
            vx.tangent = glm::vec4(glm::normalize(glm::cross(glm::vec3(0, 0, 1), n)), 1.f);
            if (hint) vx.color = glm::vec4(1.f - glm::clamp((*hint)(x, z), 0.f, 1.f), 1.f);
            v.push_back(vx);
        }
    for (int j = 0; j < res; ++j)
        for (int i = 0; i < res; ++i) {
            uint32_t a = j * (res + 1) + i;
            for (uint32_t k : {a, a + res + 1, a + 1, a + 1, a + res + 1, a + res + 2}) idx.push_back(k);
        }
}

void addHeightfield(Engine* e, TerrainChunk& c, float x0, float z0, float size, int res) {
    float mn = 1e9f, mx = -1e9f;
    for (float h : c.heights) { mn = std::min(mn, h); mx = std::max(mx, h); }
    auto* hf = new btHeightfieldTerrainShape(res + 1, res + 1, c.heights.data(), 1.f, mn, mx, 1, PHY_FLOAT, false);
    float cell = size / res;
    hf->setLocalScaling({cell, 1.f, cell});
    c.hfShape = hf;
    c.hfBody = e->physics.addStatic(hf, {x0 + size * 0.5f, (mn + mx) * 0.5f, z0 + size * 0.5f});
}

Prototype protoFromGeo(Engine* e, Geo& g) {
    Prototype p;
    for (int k = 0; k < int(Mat::Count); ++k) {
        MeshId id = g.mats[k].build(e->renderer);
        if (id != kInvalidId) p.parts.push_back({id, e->mats[k]});
        MeshId d = g.detailMats[k].build(e->renderer);
        if (d != kInvalidId) p.parts.push_back({d, e->mats[k], 0.f, kDetailFar * 0.6f});
        MeshId in = g.interiorMats[k].build(e->renderer);
        if (in != kInvalidId) p.parts.push_back({in, e->mats[k], 0.f, kInteriorFar});
    }
    if (!g.colliders.empty()) { p.colliderHalf = g.colliders[0].half; p.colliderY = g.colliders[0].center.y; }
    if (g.colliders.size() > 1) p.boxes = g.colliders;  // construção com paredes: um collider por parede
    return p;
}

void spawnPrototype(Engine* e, const Prototype& p, glm::vec3 pos, float yaw, float scale, std::vector<Scene::Handle>* out,
                std::vector<BoxCollider>* colliders) {
    glm::mat4 base = glm::scale(glm::rotate(glm::translate(glm::mat4(1.f), pos), yaw, glm::vec3(0, 1, 0)), glm::vec3(scale));
    for (const ProtoPart& pp : p.parts) {
        glm::vec3 sc(scale), tr = pos, skew; glm::vec4 persp; glm::quat rot = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
        if (pp.local != glm::mat4(1.f)) glm::decompose(base * pp.local, sc, rot, tr, skew, persp);
        Scene::Handle h = addMeshObject(e, "", pp.mesh, pp.mat, tr, sc, glm::degrees(glm::eulerAngles(rot)), false);
        if (GameObject* g = e->scene.get(h); g && g->meshRenderer) { g->meshRenderer->lodNear = pp.lodNear; g->meshRenderer->lodFar = pp.lodFar; }
        if (out) out->push_back(h);
    }
    if (colliders && !p.boxes.empty()) {
        glm::quat q = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
        for (const BoxCollider& b : p.boxes) colliders->push_back({pos + q * (b.center * scale), b.half * scale, yaw + b.yaw, b.pitch});
    } else if (colliders && p.colliderHalf.x > 0.f)
        colliders->push_back({pos + glm::vec3(0, p.colliderY * scale, 0), p.colliderHalf * scale, yaw});
}

void Cells::flush(Engine* e) {
    for (auto& [k, g] : m) { e->mapLamps.insert(e->mapLamps.end(), g.lamps.begin(), g.lamps.end()); g.lamps.clear(); }
    std::map<int64_t, Geo> big;
    auto fdiv = [](int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    for (auto& [k, g] : m) {
        int64_t cx = k >> 32, cz = int64_t(int32_t(uint32_t(k)));
        Geo& b = big[key(fdiv(cx, group), fdiv(cz, group))];
        for (int i = 0; i < int(Mat::Count); ++i) {
            b.mats[i].append(g.mats[i]);
            g.mats[i].clear();
        }
        b.colliders.insert(b.colliders.end(), g.colliders.begin(), g.colliders.end());
        g.colliders.clear();
        b.doors.insert(b.doors.end(), g.doors.begin(), g.doors.end());
        g.doors.clear();
        b.teles.insert(b.teles.end(), g.teles.begin(), g.teles.end());
        g.teles.clear();
        bool anyDetail = false;
        for (auto& d : g.detailMats) anyDetail |= !d.empty();
        for (auto& d : g.interiorMats) anyDetail |= !d.empty();
        for (auto& d : g.farMats) anyDetail |= !d.empty();
        if (anyDetail) spawnGeo(e, g, "Detalhes", false, glm::vec3(0.f), 0.f, detailFar);
    }
    for (auto& [k, g] : big) if (!g.empty() || !g.colliders.empty() || !g.doors.empty()) spawnGeo(e, g, "Mapa", false);
    m.clear();
}

void setBounds(Engine* e, glm::vec2 mn, glm::vec2 mx) {
    e->hasBounds = true;
    e->boundsMin = mn; e->boundsMax = mx;
    // barreira invisível: 4 paredes estáticas sem malha
    std::vector<BoxCollider> walls;
    glm::vec2 c = (mn + mx) * 0.5f, h = (mx - mn) * 0.5f;
    const float H = 80.f, T = 1.f;
    walls.push_back({{c.x, H, mn.y - T}, {h.x + 2 * T, H + 20.f, T}, 0.f});
    walls.push_back({{c.x, H, mx.y + T}, {h.x + 2 * T, H + 20.f, T}, 0.f});
    walls.push_back({{mn.x - T, H, c.y}, {T, H + 20.f, h.y + 2 * T}, 0.f});
    walls.push_back({{mx.x + T, H, c.y}, {T, H + 20.f, h.y + 2 * T}, 0.f});
    addStaticBoxes(e, walls);
}

void roadStrip(MeshBuilder& b, glm::vec2 a, glm::vec2 c, float w, float y) {
    glm::vec2 d = c - a;
    float L = glm::length(d);
    if (L < 0.01f) return;
    glm::vec2 dir = d / L, side(-dir.y, dir.x);
    glm::vec3 p0(a.x - side.x * w * 0.5f, y, a.y - side.y * w * 0.5f), p1(c.x - side.x * w * 0.5f, y, c.y - side.y * w * 0.5f);
    glm::vec3 p2(c.x + side.x * w * 0.5f, y, c.y + side.y * w * 0.5f), p3(a.x + side.x * w * 0.5f, y, a.y + side.y * w * 0.5f);
    glm::vec2 uv[4] = {{0, 0}, {0, L / 6.f}, {1, L / 6.f}, {1, 0}};
    // garante normal para cima
    glm::vec3 n = glm::cross(p1 - p0, p3 - p0);
    if (n.y < 0.f) { std::swap(p1, p3); std::swap(uv[1], uv[3]); }
    b.quad(p0, p1, p2, p3, uv);
}

void groundBox(Geo& g, Mat m, glm::vec2 mn, glm::vec2 mx, float top, float tile) {
    MeshBuilder& b = g[m];
    b.uvTile = {tile, tile};
    b.box({mn.x, top - 0.3f, mn.y}, {mx.x - mn.x, 0.3f, mx.y - mn.y}, 4);
    g.collider({mn.x, top - 0.3f, mn.y}, {mx.x - mn.x, 0.3f, mx.y - mn.y});
}

void segBox(MeshBuilder& m, glm::vec3 a, glm::vec3 b, float t, float t2) {
    glm::vec3 d = b - a;
    float L = glm::length(d);
    if (L < 1e-4f) return;
    glm::vec3 z = d / L, x = glm::normalize(glm::cross(std::abs(z.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), z)), y = glm::cross(z, x);
    glm::mat4 keep = m.xf;
    m.xf = keep * glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4(a, 1));
    if (t2 < 0.f) t2 = t;
    m.box({-t / 2, -t2 / 2, 0.f}, {t, t2, L});
    m.xf = keep;
}

void yawBox(MeshBuilder& m, glm::vec3 base, glm::vec3 size, float yaw, unsigned skipFaces) {
    glm::mat4 keep = m.xf;
    m.xf = glm::rotate(glm::translate(keep, base), yaw, glm::vec3(0, 1, 0));
    m.box({-size.x / 2, 0.f, -size.z / 2}, size, skipFaces);
    m.xf = keep;
}

void wire(MeshBuilder& m, glm::vec3 a, glm::vec3 b, float sag) {
    const int n = 4;
    glm::vec3 prev = a;
    for (int i = 1; i <= n; ++i) {
        float t = float(i) / n;
        glm::vec3 p = glm::mix(a, b, t) - glm::vec3(0, sag * 4.f * t * (1.f - t), 0);
        segBox(m, prev, p, 0.025f);
        prev = p;
    }
}

void muralQuad(Geo& g, int idx, glm::vec3 c, glm::vec3 n, float w) {
    MeshBuilder& m = g[Mat::Mural];
    m.color = glm::vec4(1.f); m.uvTile = {0.f, 0.f};
    glm::vec3 right = glm::normalize(glm::cross(-n, glm::vec3(0, 1, 0))), up(0, 1, 0);
    float h = w * 0.5f;
    glm::vec4 uv = muralUV(idx);
    glm::vec2 q[4] = {{uv.x, uv.w}, {uv.z, uv.w}, {uv.z, uv.y}, {uv.x, uv.y}};
    glm::vec3 o = c + n * 0.02f;
    m.quad(o - right * (w / 2) - up * (h / 2), o + right * (w / 2) - up * (h / 2), o + right * (w / 2) + up * (h / 2), o - right * (w / 2) + up * (h / 2), q);
}

// ---------------- o mapa atual ----------------
float mapHeight(Engine* e, float x, float z) {
    if (e->mapKind < 0 || e->mapKind >= int(registry().size()) || !registry()[e->mapKind].height) return 0.f;
    return registry()[e->mapKind].height(e, x, z);
}

void clearWorld(Engine* e) {
    for (auto& c : e->chunks) if (c.used) releaseChunk(e, c);
    e->chunks.clear();
    e->freeGeoMeshes.clear();  // as malhas somem junto com a cena: reaproveitar ids velhos apagava as construções
    for (auto& s : e->statics) freeStatic(e, s);
    e->statics.clear();
    e->mapKind = MAP_None;
    e->hasBounds = false;
    e->water = {};
    e->mapLamps.clear();
}

// Monta o mapa cadastrado `kind` (índice do cadastro) com a semente `seed`
void loadMap(Engine* e, int kind, uint32_t seed) {
    clearScene(e);
    if (kind < 0 || kind >= int(registry().size())) return;
    e->mapKind = kind;
    e->mapSeed = seed;
    e->dayCycleMin = 0.f;  // cada mapa escolhe (ciclo do dia, chuva)
    e->rainMode = 0;
    Rng r(seed);
    glm::vec3 spawn(0.f);
    float spawnYaw = 0.f;
    registry()[kind].generate(e, r, spawn, spawnYaw);
    e->mapKind = kind;
    mapOverview(e);  // câmera começa vendo o mapa de cima
}

Scene::Handle createBuilding(Engine* e, int kind, uint32_t seed, glm::vec3 pos, int floors) {
    Rng r(seed);
    Geo g;
    g.place(glm::vec3(0.f), 0.f);
    const char* name = "Construcao";
    switch (kind) {
        case 0: buildHouse(g, r, r.range(7.f, 10.f), r.range(8.f, 11.f), floors > 0 ? std::min(floors, 3) : 1, housePaint(r), r.chance(0.6f)); name = "Casa"; break;
        case 1: buildHouse(g, r, r.range(8.f, 11.f), r.range(9.f, 12.f), floors > 0 ? std::min(floors, 3) : 2, housePaint(r), true); name = "Sobrado"; break;
        case 2: {
            int f = r.irange(1, 3);  // sorteia sempre (mantém o resto do sorteio igual em projetos antigos)
            buildBarraco(g, r, r.range(3.5f, 5.f), r.range(3.5f, 5.f), floors > 0 ? std::min(floors, 6) : f, r.chance(0.5f), favelaPaint(r), 0.f);
            name = "Barraco";
            break;
        }
        case 3: { int f = r.irange(4, 12); buildApartment(g, r, r.range(15.f, 21.f), r.range(13.f, 18.f), floors > 0 ? floors : f, buildingPaint(r)); name = "Predio"; break; }
        case 4: { int f = r.irange(25, 55); buildSkyscraper(g, r, r.range(24.f, 34.f), r.range(22.f, 30.f), floors > 0 ? floors : f); name = "Arranha-ceu"; break; }
        case 5: buildSchoolWing(g, 40.f, 12.f, 2, srgb(245, 232, 205), srgb(40, 90, 170)); name = "Escola"; break;
        case 6: case 7: case 8:
            buildTree(g[Mat::Painted], r, kind - 6);
            g.collider({-0.25f, 0.f, -0.25f}, {0.5f, 4.f, 0.5f});
            name = kind == 6 ? "Arvore" : kind == 7 ? "Pinheiro" : "Palmeira";
            break;
        case 9: buildCar(g, r); name = "Carro"; break;
        case 10: buildStreetLight(g); g.collider({-0.15f, 0, -0.15f}, {0.3f, 7.f, 0.3f}); name = "Poste"; break;
        case 12: case 13: {  // trecho de rua (ao longo de Z) / cruzamento
            MeshBuilder& m = g[kind == 12 ? Mat::Road : Mat::Asphalt];
            float W = 8.f, L = kind == 12 ? 12.f : 8.f;
            glm::vec2 uv[4] = {{0, L / 6.f}, {1, L / 6.f}, {1, 0}, {0, 0}};
            if (kind == 13) { uv[0] = {0, 2}; uv[1] = {2, 2}; uv[2] = {2, 0}; uv[3] = {0, 0}; }
            m.quad({-W / 2, 0.03f, L / 2}, {W / 2, 0.03f, L / 2}, {W / 2, 0.03f, -L / 2}, {-W / 2, 0.03f, -L / 2}, uv);
            g.collider({-W / 2, -0.2f, -L / 2}, {W, 0.23f, L});
            name = kind == 12 ? "Rua" : "Cruzamento";
            break;
        }
        case 14: {  // calçada com meio-fio
            MeshBuilder& m = g[Mat::Sidewalk];
            m.uvTile = {2.f, 2.f};
            m.box({-1.5f, 0, -6.f}, {3.f, 0.15f, 12.f});
            g.collider({-1.5f, -0.2f, -6.f}, {3.f, 0.35f, 12.f});
            name = "Calcada";
            break;
        }
        case 15: case 16: {  // escada / rampa: sobem para +Z (o personagem consegue subir)
            float W = 2.f, H = 3.f, L = kind == 15 ? 5.f : 8.f;
            MeshBuilder& m = g[Mat::Concrete];
            m.uvTile = {2.f, 2.f};
            if (kind == 15) {
                int n = 16;
                for (int i = 0; i < n; ++i) m.box({-W / 2, 0, i * L / n}, {W, H * (i + 1) / n, L / n + 0.001f});
            } else {
                m.quad({W / 2, 0, 0}, {-W / 2, 0, 0}, {-W / 2, H, L}, {W / 2, H, L});
                m.quad({-W / 2, 0, L}, {W / 2, 0, L}, {W / 2, H, L}, {-W / 2, H, L});
                m.tri({-W / 2, 0, 0}, {-W / 2, 0, L}, {-W / 2, H, L}, {0, 0}, {1, 0}, {1, 1});
                m.tri({W / 2, 0, 0}, {W / 2, H, L}, {W / 2, 0, L}, {0, 0}, {1, 1}, {1, 0});
            }
            float th = std::atan2(H, L), len = std::sqrt(H * H + L * L);
            glm::vec3 nrm(0.f, std::cos(th), -std::sin(th));
            g.colliderTilted(glm::vec3(0.f, H * 0.5f, L * 0.5f) - nrm * 0.15f, {W * 0.5f, 0.15f, len * 0.5f}, -th);
            g.collider({-W / 2, 0, L - 0.2f}, {W, H, 0.2f});
            name = kind == 15 ? "Escada" : "Rampa";
            break;
        }
        case 17: {  // piso / laje (plataforma)
            MeshBuilder& m = g[Mat::Concrete];
            m.uvTile = {2.f, 2.f};
            m.box({-3.f, 0, -3.f}, {6.f, 0.3f, 6.f});
            g.collider({-3.f, 0, -3.f}, {6.f, 0.3f, 6.f});
            name = "Piso";
            break;
        }
        case 18: {  // parede de reboco
            MeshBuilder& m = g[Mat::Plaster];
            m.color = housePaint(r);
            m.uvTile = {2.f, 2.f};
            m.box({-2.f, 0, -0.1f}, {4.f, 3.f, 0.2f});
            g.collider({-2.f, 0, -0.1f}, {4.f, 3.f, 0.2f});
            name = "Parede";
            break;
        }
        case 19: {  // cerca de madeira
            MeshBuilder& m = g[Mat::Wood];
            for (int i = 0; i <= 4; ++i) m.box({-2.f + i - 0.05f, 0, -0.05f}, {0.1f, 1.2f, 0.1f});
            m.box({-2.f, 0.35f, -0.03f}, {4.f, 0.1f, 0.06f});
            m.box({-2.f, 0.9f, -0.03f}, {4.f, 0.1f, 0.06f});
            g.collider({-2.f, 0, -0.1f}, {4.f, 1.2f, 0.2f});
            name = "Cerca";
            break;
        }
        case 20: {  // banco de praça
            MeshBuilder& w = g[Mat::Wood];
            for (int i = 0; i < 4; ++i) w.box({-0.9f, 0.42f, -0.25f + i * 0.13f}, {1.8f, 0.04f, 0.1f});
            for (int i = 0; i < 3; ++i) w.box({-0.9f, 0.55f + i * 0.13f, 0.27f}, {1.8f, 0.1f, 0.04f});
            MeshBuilder& m = g[Mat::GunMetal];
            for (float x : {-0.8f, 0.75f}) { m.box({x, 0, -0.25f}, {0.05f, 0.42f, 0.05f}); m.box({x, 0, 0.25f}, {0.05f, 0.95f, 0.05f}); }
            g.collider({-0.9f, 0, -0.3f}, {1.8f, 0.5f, 0.6f});
            name = "Banco";
            break;
        }
        case 21: buildWaterTank(g[Mat::Painted], glm::vec3(0.f)); g.collider({-0.7f, 0, -0.7f}, {1.4f, 1.4f, 1.4f}); name = "Caixa d'agua"; break;
        default: {
            MeshBuilder& w = g[Mat::Brick];
            w.uvTile = {1.f, 1.f};
            w.box({-5.f, 0, -0.1f}, {10.f, 2.2f, 0.2f});
            g.collider({-5.f, 0, -0.1f}, {10.f, 2.2f, 0.2f});
            name = "Muro";
        }
    }
    pos.y = mapHeight(e, pos.x, pos.z);
    return spawnGeo(e, g, name, true, pos, 0.f);
}

void mapOverview(Engine* e) {
    if (e->hasBounds) {
        glm::vec2 c = (e->boundsMin + e->boundsMax) * 0.5f, ext = e->boundsMax - e->boundsMin;
        e->editorCam.target = {c.x, mapHeight(e, c.x, c.y) * 0.5f, c.y};
        e->editorCam.distance = std::max(ext.x, ext.y) * 0.85f;
    } else {
        glm::vec3 t = e->editorCam.target;
        e->editorCam.target = {t.x, mapHeight(e, t.x, t.z), t.z};
        e->editorCam.distance = 220.f;
    }
    e->editorCam.pitch = 0.75f;
    e->editorCam.yaw = 3.14159265f + 0.5f;
}

}  // namespace eng
