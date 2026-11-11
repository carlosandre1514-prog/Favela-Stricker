// Favela Stricker — o mapa da Favela: o morro com patamares e escadarias, as casas, os becos, o campinho, a praça,
// a entrada e a rua em volta. É conteúdo do jogo: usa as ferramentas de mapa do motor (engine/MapKit.h) e se
// cadastra no motor com o nome "favela" (o projeto da partida pede "mapa": {"tipo": "favela"}).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include "engine/Engine.h"
#include "engine/MapKit.h"
#include "jogo/MapaFavela.h"
#include "procgen/Noise.h"
#include "procgen/TextureGen.h"

namespace eng {

namespace {

struct FavelaPad { glm::vec2 c, half; float y; };
enum { FP_Field, FP_High, FP_Square, FP_Entrance, FP_Low, FP_Count };

float smoothStepTo(float a, float b, float x) { return smooth01(a, b, x); }

float favelaEdge(float x, float z) {
    float ax = std::abs(x) / 64.f, az = std::abs(z) / 62.f;
    return std::pow(ax * ax * ax * ax + az * az * az * az, 0.25f);
}

// Morro sem os patamares
float favelaRaw(float x, float z, uint32_t seed) {
    float n = fbm(x * 0.03f, z * 0.03f, 3, seed) - 0.5f;
    float hb = 10.f + 14.f * smooth01(45.f, -50.f, z)
             + 4.f * smooth01(-25.f, -55.f, x) * smooth01(-5.f, -45.f, z)                              // área alta (noroeste)
             - 4.f * smooth01(26.f, 46.f, x) * smooth01(-12.f, 12.f, z) * (1.f - smooth01(40.f, 56.f, z))  // área baixa (leste)
             + n * 2.4f;
    float hv = 4.2f + (fbm(x * 0.02f + 7.f, z * 0.02f, 3, seed + 5) - 0.5f) * 3.f;  // baixada em volta
    float s = favelaEdge(x, z);
    hv = glm::mix(hv, 4.2f, smooth01(1.38f, 1.5f, s));  // rua em volta do morro: plana
    // o morro desce devagar até a rua de baixo (casas por toda a encosta)
    float slope = smooth01(0.72f, 1.46f, s + n * 0.03f);
    float h = glm::mix(hb, hv, slope);
    // rua de acesso (entrada principal): desce do platô até a rua do bairro
    auto ramp = [&](glm::vec2 a, glm::vec2 b, float ya, float yb, float w) {
        float t, d = segDist2({x, z}, a, b, t);
        float k = 1.f - smooth01(w * 0.5f, w * 0.5f + 2.5f, d);
        h = glm::mix(h, glm::mix(ya, yb, t), k);
    };
    ramp({0.f, 56.f}, {0.f, 96.f}, 10.f, 4.2f, 8.f);
    return h;
}

const FavelaPad* favelaPads(uint32_t seed) {
    static FavelaPad pads[FP_Count];
    static uint32_t cached = 0xffffffffu;
    if (cached != seed) {
        cached = seed;
        pads[FP_Field] = {{2.f, -41.f}, {16.f, 11.f}, 0.f};
        pads[FP_High] = {{-46.f, -45.f}, {9.f, 8.f}, 0.f};
        pads[FP_Square] = {{-33.f, 27.f}, {12.f, 9.f}, 0.f};
        pads[FP_Entrance] = {{0.f, 49.f}, {10.f, 7.f}, 0.f};
        pads[FP_Low] = {{45.f, 22.f}, {10.f, 12.f}, 0.f};
        for (FavelaPad& p : pads) p.y = std::round(favelaRaw(p.c.x, p.c.y, seed) * 2.f) * 0.5f;
        pads[FP_Entrance].y = 10.f;
    }
    return pads;
}

// Escadarias principais (rotas amarelas do mapa): saem da entrada, da praça e da área baixa e sobem até o campinho
const std::vector<std::vector<glm::vec2>>& favelaRoutes() {
    static const std::vector<std::vector<glm::vec2>> routes = {
        {{0, 42}, {0, 22}, {-14, 10}, {-14, -14}, {-6, -29}},                // central, da entrada ao campinho
        {{8, 42}, {24, 30}, {26, 6}, {20, -14}, {12, -29}},                  // leste
        {{-21, 33}, {-36, 14}, {-40, -12}, {-44, -36}},                      // oeste, da praça à área alta
        {{35, 18}, {40, -6}, {30, -26}, {19, -31}},                          // da área baixa ao campinho
        {{-21, 22}, {0, 22}}, {{24, 30}, {35, 26}}, {{-40, -12}, {-14, -14}}, {{20, -14}, {40, -6}},
        // acessos pelos 4 lados: escadarias que sobem da rua de baixo
        {{3, -97}, {4, -70}, {2, -53}},                                      // norte -> campinho
        {{99, 20}, {72, 21}, {56, 22}},                                      // leste -> área baixa
        {{-99, 26}, {-72, 27}, {-46, 27}},                                   // oeste -> praça
        {{-38, 98}, {-35, 66}, {-33, 37}},                                   // sudoeste -> praça
        {{52, 96}, {48, 64}, {45, 35}},                                      // sudeste -> área baixa
        {{-92, -58}, {-68, -50}, {-56, -45}},                                // noroeste -> área alta
        {{84, -66}, {50, -40}, {19, -40}},                                   // nordeste -> campinho
    };
    return routes;
}
struct PadEntry { int pad; glm::vec2 a, b; };  // rampa suave de a (na borda do patamar) até b (para fora, ao longo da rota)
const std::vector<PadEntry>& favelaEntries(uint32_t seed);

float padDist(const FavelaPad& p, float x, float z) {
    return std::max(std::abs(x - p.c.x) - p.half.x, std::abs(z - p.c.y) - p.half.y);
}

const std::vector<PadEntry>& favelaEntries(uint32_t seed) {
    static std::vector<PadEntry> list;
    static uint32_t cached = 0xffffffffu;
    if (cached == seed) return list;
    cached = seed;
    list.clear();
    const FavelaPad* pads = favelaPads(seed);
    for (const auto& rt : favelaRoutes())
        for (int end = 0; end < 2; ++end) {
            glm::vec2 e = end ? rt.back() : rt.front(), prev = end ? rt[rt.size() - 2] : rt[1];
            for (int k = 0; k < FP_Count; ++k)
                if (padDist(pads[k], e.x, e.y) < 4.f) list.push_back({k, e, e + glm::normalize(prev - e) * 8.f});
        }
    // escadaria que atravessa um patamar (entra por um lado e sai pelo outro): passagem também onde ela cruza a
    // borda, senão o muro de arrimo fica fechado no meio do caminho
    for (const auto& rt : favelaRoutes())
        for (size_t i = 0; i + 1 < rt.size(); ++i)
            for (int k = 0; k < FP_Count; ++k) {
                const glm::vec2 a = rt[i], b = rt[i + 1];
                const int passos = std::max(2, int(glm::length(b - a) / 0.5f));
                auto dentro = [&](float t) { glm::vec2 q = glm::mix(a, b, t); return padDist(pads[k], q.x, q.y) < 0.f; };
                for (int sI = 0; sI < passos; ++sI) {  // procura ao longo do trecho cada vez que ele cruza a borda
                    float lo = float(sI) / passos, hi = float(sI + 1) / passos;
                    const bool dLo = dentro(lo);
                    if (dLo == dentro(hi)) continue;
                    for (int it = 0; it < 20; ++it) { float m = (lo + hi) * 0.5f; if (dentro(m) == dLo) lo = m; else hi = m; }
                    const glm::vec2 c = glm::mix(a, b, (lo + hi) * 0.5f);
                    bool perto = false;
                    for (const PadEntry& en : list) if (en.pad == k && glm::length(en.a - c) < 4.f) perto = true;
                    if (perto) continue;  // já é a ponta da escadaria
                    const glm::vec2 fora = glm::normalize(dLo ? b - a : a - b);  // saindo do patamar
                    list.push_back({k, c, c + fora * 8.f});
                }
            }
    return list;
}

float favelaHeight(float x, float z, uint32_t seed) {
    float h = favelaRaw(x, z, seed);
    const FavelaPad* pads = favelaPads(seed);
    const auto& entries = favelaEntries(seed);
    for (int k = 0; k < FP_Count; ++k) {
        float d = padDist(pads[k], x, z);
        if (d > 11.f) continue;
        float blend = 1.6f;
        for (const PadEntry& en : entries) {  // onde a escadaria chega, o patamar desce em rampa
            if (en.pad != k) continue;
            float t, ds = segDist2({x, z}, en.a, en.b, t);
            if (ds < 4.f) blend = std::max(blend, glm::mix(1.6f, 9.f, 1.f - smooth01(2.f, 4.f, ds)));
        }
        if (d < blend) h = glm::mix(h, pads[k].y, 1.f - smooth01(0.f, blend, d));
    }
    return h;
}

// chao(x, z): o chão naquele ponto (coordenadas da casa) em relação ao piso do térreo (negativo: mais baixo)
void favelaHouse(Geo& g, Rng& r, float w, float d, int floors, float footLow, bool frontOpen,
                 const std::function<float(float, float)>& chao) {
    glm::vec4 paint = favelaPaint(r);
    const bool brickBase = r.chance(0.3f), graffitiBase = !brickBase && r.chance(0.18f);
    const bool rawUpper = r.chance(0.45f);
    const bool outsideStairs = r.chance(0.5f);
    const bool porchGround = frontOpen && r.chance(0.3f);
    // Escada externa do lado -X. Com mais de um andar: zigue-zague em 2 corredores de 0,9 m (cada andar no outro
    // corredor e no outro sentido, com patamar no fim de cada lance; o último lance chega colado na laje do
    // telhado). Casa estreita ou curta demais para isso: a escada externa só vai até a primeira laje.
    const bool zigzag = outsideStairs && floors > 1 && w - 1.85f >= 2.4f && d >= 5.f;  // fundo para lance + 2 patamares
    const float lane = zigzag ? 0.9f : 1.f;
    const float stairsBand = outsideStairs ? (zigzag ? 2.f * lane : lane) + 0.05f : 0.f;
    float bw = w - stairsBand;
    float bx = stairsBand * 0.5f;
    float bd = porchGround ? d - 2.f : d;              // varanda coberta na frente
    float bz = porchGround ? -1.f : 0.f;
    // fundação até o chão (terreno inclinado)
    {
        MeshBuilder& f = g[brickBase ? Mat::Brick : Mat::Concrete];
        f.color = glm::vec4(1.f); f.uvTile = {1.f, 1.f};
        if (footLow > 0.05f) {
            f.box({-w / 2, -footLow - 0.3f, -d / 2}, {w, footLow + 0.3f, d});
            g.collider({-w / 2, -footLow - 0.3f, -d / 2}, {w, footLow + 0.3f, d});
        }
    }
    float y = 0.f;
    DetailScope houseDetail(g);  // casa completa só de perto; de longe vale o bloco simples (no fim)
    g.offset({bx, 0.f, bz});
    for (int f = 0; f < floors; ++f) {
        ModuleSpec ms;
        ms.w = bw; ms.d = bd; ms.h = 2.7f;
        ms.floor = f == 0;
        ms.slab = true;
        ms.wallMat = f == 0 ? (brickBase ? Mat::Brick : graffitiBase ? Mat::Graffiti : Mat::Plaster)
                            : (rawUpper ? Mat::Brick : brickBase ? Mat::Brick : Mat::Plaster);
        ms.wallColor = paint;
        ms.floorMat = r.chance(0.6f) ? Mat::Ceramic : Mat::Concrete;
        ms.walls[0] = f == 0 ? MW_Door : (r.chance(0.8f) ? MW_Window : MW_Plain);
        ms.walls[1] = r.chance(0.5f) ? MW_Window : MW_Plain;
        ms.walls[2] = r.chance(0.25f) ? MW_Window : MW_Plain;
        ms.walls[3] = r.chance(0.25f) ? MW_Window : MW_Plain;
        if (bw < 3.2f || bd < 3.2f) ms.walls[2] = ms.walls[3] = MW_Plain;
        // acesso à laje: escada externa em todo andar, ou interna (reta) quando cabe
        if (outsideStairs && (zigzag || f == 0)) {
            ms.stairs = SK_Outside;
            ms.stairsW = lane;
            const int fromTop = floors - 1 - f;               // o último lance fica no corredor colado na casa
            ms.stairsLane = zigzag ? fromTop % 2 : 0;
            ms.stairsToFront = zigzag && f % 2 == 1;         // lances alternam o sentido
            ms.stairsZigzag = zigzag;
            if (f == 0 && chao) {  // o primeiro lance começa no chão de verdade (no lado de baixo do morro)
                const float sx = bx - bw / 2 - lane * float(ms.stairsLane + 1) + lane * 0.5f;
                const float sz = bz + (ms.stairsToFront ? -bd / 2 : bd / 2);
                ms.stairsDrop = std::clamp(-chao(sx, sz), 0.f, 4.f);
                if (std::getenv("MAPA_ESCADAS")) std::printf("  escada externa: começa %.2f m abaixo do piso\n", ms.stairsDrop);
            }
        } else if (outsideStairs) {
        } else if (bd >= 4.4f && bw >= 3.4f) ms.stairs = SK_Straight;
        ms.porch = f == 0 ? porchGround : (frontOpen && r.chance(0.3f));
        ms.porchD = f == 0 ? 2.f : 1.1f;
        ms.parapet = f == floors - 1 && r.chance(0.35f);
        g.offset({0.f, y, 0.f});
        buildModule(g, ms);
        g.offset({0.f, -y, 0.f});
        y += moduleTop(ms);
    }
    g.offset({-bx, 0.f, -bz});
    // pichação/grafite numa parede lateral lisa do térreo
    if (r.chance(0.28f) && bd > 3.2f) {
        bool plus = outsideStairs || r.chance(0.5f);  // do lado da escada externa não
        float mw = std::min(bd - 0.6f, 3.2f);
        muralQuad(g, r.irange(0, kMuralCount - 1), {bx + (plus ? bw / 2 : -bw / 2), 0.15f + 0.4f + mw * 0.25f, bz},
                  {plus ? 1.f : -1.f, 0.f, 0.f}, mw);
    }
    // laje: caixa d'água, telhado de fibrocimento, varal, antena, parabólica, vergalhões
    DetailScope ds(g);
    const float top = y;
    MeshBuilder& p = g[Mat::Painted];
    p.uvTile = {0.f, 0.f};
    // caixa d'água: com escada externa (lado -X), sempre do outro lado (antes às vezes ficava bem onde a escada chega
    // na laje e tapava a subida)
    const bool txMais = r.chance(0.5f) || outsideStairs;
    float tx = txMais ? bx + bw / 2 - 0.8f : bx - bw / 2 + 0.8f, tz = bz - bd / 2 + 0.8f;
    if (r.chance(0.85f)) {
        MeshBuilder& c = g[Mat::Concrete];
        c.color = glm::vec4(1.f); c.uvTile = {1.f, 1.f};
        c.box({tx - 0.7f, top, tz - 0.7f}, {1.4f, 0.25f, 1.4f});
        buildWaterTank(p, {tx, top + 0.25f, tz});
        g.collider({tx - 0.7f, top, tz - 0.7f}, {1.4f, 1.2f, 1.4f});
    }
    if (r.chance(0.35f)) {  // meia-água de telha ondulada em cima de parte da laje
        MeshBuilder& t = g[Mat::Corrugated];
        t.color = glm::vec4(1.f); t.uvTile = {2.f, 2.f};
        float x0 = bx - bw / 2, x1 = bx + bw / 2, z0 = bz + bd * 0.05f, z1 = bz + bd / 2;
        float y0 = top + 2.2f, y1 = top + 1.9f;
        t.quad({x0, y0, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y0, z0});
        MeshBuilder& m = g[Mat::Wood];
        m.color = glm::vec4(1.f); m.uvTile = {0.f, 0.f};
        for (float x : {x0 + 0.1f, x1 - 0.2f}) m.box({x, top, z1 - 0.2f}, {0.1f, 1.9f, 0.1f});
    }
    if (r.chance(0.35f)) {  // varal com roupa
        MeshBuilder& m = g[Mat::GunMetal];
        m.color = glm::vec4(1.f); m.uvTile = {0.f, 0.f};
        glm::vec3 a(bx - bw / 2 + 0.3f, top, bz + 0.3f), b(bx + bw / 2 - 0.3f, top, bz + 0.3f);
        m.box(a - glm::vec3(0.03f, 0, 0.03f), {0.05f, 1.7f, 0.05f});
        m.box(b - glm::vec3(0.03f, 0, 0.03f), {0.05f, 1.7f, 0.05f});
        segBox(m, a + glm::vec3(0, 1.65f, 0), b + glm::vec3(0, 1.65f, 0), 0.015f);
        for (float x = a.x + 0.3f; x < b.x - 0.3f; x += r.range(0.45f, 0.7f)) {
            p.color = favelaPaint(r);
            p.box({x, top + 1.05f + r.range(0.f, 0.2f), a.z - 0.01f}, {r.range(0.3f, 0.5f), r.range(0.35f, 0.55f), 0.02f});
        }
    }
    if (r.chance(0.3f)) {  // parabólica
        p.color = srgb(220, 220, 215);
        glm::vec3 c(bx + bw / 2 - 0.5f, top + 0.9f, bz + bd / 2 - 0.5f);
        p.cylinder(c, c + glm::vec3(0.2f, 0.12f, 0.25f), 0.45f, 0.05f, 10);
        MeshBuilder& m = g[Mat::GunMetal];
        m.box({c.x - 0.03f, top, c.z - 0.03f}, {0.06f, 0.9f, 0.06f});
    }
    if (r.chance(0.3f)) {  // vergalhões nos cantos (obra que vai continuar)
        MeshBuilder& m = g[Mat::GunMetal];
        for (float sx : {-1.f, 1.f})
            for (float sz : {-1.f, 1.f}) {
                glm::vec3 c(bx + sx * (bw / 2 - 0.1f), top, bz + sz * (bd / 2 - 0.1f));
                for (int k = 0; k < 3; ++k) segBox(m, c + glm::vec3(k * 0.04f, 0, 0), c + glm::vec3(k * 0.04f + r.range(-0.05f, 0.05f), r.range(0.6f, 1.1f), 0), 0.02f);
            }
    }
    if (r.chance(0.25f)) {  // vasos de planta
        for (int k = 0; k < 3; ++k) {
            glm::vec3 c(bx + r.range(-bw / 2 + 0.4f, bw / 2 - 0.4f), top, bz + bd / 2 - 0.35f);
            p.color = srgb(150, 80, 50);
            p.box(c - glm::vec3(0.15f, 0, 0.15f), {0.3f, 0.3f, 0.3f});
            p.color = srgb(60, 130, 50);
            p.boxC(c + glm::vec3(0, 0.5f, 0), {0.45f, 0.4f, 0.45f});
        }
    }
    // de longe: bloco com textura de janelas na mesma cor, laje e caixa d'água
    {
        FarScope fs(g);
        MeshBuilder& w = g[brickBase || rawUpper ? Mat::BrickWindow : Mat::FacadeHouse];
        w.color = brickBase || rawUpper ? glm::vec4(1.f) : paint;
        w.uvTile = brickBase || rawUpper ? glm::vec2(2.5f, 2.7f) : glm::vec2(3.f, 2.85f);
        w.box({bx - bw / 2, 0.f, bz - bd / 2}, {bw, top, bd}, 4);
        MeshBuilder& c = g[Mat::Concrete];
        c.color = glm::vec4(1.f); c.uvTile = {2.f, 2.f};
        c.box({bx - bw / 2 - 0.05f, top - 0.15f, bz - bd / 2 - 0.05f}, {bw + 0.1f, 0.2f, bd + 0.1f}, 4);
        MeshBuilder& t = g[Mat::Painted];
        t.color = srgb(40, 110, 200); t.uvTile = {0.f, 0.f};
        t.box({tx - 0.55f, top, tz - 0.55f}, {1.1f, 1.f, 1.1f}, 4);
    }
}

void genFavela(Engine* e, Rng& r, glm::vec3& spawn, float& spawnYaw) {
    const float half = 130.f;
    const uint32_t seed = e->mapSeed;
    HeightFn H = [seed](float x, float z) { return favelaHeight(x, z, seed); };
    const FavelaPad* pads = favelaPads(seed);
    // chão (malha de 1 m + heightfield). Dica do shader: chão batido no platô e nas ruas, mata nas encostas
    {
        e->chunks.emplace_back();
        TerrainChunk& c = e->chunks.back();
        c.used = true;
        std::vector<Vertex> v; std::vector<uint32_t> idx;
        const int res = 220;
        std::function<glm::vec3(float, float)> hint = [](float x, float z) {
            float s = favelaEdge(x, z);
            float town = 1.f - smooth01(1.4f, 1.5f, s);
            float road = (1.f - smooth01(5.f, 7.f, std::abs(x))) * smooth01(50.f, 56.f, z);
            float forest = smooth01(1.62f, 1.72f, s) * (1.f - road);
            return glm::vec3(std::max(town, road), forest, 0.f);
        };
        terrainMesh(v, idx, -half, -half, 2 * half, res, H, &c.heights, &hint);  // (só as alturas da colisão, em 1,2 m)
        addHeightfield(e, c, -half, -half, 2 * half, res);
        // Desenho: 5x5 pedaços, cada um em 3 versões (completa perto, metade e um quarto dos triângulos longe).
        // Antes era uma malha só de 97 mil triângulos desenhada inteira (até o que fica atrás da câmera).
        // A borda de cada pedaço desce 60 cm (saia): onde um pedaço detalhado encosta num simples não abre fresta.
        const int N = 5, sub = res / N;              // 44 células por pedaço
        const float csz = 2 * half / N;
        static const float kLodDist[3] = {85.f, 200.f, 1e30f};  // (o motor multiplica pelo nível: Leve 34 m / 80 m)
        for (int cj = 0; cj < N; ++cj)
            for (int ci = 0; ci < N; ++ci)
                for (int l = 0; l < 3; ++l) {
                    const int r2 = sub >> l;
                    terrainMesh(v, idx, -half + ci * csz, -half + cj * csz, csz, r2, H, nullptr, &hint);
                    // as versões simples ficam um pouco abaixo: com menos pontos, o chão "esticado" subiria por cima
                    // do cimentado e dos pisos (manchas verdes de longe)
                    if (l > 0) for (Vertex& x : v) x.pos.y -= l == 1 ? 0.18f : 0.4f;
                    // dentro da favela o chão é concreto (sem grama): vai no alfa da cor (o shader do terreno lê)
                    for (Vertex& x : v) x.color.a = 1.f - (1.f - smooth01(1.36f, 1.46f, favelaEdge(x.pos.x, x.pos.z)));
                    const uint32_t W = uint32_t(r2 + 1);
                    auto saia = [&](uint32_t a, uint32_t b) {  // vira para os dois lados (não importa de onde se olha)
                        const uint32_t base = uint32_t(v.size());
                        for (uint32_t k : {a, b}) { Vertex x = v[k]; x.pos.y -= 0.6f; v.push_back(x); }
                        for (uint32_t k : {a, b, base, b, base + 1, base, a, base, b, b, base, base + 1}) idx.push_back(k);
                    };
                    for (uint32_t k = 0; k + 1 < W; ++k) {
                        saia(k, k + 1);                                  // borda de baixo
                        saia((W - 1) * W + k, (W - 1) * W + k + 1);      // de cima
                        saia(k * W, (k + 1) * W);                        // esquerda
                        saia(k * W + W - 1, (k + 1) * W + W - 1);        // direita
                    }
                    const MeshId m = e->renderer.createMesh(v.data(), uint32_t(v.size()), idx.data(), uint32_t(idx.size()), false);
                    const Scene::Handle h = addMeshObject(e, "Morro", m, e->mats[int(Mat::Terrain)], glm::vec3(0.f), glm::vec3(1.f), glm::vec3(0.f), false);
                    if (GameObject* go = e->scene.get(h); go && go->meshRenderer) {
                        go->meshRenderer->castShadows = false;  // só recebe sombra
                        go->meshRenderer->lodNear = l == 0 ? 0.f : kLodDist[l - 1];
                        go->meshRenderer->lodFar = kLodDist[l];
                    }
                    c.objects.push_back(h);
                }
        c.cx = 1 << 20;  // fora da grade do mundo infinito
    }
    Cells cells(30.f, 2);
    cells.detailFar = 85.f;  // casas completas até 85 m; depois, blocos simples
    std::vector<BoxCollider> treeBoxes;
    // Sem árvores, arbustos e capim (mapa leve para celular fraco). Onde havia plantas, os sorteios continuam
    // sendo feitos para o resto do mapa (vans, carros, entulho) ficar no mesmo lugar de antes.

    // ---- rotas principais (escadarias de concreto pelos becos) ----
    const auto& routes = favelaRoutes();
    const auto& entries = favelaEntries(seed);
    auto nearRoute = [&](glm::vec2 p, float w) {
        for (const auto& rt : routes)
            for (size_t i = 0; i + 1 < rt.size(); ++i) { float t; if (segDist2(p, rt[i], rt[i + 1], t) < w) return true; }
        return false;
    };
    {
        // Degraus das escadarias, cada um na célula do mapa onde está (antes era um objeto só com todos os degraus,
        // sempre desenhado inteiro). Perto: o degrau (sem a face de baixo, enterrada); longe: um degrau grosso a cada
        // 1,8 m (4 vezes menos triângulos). O que fica atrás da câmera nem é desenhado.
        std::set<Geo*> stepStarted;
        auto stepGeo = [&](glm::vec3 p) -> Geo& {
            Geo& gs = cells.at(p);
            if (stepStarted.insert(&gs).second) gs.place(glm::vec3(0.f), 0.f);
            return gs;
        };
        auto stepMesh = [](Geo& gs) -> MeshBuilder& {
            MeshBuilder& m = gs[Mat::Concrete];
            m.color = glm::vec4(1.f); m.uvTile = {1.f, 1.f};
            return m;
        };
        std::set<Geo*> railStarted;
        for (size_t ri = 0; ri < routes.size(); ++ri)
            for (size_t i = 0; i + 1 < routes[ri].size(); ++i) {
                const auto& rt = routes[ri];
                glm::vec2 a = rt[i], b = rt[i + 1], dir = glm::normalize(b - a);
                float L = glm::length(b - a), yaw = std::atan2(dir.x, dir.y);
                // degrau seguindo o morro (sem o fundo): onde é íngreme o degrau fica mais curto, para o espelho não
                // passar de ~20 cm (com 45 cm fixos, o morro íngreme dava degraus de 30 cm)
                for (float s = 0.f; s < L;) {
                    const glm::vec2 q0 = a + dir * s, q1 = a + dir * std::min(L, s + 0.45f);
                    const float incl = std::abs(H(q1.x, q1.y) - H(q0.x, q0.y)) / std::max(0.05f, glm::length(q1 - q0));
                    const float passo = std::clamp(0.19f / std::max(incl, 1e-3f), 0.26f, 0.45f);
                    glm::vec2 q = a + dir * (s + passo * 0.5f);
                    const glm::vec3 p(q.x, H(q.x, q.y) - 0.35f, q.y);
                    Geo& gs = stepGeo(p);
                    DetailScope ds(gs);
                    yawBox(stepMesh(gs), p, {2.8f, 0.42f, passo + 0.02f}, yaw, 4);
                    s += passo;
                }
                for (float s = 0.f; s < L; s += 1.8f) {  // de longe
                    const float len = std::min(1.8f, L - s);
                    glm::vec2 q = a + dir * (s + len * 0.5f);
                    const glm::vec3 p(q.x, H(q.x, q.y) - 0.35f, q.y);
                    Geo& gs = stepGeo(p);
                    FarScope fs(gs);
                    yawBox(stepMesh(gs), p, {2.8f, 0.42f, len + 0.02f}, yaw, 4);
                }
                // corrimão de ferro onde a escadaria é íngreme
                glm::vec2 side(-dir.y, dir.x);
                glm::vec3 prevTop(0.f);
                bool havePrev = false;
                for (float s = 0.5f; s < L - 0.5f; s += 2.f) {
                    glm::vec2 q = a + dir * s, q2 = a + dir * std::min(L, s + 2.f);
                    float steep = std::abs(H(q2.x, q2.y) - H(q.x, q.y)) / 2.f;
                    glm::vec2 pp = q + side * 1.3f;
                    glm::vec3 base(pp.x, H(pp.x, pp.y), pp.y), topP = base + glm::vec3(0, 0.95f, 0);
                    if (steep < 0.25f) { havePrev = false; continue; }
                    Geo& gr = cells.at(base);
                    if (railStarted.insert(&gr).second) gr.place(glm::vec3(0.f), 0.f);
                    DetailScope ds(gr);
                    MeshBuilder& rm = gr[Mat::GunMetal];
                    rm.color = glm::vec4(1.f); rm.uvTile = {0.f, 0.f};
                    rm.box(base - glm::vec3(0.025f, 0.f, 0.025f), {0.05f, 0.95f, 0.05f});
                    if (havePrev) segBox(rm, prevTop, topP, 0.045f);
                    prevTop = topP; havePrev = true;
                }
            }
    }
    // ---- postes com fiação ----
    {
        std::vector<glm::vec3> poles;
        std::vector<glm::vec2> poleSide;  // de que lado da escadaria o poste está
        for (size_t ri = 0; ri < routes.size(); ++ri)
            for (size_t i = 0; i + 1 < routes[ri].size(); ++i) {
                const auto& rt = routes[ri];
                glm::vec2 a = rt[i], b = rt[i + 1], dir = glm::normalize(b - a), side(-dir.y, dir.x);
                float L = glm::length(b - a);
                for (float s = 4.f; s < L; s += 16.f) {
                    glm::vec2 q = a + dir * s + side * 1.7f;
                    poles.push_back({q.x, H(q.x, q.y), q.y});
                    poleSide.push_back(side);
                }
            }
        for (size_t k = 0; k < poles.size(); ++k) {
            glm::vec3 p = poles[k];
            Geo& g = cells.at(p);
            g.place(p, 0.f);
            MeshBuilder& c = g[Mat::Concrete];
            c.color = glm::vec4(1.f); c.uvTile = {1.f, 1.f};
            c.cylinder({0, 0, 0}, {0, 8.f, 0}, 0.13f, 0.09f, 8);
            g.collider({-0.15f, 0, -0.15f}, {0.3f, 8.f, 0.3f});
            DetailScope ds(g);
            MeshBuilder& m = g[Mat::GunMetal];
            m.color = glm::vec4(1.f); m.uvTile = {0.f, 0.f};
            m.box({-0.7f, 7.3f, -0.05f}, {1.4f, 0.1f, 0.1f});
            m.box({-0.25f, 6.2f, -0.2f}, {0.5f, 0.6f, 0.4f});  // transformador / caixa de luz
            if (k % 3 != 2) {  // braço com luminária virada para a escadaria
                glm::vec2 to = glm::normalize(glm::vec2(-poleSide[k].x, -poleSide[k].y));
                glm::vec3 arm(to.x * 1.3f, 6.7f, to.y * 1.3f);
                segBox(m, {0.f, 6.3f, 0.f}, arm, 0.06f);
                m.box(arm + glm::vec3(-0.18f, 0.f, -0.18f), {0.36f, 0.1f, 0.36f});
                MeshBuilder& l = g[Mat::Lamp];
                l.color = glm::vec4(1.f); l.uvTile = {0.f, 0.f};
                l.box(arm + glm::vec3(-0.13f, -0.07f, -0.13f), {0.26f, 0.07f, 0.26f});
                g.lamp(arm - glm::vec3(0, 0.25f, 0));
            }
            g.place(glm::vec3(0.f), 0.f);
            if (k + 1 < poles.size() && glm::length(poles[k + 1] - p) < 22.f)
                for (float o : {-0.6f, 0.f, 0.6f}) wire(g[Mat::Polymer], p + glm::vec3(o, 7.35f, 0), poles[k + 1] + glm::vec3(o, 7.35f, 0), 0.6f);
        }
    }

    // ---- casas: lotes em grade, becos entre eles ----
    std::vector<std::pair<glm::vec2, glm::vec2>> lots;  // centro, meio tamanho (para os testes)
    int houseTotal = 0, houseKept = 0;
    const float kHouseThinFrom = 1.12f;   // a partir daqui (fração do raio da favela) as casas ficam mais raras
    const unsigned kHouseThinKeep = 35;   // % das casas que ficam nessa faixa
    const float pitchX = 7.6f, pitchZ = 7.2f;
    for (float gz = -94.f; gz < 92.f; gz += pitchZ)
        for (float gx = -98.f; gx < 98.f; gx += pitchX) {
            glm::vec2 c(gx + pitchX * 0.5f + r.range(-0.3f, 0.3f), gz + pitchZ * 0.5f + r.range(-0.3f, 0.3f));
            float gap = r.range(1.2f, 1.6f);
            float lw = pitchX - gap, ld = pitchZ - gap;
            if (r.chance(0.15f)) lw -= r.range(0.5f, 1.5f);
            // o lote encolhe do lado de uma escadaria/patamar em vez de sumir
            auto fits = [&](glm::vec2 cc, glm::vec2 hh) {
                for (int i = -1; i <= 1; ++i)
                    for (int j = -1; j <= 1; ++j) {
                        glm::vec2 q = cc + glm::vec2(i * hh.x, j * hh.y);
                        if (favelaEdge(q.x, q.y) > 1.4f) return false;  // até a rua de baixo
                        for (int k = 0; k < FP_Count; ++k) if (padDist(pads[k], q.x, q.y) < 1.8f) return false;
                        if (nearRoute(q, 1.65f)) return false;
                        if (std::abs(q.x) < 7.f && q.y > 40.f) return false;  // rua da entrada
                    }
                return true;
            };
            glm::vec2 hs(lw * 0.5f, ld * 0.5f);
            bool ok = fits(c, hs);
            for (int tries = 0; !ok && tries < 8; ++tries) {
                glm::vec2 sh = tries < 4 ? glm::vec2(lw * 0.34f, ld * 0.34f) : glm::vec2(lw * 0.5f, ld * 0.5f);
                int side = tries % 4;
                glm::vec2 h2 = side < 2 ? glm::vec2(hs.x - sh.x * 0.5f, hs.y) : glm::vec2(hs.x, hs.y - sh.y * 0.5f);
                glm::vec2 c2 = c + (side == 0 ? glm::vec2(sh.x * 0.5f, 0) : side == 1 ? glm::vec2(-sh.x * 0.5f, 0)
                                  : side == 2 ? glm::vec2(0, sh.y * 0.5f) : glm::vec2(0, -sh.y * 0.5f));
                if (h2.x > 1.4f && h2.y > 1.4f && fits(c2, h2)) { c = c2; hs = h2; lw = hs.x * 2; ld = hs.y * 2; ok = true; }
            }
            if (!ok || r.chance(0.04f)) continue;
            // casa: base no canto mais alto do terreno; a fundação desce até o mais baixo
            float hs4[4] = {H(c.x - hs.x, c.y - hs.y), H(c.x + hs.x, c.y - hs.y), H(c.x - hs.x, c.y + hs.y), H(c.x + hs.x, c.y + hs.y)};
            float mn = *std::min_element(hs4, hs4 + 4), mx = *std::max_element(hs4, hs4 + 4);
            int floors = r.chance(0.2f) ? 1 : r.chance(0.55f) ? 2 : r.chance(0.7f) ? 3 : 4;
            float yaw = 0.f;
            float pick = r.f();
            if (pick > 0.75f) yaw = 1.5707963f; else if (pick > 0.55f) yaw = -1.5707963f;
            float w = yaw == 0.f ? lw : ld, d = yaw == 0.f ? ld : lw;
            bool frontOpen = true;
            glm::vec3 pos(c.x, mx + 0.05f, c.y);
            // Mapa mais leve: na beirada do morro (longe do miolo onde a partida acontece) só fica parte das casas.
            // A casa que sai é montada numa peça descartada, para os sorteios (e o resto do mapa) não mudarem; o lote
            // fica de chão batido (sem cimento). Igual para todo mundo (online: o mapa é o mesmo em todo aparelho).
            const float edge = favelaEdge(c.x, c.y);
            uint32_t hh = uint32_t(int(c.x * 10.f)) * 73856093u ^ uint32_t(int(c.y * 10.f)) * 19349663u;
            hh ^= hh >> 13; hh *= 0x5bd1e995u; hh ^= hh >> 15;
            const bool keep = edge < kHouseThinFrom || (hh % 100u) < kHouseThinKeep;
            ++houseTotal;
            if (!keep) {
                Geo descarte;
                descarte.place(pos, yaw);
                favelaHouse(descarte, r, w, d, floors, mx - mn + 0.05f, frontOpen, nullptr);
                lots.push_back({c, hs});
                continue;
            }
            ++houseKept;
            Geo& g = cells.at(pos);
            g.place(pos, yaw);
            const auto chao = [&](float lx, float lz) {  // ponto da casa -> altura do terreno ali, em relação ao piso
                const float cy = std::cos(yaw), sy = std::sin(yaw);
                const float wx = pos.x + lx * cy + lz * sy, wz = pos.z - lx * sy + lz * cy;
                return H(wx, wz) - pos.y;
            };
            if (std::getenv("MAPA_ESCADAS")) std::printf("casa em %.1f %.1f %.1f yaw %.2f\n", pos.x, pos.y, pos.z, yaw);
            favelaHouse(g, r, w, d, floors, mx - mn + 0.05f, frontOpen, chao);
            lots.push_back({c, hs});
        }
    if (std::getenv("MAPA_DEBUG")) std::printf("  casas: %d de %d\n", houseKept, houseTotal);

    // (chão dos becos e quintais: o próprio terreno, que dentro da favela é concreto; as encostas são rampas lisas,
    // sem os degrauzinhos de cada metro quadrado que se misturavam com as escadarias)

    // ---- muros de arrimo dos patamares (grafitados) ----
    for (int k = 0; k < FP_Count; ++k) {
        const FavelaPad& P = pads[k];
        Geo& g = cells.at({P.c.x, 0, P.c.y});
        g.place(glm::vec3(0.f), 0.f);
        for (int side = 0; side < 4; ++side) {
            glm::vec2 n = side == 0 ? glm::vec2(0, 1) : side == 1 ? glm::vec2(0, -1) : side == 2 ? glm::vec2(1, 0) : glm::vec2(-1, 0);
            glm::vec2 t(-n.y, n.x);
            float len = side < 2 ? P.half.x * 2.f : P.half.y * 2.f;
            glm::vec2 edge = P.c + n * (side < 2 ? P.half.y : P.half.x);
            for (float s = -len / 2; s < len / 2 - 0.01f; s += 2.f) {
                float segL = std::min(2.f, len / 2 - s);
                glm::vec2 q = edge + t * (s + segL * 0.5f) + n * 0.2f;
                glm::vec2 out = q + n * 2.2f;
                float ho = favelaRaw(out.x, out.y, seed);
                float diff = ho - P.y;
                if (std::abs(diff) < 0.5f) continue;
                if (k == FP_Entrance && side == 0) continue;  // a rua chega por aqui
                bool entry = false;
                for (const PadEntry& en : entries) { float t; if (en.pad == k && segDist2(q, en.a, en.b, t) < 3.2f) entry = true; }
                if (entry) continue;  // passagem da escadaria
                float y0 = std::min(ho, P.y) - 0.6f, y1 = diff < 0.f ? P.y + 0.9f : ho + 0.1f;  // aterro: guarda-corpo de 90 cm
                Mat wm = r.chance(0.55f) ? Mat::Graffiti : r.chance(0.5f) ? Mat::Concrete : Mat::Stone;
                MeshBuilder& m = g[wm];
                m.color = glm::vec4(1.f);
                m.uvTile = wm == Mat::Graffiti ? glm::vec2(4.f, 2.f) : glm::vec2(2.f, 2.f);
                glm::vec3 size(side < 2 ? segL : 0.4f, y1 - y0, side < 2 ? 0.4f : segL);
                glm::vec3 mnp(q.x - size.x / 2, y0, q.y - size.z / 2);
                m.box(mnp, size);
                g.collider(mnp, size);
                // grafite no lado que aparece (aterro: por fora; corte: virado para o patamar)
                float vis = diff < 0.f ? P.y + 0.9f - ho : ho - P.y;
                auto wallAt = [&](float off) {  // vizinho também tem muro do mesmo tipo?
                    glm::vec2 o2 = q + t * off + n * 2.2f;
                    float d2 = favelaRaw(o2.x, o2.y, seed) - P.y;
                    return std::abs(d2) >= 0.5f && (d2 < 0.f) == (diff < 0.f) && std::abs(s + segL * 0.5f + off) < len / 2;
                };
                if (vis > 1.2f && segL > 1.9f && wallAt(-2.f) && wallAt(2.f) && r.chance(0.4f)) {
                    float mw = std::min(3.6f, (vis - 0.2f) * 2.f);
                    glm::vec3 nn = diff < 0.f ? glm::vec3(n.x, 0, n.y) : glm::vec3(-n.x, 0, -n.y);
                    glm::vec3 face = glm::vec3(q.x, 0, q.y) + nn * 0.2f;
                    float cy = diff < 0.f ? (ho + P.y + 0.9f) * 0.5f : (P.y + ho) * 0.5f;
                    muralQuad(g, r.irange(0, kMuralCount - 1), {face.x, cy, face.z}, nn, std::min(mw, 2.f * 1.95f));
                }
            }
        }
    }

    // ---- campinho de futebol: gramado com linhas, traves com rede e alambrado em volta ----
    {
        const FavelaPad& P = pads[FP_Field];
        glm::vec3 c(P.c.x, P.y, P.c.y);
        Geo& g = cells.at(c);
        g.place(c, 0.f);
        MeshBuilder& b = g[Mat::Concrete];
        b.color = glm::vec4(1.f); b.uvTile = {2.f, 2.f};
        b.box({-P.half.x, -0.4f, -P.half.y}, {P.half.x * 2, 0.45f, P.half.y * 2});
        g.collider({-P.half.x, -0.4f, -P.half.y}, {P.half.x * 2, 0.45f, P.half.y * 2});
        MeshBuilder& q = g[Mat::Court];
        glm::vec2 uv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        q.quad({-14, 0.07f, 7.5f}, {14, 0.07f, 7.5f}, {14, 0.07f, -7.5f}, {-14, 0.07f, -7.5f}, uv);
        MeshBuilder& wpost = g[Mat::Painted];
        wpost.color = srgb(240, 240, 240); wpost.uvTile = {0.f, 0.f};
        MeshBuilder& net = g[Mat::ChainLink];
        net.color = glm::vec4(1.f); net.uvTile = {1.f, 1.f};
        for (float sx : {-1.f, 1.f}) {
            float x = sx * 13.f;
            for (float z : {-1.5f, 1.5f}) wpost.box({x - 0.06f, 0.05f, z - 0.06f}, {0.12f, 2.f, 0.12f});
            wpost.box({x - 0.06f, 2.f, -1.56f}, {0.12f, 0.12f, 3.12f});
            float back = x + sx * 1.f;
            net.box({std::min(x, back), 0.05f, -1.5f}, {1.f, 2.05f, 3.f}, sx > 0 ? 1 : 2);  // rede (sem a face da boca)
            g.collider({sx > 0 ? x + 0.9f : back, 0.05f, -1.5f}, {0.12f, 2.f, 3.f});
        }
        // alambrado de 4 m com portões no lado sul (onde chegam as escadarias)
        MeshBuilder& post = g[Mat::GunMetal];
        post.color = glm::vec4(1.f); post.uvTile = {0.f, 0.f};
        const float fh = 4.f, hx = P.half.x - 0.5f, hz = P.half.y - 0.5f;
        auto fence = [&](glm::vec2 a, glm::vec2 b2) {
            glm::vec2 d = b2 - a;
            float L = glm::length(d);
            for (float s = 0.f; s < L - 0.01f; s += 3.f) {
                float l = std::min(3.f, L - s);
                glm::vec2 p0 = a + d / L * s, p1 = a + d / L * (s + l);
                glm::vec2 mn = glm::min(p0, p1), mx = glm::max(p0, p1);
                // gates: aberturas onde chegam as rotas
                glm::vec2 mid = (p0 + p1) * 0.5f;
                if (std::abs(mid.y - hz) < 0.1f && (std::abs(mid.x + 8.f) < 1.6f || std::abs(mid.x - 10.f) < 1.6f)) continue;
                if (std::abs(mid.y + hz) < 0.1f && std::abs(mid.x) < 1.6f) continue;   // portão norte
                if (std::abs(mid.x - hx) < 0.1f && std::abs(mid.y - 1.f) < 1.6f) continue;  // portão leste
                net.box({mn.x - 0.01f, 0.f, mn.y - 0.01f}, {mx.x - mn.x + 0.02f, fh, mx.y - mn.y + 0.02f});
                g.collider({mn.x - 0.03f, 0.f, mn.y - 0.03f}, {mx.x - mn.x + 0.06f, fh, mx.y - mn.y + 0.06f});
                post.box({p0.x - 0.04f, 0.f, p0.y - 0.04f}, {0.08f, fh + 0.1f, 0.08f});
                segBox(post, {p0.x, fh, p0.y}, {p1.x, fh, p1.y}, 0.05f);
            }
        };
        fence({-hx, -hz}, {hx, -hz});
        fence({hx, -hz}, {hx, hz});
        fence({hx, hz}, {-hx, hz});
        fence({-hx, hz}, {-hx, -hz});
        // arquibancada de concreto no lado leste (fora do alambrado não cabe: fica dentro, atrás da linha)
        MeshBuilder& a = g[Mat::Concrete];
        for (int k = 0; k < 2; ++k) {
            a.box({14.6f + k * 0.0f, 0.f, -6.f}, {0.6f, 0.45f * (k + 1), 12.f});
            g.collider({14.6f, 0.f, -6.f}, {0.6f, 0.45f * (k + 1), 12.f});
        }
    }

    // ---- área alta (mirante): laje com guarda-corpo, banco e torre de antena ----
    {
        const FavelaPad& P = pads[FP_High];
        glm::vec3 c(P.c.x, P.y, P.c.y);
        Geo& g = cells.at(c);
        g.place(c, 0.f);
        MeshBuilder& b = g[Mat::Concrete];
        b.color = glm::vec4(1.f); b.uvTile = {2.f, 2.f};
        b.box({-P.half.x, -0.4f, -P.half.y}, {P.half.x * 2, 0.45f, P.half.y * 2});
        g.collider({-P.half.x, -0.4f, -P.half.y}, {P.half.x * 2, 0.45f, P.half.y * 2});
        MeshBuilder& m = g[Mat::GunMetal];
        m.color = glm::vec4(1.f); m.uvTile = {0.f, 0.f};
        // torre treliçada de 18 m
        glm::vec3 t0(-P.half.x + 2.f, 0.05f, -P.half.y + 2.f);
        for (int k = 0; k < 4; ++k) {
            glm::vec3 a = t0 + glm::vec3(k & 1 ? 1.2f : -1.2f, 0, k & 2 ? 1.2f : -1.2f);
            segBox(m, a, t0 + glm::vec3(k & 1 ? 0.2f : -0.2f, 18.f, k & 2 ? 0.2f : -0.2f), 0.08f);
        }
        for (float h = 1.5f; h < 18.f; h += 1.5f) {
            float s = 1.2f - h / 18.f * 1.0f;
            for (int k = 0; k < 4; ++k) {
                glm::vec3 a = t0 + glm::vec3((k == 0 || k == 3) ? -s : s, h, k < 2 ? -s : s);
                glm::vec3 b2 = t0 + glm::vec3((k == 0 || k == 1) ? s : -s, h, (k == 1 || k == 2) ? s : -s);
                segBox(m, a, b2, 0.04f);
            }
        }
        g.collider(t0 - glm::vec3(1.3f, 0, 1.3f), {2.6f, 18.f, 2.6f});
        MeshBuilder& w = g[Mat::Wood];
        w.color = glm::vec4(1.f); w.uvTile = {0.f, 0.f};
        for (int k = 0; k < 2; ++k) {
            float z = 1.f + k * 3.f;
            w.box({2.f, 0.45f, z}, {2.f, 0.06f, 0.5f});
            m.box({2.1f, 0.f, z + 0.2f}, {0.06f, 0.45f, 0.06f});
            m.box({3.8f, 0.f, z + 0.2f}, {0.06f, 0.45f, 0.06f});
        }
    }

    // ---- praça das crianças: chão de terra, balanço, escorregador, gangorra, gira-gira, bancos e árvores ----
    {
        const FavelaPad& P = pads[FP_Square];
        glm::vec3 c(P.c.x, P.y, P.c.y);
        Geo& g = cells.at(c);
        g.place(c, 0.f);
        MeshBuilder& gr = g[Mat::Dirt];
        gr.color = glm::vec4(1.7f, 1.55f, 1.35f, 1.f); gr.uvTile = {3.f, 3.f};
        gr.box({-P.half.x, -0.4f, -P.half.y}, {P.half.x * 2, 0.45f, P.half.y * 2});
        g.collider({-P.half.x, -0.4f, -P.half.y}, {P.half.x * 2, 0.45f, P.half.y * 2});
        MeshBuilder& p = g[Mat::Painted];
        p.uvTile = {0.f, 0.f};
        MeshBuilder& m = g[Mat::GunMetal];
        m.color = glm::vec4(1.f); m.uvTile = {0.f, 0.f};
        // balanço (2 lugares)
        {
            glm::vec3 o(-6.f, 0.05f, -3.f);
            p.color = srgb(230, 60, 50);
            for (float x : {-1.6f, 1.6f}) {
                segBox(p, o + glm::vec3(x, 0, -0.9f), o + glm::vec3(x, 2.4f, 0), 0.09f);
                segBox(p, o + glm::vec3(x, 0, 0.9f), o + glm::vec3(x, 2.4f, 0), 0.09f);
            }
            segBox(p, o + glm::vec3(-1.7f, 2.4f, 0), o + glm::vec3(1.7f, 2.4f, 0), 0.1f);
            for (float x : {-0.75f, 0.75f}) {
                segBox(m, o + glm::vec3(x - 0.2f, 2.4f, 0), o + glm::vec3(x - 0.2f, 0.55f, 0), 0.02f);
                segBox(m, o + glm::vec3(x + 0.2f, 2.4f, 0), o + glm::vec3(x + 0.2f, 0.55f, 0), 0.02f);
                p.color = srgb(250, 200, 40);
                p.box(o + glm::vec3(x - 0.25f, 0.5f, -0.12f), {0.5f, 0.05f, 0.24f});
                p.color = srgb(230, 60, 50);
            }
            g.collider(o + glm::vec3(-1.8f, 0, -0.1f), {3.6f, 2.5f, 0.2f});
        }
        // escorregador: escada atrás, plataforma, rampa vermelha
        {
            glm::vec3 o(3.f, 0.05f, -3.f);
            p.color = srgb(40, 120, 220);
            for (float x : {-0.35f, 0.35f}) segBox(p, o + glm::vec3(x, 0, -1.2f), o + glm::vec3(x, 2.f, -0.2f), 0.07f);
            for (float h = 0.35f; h < 2.f; h += 0.35f) p.box(o + glm::vec3(-0.35f, h, -1.2f + h * 0.5f - 0.03f), {0.7f, 0.05f, 0.08f});
            p.color = srgb(250, 200, 40);
            p.box(o + glm::vec3(-0.4f, 1.95f, -0.3f), {0.8f, 0.08f, 0.6f});
            for (float x : {-0.38f, 0.32f}) for (float z : {-0.3f, 0.24f}) p.box(o + glm::vec3(x, 0, z), {0.06f, 1.95f, 0.06f});
            p.color = srgb(225, 50, 45);
            segBox(p, o + glm::vec3(0, 1.98f, 0.3f), o + glm::vec3(0, 0.25f, 2.9f), 0.62f, 0.06f);
            g.collider(o + glm::vec3(-0.45f, 0, -1.25f), {0.9f, 2.f, 1.6f});
        }
        // gangorra
        {
            glm::vec3 o(-6.f, 0.05f, 4.f);
            p.color = srgb(60, 170, 80);
            p.box(o + glm::vec3(-0.15f, 0, -0.15f), {0.3f, 0.45f, 0.3f});
            segBox(p, o + glm::vec3(-1.8f, 0.2f, 0), o + glm::vec3(1.8f, 0.7f, 0), 0.3f, 0.06f);
            g.collider(o + glm::vec3(-1.8f, 0, -0.15f), {3.6f, 0.6f, 0.3f});
        }
        // gira-gira
        {
            glm::vec3 o(3.f, 0.05f, 4.f);
            p.color = srgb(250, 140, 40);
            p.cylinder(o + glm::vec3(0, 0.25f, 0), o + glm::vec3(0, 0.33f, 0), 1.2f, 1.2f, 16);
            m.cylinder(o, o + glm::vec3(0, 0.25f, 0), 0.08f, 0.08f, 8);
            for (int k = 0; k < 4; ++k) {
                float a = k * 1.5707963f;
                segBox(m, o + glm::vec3(std::cos(a) * 1.05f, 0.33f, std::sin(a) * 1.05f), o + glm::vec3(0, 1.f, 0), 0.04f);
            }
            g.collider(o - glm::vec3(1.2f, 0, 1.2f), {2.4f, 0.4f, 2.4f});
        }
        // caixa de areia
        {
            MeshBuilder& w = g[Mat::Wood];
            w.color = glm::vec4(1.f); w.uvTile = {1.f, 1.f};
            glm::vec3 o(8.5f, 0.05f, 4.f);
            w.box(o + glm::vec3(-1.5f, 0, -1.5f), {3.f, 0.3f, 0.12f});
            w.box(o + glm::vec3(-1.5f, 0, 1.38f), {3.f, 0.3f, 0.12f});
            w.box(o + glm::vec3(-1.5f, 0, -1.5f), {0.12f, 0.3f, 3.f});
            w.box(o + glm::vec3(1.38f, 0, -1.5f), {0.12f, 0.3f, 3.f});
            p.color = srgb(220, 200, 150);
            p.box(o + glm::vec3(-1.4f, 0, -1.4f), {2.8f, 0.22f, 2.8f});
        }
        // bancos
        MeshBuilder& w = g[Mat::Wood];
        w.color = glm::vec4(1.f); w.uvTile = {0.f, 0.f};
        for (int k = 0; k < 4; ++k) {
            glm::vec3 o(-9.f + k * 6.f, 0.05f, P.half.y - 1.2f);
            for (int i = 0; i < 3; ++i) w.box(o + glm::vec3(-0.9f, 0.42f, -0.25f + i * 0.17f), {1.8f, 0.04f, 0.14f});
            m.box(o + glm::vec3(-0.8f, 0, -0.2f), {0.05f, 0.42f, 0.4f});
            m.box(o + glm::vec3(0.75f, 0, -0.2f), {0.05f, 0.42f, 0.4f});
            g.collider(o + glm::vec3(-0.9f, 0, -0.3f), {1.8f, 0.46f, 0.6f});
        }
        // (árvores da praça e floreiras saíram: os mesmos sorteios de antes, sem plantar nada)
        for (int k = 0; k < 5 + 9; ++k) { r.range(0, 6.28f); r.range(0.8f, 1.2f); }
        // luz da praça
        g.place(c + glm::vec3(0, 0.05f, 0), 0.f);
        buildStreetLight(g);
    }

    // ---- entrada principal: rua subindo do bairro, largo de concreto, vans estacionadas, boteco ----
    {
        Geo& g = cells.at({0, 0, 70});
        g.place(glm::vec3(0.f), 0.f);
        MeshBuilder& road = g[Mat::Asphalt];
        road.color = glm::vec4(1.f); road.uvTile = {0.f, 0.f};
        for (float z = 55.f; z < 92.f; z += 1.f) {
            float h0 = H(0.f, z) + 0.04f, h1 = H(0.f, z + 1.f) + 0.04f;
            glm::vec2 uv[4] = {{0, z / 4.f}, {0, (z + 1) / 4.f}, {1.5f, (z + 1) / 4.f}, {1.5f, z / 4.f}};
            road.quad({-3.5f, h0, z}, {-3.5f, h1, z + 1.f}, {3.5f, h1, z + 1.f}, {3.5f, h0, z}, uv);
        }
        // postes de rua subindo a entrada (luz de verdade à noite)
        for (int k = 0; k < 3; ++k) {
            float z = 62.f + k * 11.f, x = k % 2 ? 4.4f : -4.4f;
            glm::vec3 p(x, H(x, z), z);
            Geo& gl = cells.at(p);
            gl.place(p, x > 0 ? -1.5707963f : 1.5707963f);
            buildStreetLight(gl);
        }
        g.place(glm::vec3(0.f), 0.f);
        const FavelaPad& P = pads[FP_Entrance];
        glm::vec3 c(P.c.x, P.y, P.c.y);
        Geo& ge = cells.at(c);
        ge.place(c, 0.f);
        MeshBuilder& b = ge[Mat::Sidewalk];
        b.color = glm::vec4(1.f); b.uvTile = {2.f, 2.f};
        b.box({-P.half.x, -0.4f, -P.half.y}, {P.half.x * 2, 0.45f, P.half.y * 2});
        ge.collider({-P.half.x, -0.4f, -P.half.y}, {P.half.x * 2, 0.45f, P.half.y * 2});
        for (int k = 0; k < 2; ++k) {
            glm::vec3 cp = c + glm::vec3(-6.5f + k * 13.f, 0.05f, 2.5f - k * 1.f);
            Geo& gc = cells.at(cp);
            gc.place(cp, k ? 0.1f : 3.0f);
            buildCar(gc, r);
        }
        // boteco: cômodo com porta larga aberta e toldo de telha ondulada
        glm::vec3 bp = c + glm::vec3(-P.half.x + 3.f, 0.05f, -P.half.y + 2.6f);
        ge.place(bp, 0.f);
        ModuleSpec ms;
        ms.w = 5.f; ms.d = 4.f; ms.h = 2.8f;
        ms.walls[0] = MW_Open; ms.walls[1] = MW_Plain; ms.walls[2] = MW_Window; ms.walls[3] = MW_Plain;
        ms.wallMat = Mat::Plaster; ms.wallColor = srgb(240, 200, 60); ms.floorMat = Mat::Ceramic;
        buildModule(ge, ms);
        MeshBuilder& t = ge[Mat::Corrugated];
        t.color = glm::vec4(1.f); t.uvTile = {2.f, 2.f};
        t.quad({-2.6f, 2.9f, 2.f}, {-2.6f, 2.5f, 4.f}, {2.6f, 2.5f, 4.f}, {2.6f, 2.9f, 2.f});
        MeshBuilder& pw = ge[Mat::Wood];
        for (float x : {-2.5f, 2.4f}) pw.box({x, 0.f, 3.9f}, {0.1f, 2.5f, 0.1f});
        MeshBuilder& tb = ge[Mat::Painted];
        tb.color = srgb(230, 40, 40); tb.uvTile = {0.f, 0.f};
        for (int k = 0; k < 2; ++k) {  // mesas de plástico
            glm::vec3 o(-1.f + k * 2.f, 0.f, 3.f);
            tb.box(o + glm::vec3(-0.4f, 0.7f, -0.4f), {0.8f, 0.04f, 0.8f});
            tb.box(o + glm::vec3(-0.04f, 0.f, -0.04f), {0.08f, 0.7f, 0.08f});
        }
    }

    // ---- área baixa (acesso rápido): terreiro de terra com carros e entulho ----
    {
        const FavelaPad& P = pads[FP_Low];
        glm::vec3 c(P.c.x, P.y, P.c.y);
        Geo& g = cells.at(c);
        g.place(c, 0.f);
        MeshBuilder& b = g[Mat::Concrete];
        b.color = glm::vec4(0.9f, 0.9f, 0.88f, 1.f); b.uvTile = {2.f, 2.f};
        b.box({-P.half.x, -0.4f, -P.half.y}, {P.half.x * 2, 0.45f, P.half.y * 2});
        g.collider({-P.half.x, -0.4f, -P.half.y}, {P.half.x * 2, 0.45f, P.half.y * 2});
        MeshBuilder& w = g[Mat::Wood];
        w.color = glm::vec4(1.f); w.uvTile = {1.f, 1.f};
        for (int k = 0; k < 4; ++k) {  // caixotes (cobertura no tiroteio)
            glm::vec3 o(r.range(-6.f, 6.f), 0.05f, r.range(-8.f, 4.f));
            w.box(o, {1.f, 1.f, 1.f});
            g.collider(o, {1.f, 1.f, 1.f});
        }
        for (int k = 0; k < 2; ++k) {
            glm::vec3 cp = c + glm::vec3(-3.f + k * 6.f, 0.05f, 6.f);
            Geo& gc = cells.at(cp);
            gc.place(cp, 1.5707963f + r.range(-0.3f, 0.3f));
            buildCar(gc, r);
        }
    }

    // ---- rua em volta do pé do morro (todas as escadarias chegam nela), prédios do bairro do lado de fora ----
    {
        auto ringAt = [](float t, float k) {  // superelipse do morro aumentada k vezes
            float c = std::cos(t), sn = std::sin(t);
            return glm::vec2(64.f * k * (c < 0 ? -1.f : 1.f) * std::sqrt(std::abs(c)), 62.f * k * (sn < 0 ? -1.f : 1.f) * std::sqrt(std::abs(sn)));
        };
        Geo& g = cells.at({1e4f, 0, -1e4f});
        g.place(glm::vec3(0.f), 0.f);
        MeshBuilder& road = g[Mat::Road];
        road.color = glm::vec4(1.f); road.uvTile = {0.f, 0.f};
        const int seg = 240;
        float along = 0.f;
        for (int k = 0; k < seg; ++k) {
            float t0 = 6.2831853f * k / seg, t1 = 6.2831853f * (k + 1) / seg;
            glm::vec2 a0 = ringAt(t0, 1.5f), a1 = ringAt(t1, 1.5f), b0 = ringAt(t0, 1.62f), b1 = ringAt(t1, 1.62f);
            float L = glm::length((a1 + b1) * 0.5f - (a0 + b0) * 0.5f);
            glm::vec2 uv[4] = {{0, along / 6.f}, {0, (along + L) / 6.f}, {1, (along + L) / 6.f}, {1, along / 6.f}};
            glm::vec3 p0(a0.x, H(a0.x, a0.y) + 0.06f, a0.y), p1(a1.x, H(a1.x, a1.y) + 0.06f, a1.y);
            glm::vec3 p2(b1.x, H(b1.x, b1.y) + 0.06f, b1.y), p3(b0.x, H(b0.x, b0.y) + 0.06f, b0.y);
            glm::vec3 nrm = glm::cross(p1 - p0, p3 - p0);
            if (nrm.y < 0.f) road.quad(p0, p3, p2, p1, uv); else road.quad(p0, p1, p2, p3, uv);
            along += L;
            if (k % 12 == 6) {  // poste do lado de fora da rua
                glm::vec2 o = ringAt(t0, 1.66f), in = ringAt(t0, 1.5f);
                glm::vec2 dir = glm::normalize(in - o);
                glm::vec3 p(o.x, H(o.x, o.y), o.y);
                Geo& gl = cells.at(p);
                gl.place(p, std::atan2(dir.x, dir.y));
                buildStreetLight(gl);
            }
            if (k % 20 == 3 && r.chance(0.6f)) {  // carro parado na beira
                glm::vec2 cp2 = ringAt(t0 + 0.004f, 1.6f), nx = ringAt(t0 + 0.03f, 1.6f);
                glm::vec3 cp(cp2.x, H(cp2.x, cp2.y), cp2.y);
                Geo& gc = cells.at(cp);
                gc.place(cp, std::atan2(nx.x - cp2.x, nx.y - cp2.y) + 1.5707963f);
                buildCar(gc, r);
            }
        }
    }

    // ---- ao fundo: morros e o Cristo no alto (sem colisão, fora do limite; sem as casinhas, para ficar leve) ----
    {
        auto far = [seed](float x, float z) {
            float r2 = std::sqrt(x * x + z * z);
            float hills = 6.f + fbm(x * 0.005f, z * 0.005f, 4, seed + 77) * 55.f;
            float cristo = 45.f * std::exp(-glm::dot(glm::vec2(x + 120.f, z + 380.f), glm::vec2(x + 120.f, z + 380.f)) / (2.f * 60.f * 60.f));
            return glm::mix(-30.f, hills + cristo, smooth01(170.f, 260.f, r2));
        };
        e->chunks.emplace_back();
        TerrainChunk& c = e->chunks.back();
        c.used = true;
        std::vector<Vertex> v; std::vector<uint32_t> idx;
        std::function<glm::vec3(float, float)> hint = [](float, float) { return glm::vec3(0.f, 1.f, 0.f); };
        // 36x36 células de 39 m: os morros estão a centenas de metros e são suaves (antes 64x64, 8 mil triângulos)
        terrainMesh(v, idx, -700.f, -700.f, 1400.f, 36, far, nullptr, &hint);
        c.mesh = e->renderer.createMesh(v.data(), uint32_t(v.size()), idx.data(), uint32_t(idx.size()), false);
        c.ground = addMeshObject(e, "Morros ao fundo", c.mesh, e->mats[int(Mat::Terrain)], glm::vec3(0.f), glm::vec3(1.f), glm::vec3(0.f), false);
        if (GameObject* go = e->scene.get(c.ground)) if (go->meshRenderer) go->meshRenderer->castShadows = false;
        c.cx = (1 << 20) + 1;
        Geo& g = cells.at({1e4f, 0, 1e4f});
        g.place(glm::vec3(0.f), 0.f);
        MeshBuilder& p = g[Mat::Painted];
        p.uvTile = {0.f, 0.f};
        // Cristo Redentor (silhueta) no morro mais alto ao norte
        glm::vec3 cb(-120.f, far(-120.f, -380.f) - 2.f, -380.f);
        p.color = srgb(235, 232, 225);
        p.box(cb + glm::vec3(-5.f, 0, -5.f), {10.f, 10.f, 10.f});
        p.cylinder(cb + glm::vec3(0, 10.f, 0), cb + glm::vec3(0, 34.f, 0), 3.4f, 2.2f, 10);
        p.box(cb + glm::vec3(-14.f, 29.f, -1.5f), {28.f, 3.f, 3.f});
        p.cylinder(cb + glm::vec3(0, 34.f, 0), cb + glm::vec3(0, 38.f, 0), 1.6f, 1.2f, 10);
    }

    cells.flush(e);
    addStaticBoxes(e, treeBoxes);
    setBounds(e, {-half + 12, -half + 12}, {half - 12, half - 12});
    const FavelaPad& P = pads[FP_Entrance];
    spawn = {P.c.x, P.y + 0.1f, P.c.y + 3.f};
    spawnYaw = 3.14159265f;  // olhando para dentro da favela (norte)
    e->renderer.fogDensity = 0.0018f;
    setTimeOfDay(e, 16.9f);   // fim de tarde, luz dourada
    e->dayCycleMin = 24.f;    // no Play: um dia por partida (~20 min: a noite passa mais rápido)
    e->rainMode = 1;          // chuva às vezes (só gotas e céu fechado: leve no celular)
}

}  // namespace

void favelaNavGraph(Engine* e, std::vector<glm::vec3>& nodes, std::vector<std::vector<int>>& adj, const std::vector<glm::vec3>& extra) {
    nodes.clear(); adj.clear();
    auto node = [&](glm::vec2 p) {
        for (size_t i = 0; i < nodes.size(); ++i)
            if (glm::length(glm::vec2(nodes[i].x, nodes[i].z) - p) < 2.5f) return int(i);
        nodes.push_back({p.x, mapHeight(e, p.x, p.y), p.y});
        adj.emplace_back();
        return int(nodes.size()) - 1;
    };
    auto link = [&](int a, int b) {
        if (a == b) return;
        if (std::find(adj[a].begin(), adj[a].end(), b) == adj[a].end()) { adj[a].push_back(b); adj[b].push_back(a); }
    };
    auto chain = [&](const std::vector<glm::vec2>& pts) {
        int prev = -1;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            int n = std::max(1, int(glm::length(pts[i + 1] - pts[i]) / 5.f));
            for (int k = 0; k <= n; ++k) {
                if (i > 0 && k == 0) continue;
                int cur = node(glm::mix(pts[i], pts[i + 1], float(k) / float(n)));
                if (prev >= 0) link(prev, cur);
                prev = cur;
            }
        }
    };
    for (const auto& rt : favelaRoutes()) chain(rt);
    chain({{0.f, 49.f}, {0.f, 96.f}});  // rua de acesso principal
    // rua em volta do morro
    std::vector<glm::vec2> ring;
    for (int i = 0; i <= 64; ++i) {
        float a = float(i) / 64.f * 6.2831853f, c = std::cos(a), sn = std::sin(a);
        float r = 1.56f / std::pow(std::pow(std::abs(c) / 64.f, 4.f) + std::pow(std::abs(sn) / 62.f, 4.f), 0.25f);
        ring.push_back({c * r, sn * r});
    }
    chain(ring);
    // patamares: o centro liga nas escadarias que chegam nele
    const FavelaPad* pads = favelaPads(e->mapSeed);
    const size_t routeNodes = nodes.size();
    for (int k = 0; k < FP_Count; ++k) {
        int c = node(pads[k].c);
        for (size_t i = 0; i < routeNodes; ++i)
            if (padDist(pads[k], nodes[i].x, nodes[i].z) < 3.f) link(c, int(i));
    }
    // pontas soltas (fim de escadaria na rua): liga no nó mais perto
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (adj[i].size() > 1) continue;
        int best = -1; float bd = 9.f;
        for (size_t j = 0; j < nodes.size(); ++j) {
            if (j == i || std::find(adj[i].begin(), adj[i].end(), int(j)) != adj[i].end()) continue;
            float d = glm::length(glm::vec2(nodes[j].x - nodes[i].x, nodes[j].z - nodes[i].z));
            if (d < bd) { bd = d; best = int(j); }
        }
        if (best >= 0) link(int(i), best);
    }
    // pontos extras (bases dos times): entram no grafo ligados ao nó mais perto
    for (const glm::vec3& b : extra) {
        int best = -1; float bd = 1e9f;
        for (size_t j = 0; j < nodes.size(); ++j) {
            float d = glm::length(glm::vec2(nodes[j].x - b.x, nodes[j].z - b.z));
            if (d < bd) { bd = d; best = int(j); }
        }
        int bn = node({b.x, b.z});
        if (best >= 0) link(bn, best);
    }
}

const std::vector<std::vector<glm::vec2>>& favelaRotas() { return favelaRoutes(); }

void registrarMapaFavela() {
    registerMap({"favela", genFavela, [](Engine* e, float x, float z) { return favelaHeight(x, z, e->mapSeed); }});
}

}  // namespace eng
