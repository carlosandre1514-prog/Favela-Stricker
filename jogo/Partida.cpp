// Favela Stricker — a partida Polícia x Bandidos (times 5 x 5, robôs, loja, granadas, online).
// É o código do jogo: usa o motor (Engine.h) e se liga a ele como um módulo (GameModule), do jeito que os
// scripts de um jogo se ligam ao motor. O motor não sabe nada desta partida.
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <functional>
#include "engine/Engine.h"
#include "engine/MapKit.h"
#include "jogo/MapaFavela.h"
#include "jogo/Partida.h"
#include "json.hpp"

namespace eng {

using json = nlohmann::json;

namespace {

bool sameHandle(Scene::Handle a, Scene::Handle b) { return a.index == b.index && a.gen == b.gen; }
// ================= Polícia x Bandidos (partida de times) =================
// Roupas da partida: os personagens da Meshy (shaders/kit/personagens/meshy, tools/preparar_personagem.py), com o
// esqueleto e as animações do jogo. Polícia: 5 (soldado, PM, BOPE, selva, choque); bandidos: 4. Todas gratuitas.
struct SkinDef { const char* name; int price; const char* modelo; };
const SkinDef kSkins[2][5] = {{{"Soldado", 0, "soldado"}, {"PM", 0, "pm"}, {"BOPE", 0, "bope"}, {"Selva", 0, "exercito"}, {"Choque", 0, "choque"}},
                              {{"Malandro", 0, "criminoso1"}, {"Vapor", 0, "criminoso2"}, {"Olheiro", 0, "criminoso3"}, {"Frente", 0, "criminoso4"}, {"", 0, ""}}};
// quantas roupas cada lado tem
const int kSkinCount[2] = {5, 4};
int clampSkin(int team, int skin) { return std::clamp(skin, 0, kSkinCount[std::clamp(team, 0, 1)] - 1); }

// Aparência de um lutador: o modelo da roupa do lado, na altura do esqueleto dos modelos
json fighterLook(int team, int skin, const std::string& name) {
    (void)name;
    team = std::clamp(team, 0, 1);
    skin = clampSkin(team, skin);
    return json{{"modelo", kSkins[team][skin].modelo}, {"altura", 1.78}};
}
// Armas da partida (tabela de dano aprovada): dano no corpo (cabeça = 2,5x), intervalo entre tiros, dispersão do
// robô, rajada do robô, bagos, pente, reserva e a partir de quantos metros o dano cai (até 30% bem de longe)
struct GunDef { const char* key; const char* name; int price; int team; float dmg, interval, spread; int burst, pellets, mag, reserve; float falloff; };
const GunDef kGuns[] = {  // só as armas realistas (modelos do kit)
    {"m4a1", "M4A1", 1200, 0, 24.f, 0.1f, 0.035f, 4, 1, 30, 90, 40.f},
    {"m4colt", "M4 Colt", 1500, 0, 26.f, 1.f / 9.f, 0.03f, 4, 1, 30, 90, 45.f},
    {"ak47", "AK-47", 1200, 1, 30.f, 0.125f, 0.045f, 3, 1, 30, 90, 35.f},
    {"ak74", "AK-74", 1400, 1, 27.f, 1.f / 9.f, 0.035f, 4, 1, 30, 90, 40.f},
    {"ak74_tatica", "AK-74 tática", 1800, 1, 28.f, 1.f / 9.5f, 0.03f, 4, 1, 30, 90, 45.f},
    {"tt33", "TT-33", 0, -1, 25.f, 1.f / 3.f, 0.03f, 2, 1, 8, 32, 20.f},  // pistola (dos dois lados)
};
constexpr int kGunCount = int(sizeof(kGuns) / sizeof(kGuns[0]));
const char* kTeamName[2] = {"Polícia", "Bandidos"};
const char* kTeamIcon[2] = {"🚓", "🔫"};

const GunDef& gunOf(const std::string& key) {
    for (const GunDef& g : kGuns) if (key == g.key) return g;
    return kGuns[0];
}
bool knownGun(const std::string& key) {
    for (const GunDef& g : kGuns) if (key == g.key) return true;
    return false;
}
// arma pedida para o jogador: qualquer arma conhecida (a arma com skin comprada vale para os dois lados);
// desconhecida, a padrão do lado (polícia M4A1, bandidos AK-47). A loja da base vende todas para os dois lados.
std::string gunForTeam(const std::string& key, int team) {
    for (const GunDef& g : kGuns) if (key == g.key) return key;
    return team ? "ak47" : "m4a1";
}
int charIndexOfRoot(Engine* e, Scene::Handle root) {
    for (size_t i = 0; i < e->characters.size(); ++i) if (sameHandle(e->characters[i].root, root)) return int(i);
    return -1;
}
json recipeOf(Engine* e, const GameObject& g) {
    if (g.recipe < 0 || g.recipe >= int(e->recipes.size())) return json();
    json r = json::parse(e->recipes[g.recipe], nullptr, false);
    return r.is_object() ? r : json();
}
int teamOf(const json& r) {
    if (!r.is_object() || !r.contains("time") || !r["time"].is_string()) return -1;
    std::string t = r["time"].get<std::string>();
    return t.rfind("pol", 0) == 0 ? 0 : t.rfind("band", 0) == 0 ? 1 : -1;
}

void equipGun(Engine* e, Fighter& f) {
    int ci = charIndexOfRoot(e, f.root);
    if (ci < 0) return;
    const std::string& k = f.weapon;
    int kind = k == "tt33" ? WK_Pistol : WK_Rifle;
    // skin de arma: a minha (jogador deste aparelho) ou a de outra pessoa da sala; robô não tem
    const std::map<std::string, std::string>* skins = f.remote ? &f.gunSkins : !f.bot ? &matchOf(e).gunSkins : nullptr;
    auto skinDe = [&](const std::string& gun) -> std::string {
        if (!skins) return {};
        auto it = skins->find(gun);
        return it == skins->end() ? std::string() : it->second;
    };
    WeaponLook look;
    // skin com modelo próprio: a arma inteira troca; na mão do jogador deste aparelho, a versão detalhada
    look.file = gunModelFile(e, gunOf(k).key, skinDe(gunOf(k).key), !f.remote && !f.bot && e->nivel >= 1);
    int w = createWeapon(e, kind, ci, look);
    // pintura da skin; a de modelo próprio tem a dela (o UV de cada modelo é outro): armas/skins/<modelo>.jpg
    auto pintar = [&](GameObject& wg, const std::string& gun, const std::string& arquivo) {
        const std::string sk = skinDe(gun);
        if (sk.empty()) return;
        float metal = -1.f, rough = -1.f;
        gunSkinFinish(gun, sk, metal, rough);
        const std::string nome = arquivo != "kit:armas/" + gun ? arquivo.substr(10) : gun + "_" + sk;
        applyKitTexture(e, wg, "armas/skins/" + nome + ".jpg", metal, rough);
    };
    if (GameObject* wg = objectById(e, w)) {
        e->game.spawned.push_back(Scene::handleOf(*wg));
        pintar(*wg, gunOf(k).key, look.file);
    }
    // a outra arma fica guardada no corpo: fuzil nas costas, pistola na coxa
    const std::string outra = k == f.primary ? f.secondary : f.primary;
    if (!outra.empty() && outra != k && knownGun(outra)) {
        WeaponLook guardada;
        guardada.file = gunModelFile(e, gunOf(outra).key, skinDe(gunOf(outra).key));
        int hw = holsterWeapon(e, ci, outra == "tt33" ? WK_Pistol : WK_Rifle, guardada);
        if (GameObject* hg = objectById(e, hw)) pintar(*hg, gunOf(outra).key, guardada.file);
    } else {
        holsterWeapon(e, ci, -1, {});
    }
    e->lastError.clear();
    CharacterInfo& c = e->characters[ci];
    const GunDef& g = gunOf(k);
    c.gunDmg = g.dmg; c.gunInterval = g.interval; c.gunSpread = g.spread; c.gunPellets = g.pellets;
    c.gunFalloff = g.falloff; c.magSize = g.mag; c.reloadTimer = 0.f;
    if (f.bot) { c.ammo = g.mag; c.reserve = -1; }
    else { c.ammo = f.mag[f.slot]; c.reserve = f.reserve[f.slot]; }
}

// Balas do jogador: pente e reserva de cada arma, cheios
void fillAmmo(Fighter& f) {
    if (f.secondary.empty() || f.secondary == f.primary) f.slot = 0;
    f.weapon = f.slot ? f.secondary : f.primary;
    for (int s = 0; s < 2; ++s) {
        const GunDef& g = gunOf(s ? f.secondary : f.primary);
        f.mag[s] = g.mag; f.reserve[s] = g.reserve;
    }
    f.grenades = 2;
}
std::string g_killWeapon;  // arma do abate em andamento ("" = a arma de quem atirou)

// Cria (ou recria, ao trocar de roupa) o personagem em jogo do lutador, em pos/yaw
void spawnFighterBody(Engine* e, Fighter& f, glm::vec3 pos, float yaw) {
    json spec = {{"tipo", "personagem"}, {"aparencia", fighterLook(f.lado, f.skin, f.name)}, {"semente", 7}};
    spec["pos"] = {pos.x, pos.y, pos.z};
    spec["rot_y"] = glm::degrees(yaw);
    spec["nome"] = f.name;
    if (!f.bot) spec["jogador"] = true;
    Scene::Handle selected = e->selected;
    int prevPlayer = e->player;
    // o corpo antigo sai (estava na lista dos criados no Play)
    if (GameObject* old = e->scene.get(f.root)) {
        Scene::Handle oh = f.root;
        int oci = charIndexOfRoot(e, oh);
        if (oci >= 0) if (GameObject* w = e->scene.get(e->characters[oci].weapon)) deleteObject(e, Scene::handleOf(*w));
        (void)old;
        deleteObject(e, oh);
    }
    int id = createFromSpecJson(e, spec.dump(), nullptr);
    e->selected = selected;
    GameObject* g = objectById(e, id);
    if (!g) { e->lastError.clear(); return; }
    f.root = Scene::handleOf(*g);
    e->game.spawned.push_back(f.root);
    int ci = charIndexOfRoot(e, f.root);
    if (f.bot && e->player == ci) e->player = prevPlayer < int(e->characters.size()) ? prevPlayer : -1;
    if (!f.bot && ci >= 0) e->player = ci;
    if (g->rigidBody) e->physics.syncFromTransform(*g->rigidBody);
    equipGun(e, f);
}

glm::vec3 groundAt(Engine* e, glm::vec3 p) {
    glm::vec3 hit; GameObject* obj = nullptr;
    if (e->physics.raycast(p + glm::vec3(0, 30.f, 0), p - glm::vec3(0, 60.f, 0), hit, obj)) return hit + glm::vec3(0, 0.1f, 0);
    return {p.x, mapHeight(e, p.x, p.z) + 0.1f, p.z};
}

int nearestNode(const TeamMatch& M, glm::vec3 p) {
    int best = -1; float bd = 1e18f;
    for (size_t i = 0; i < M.nav.size(); ++i) {
        glm::vec3 d = M.nav[i] - p; d.y *= 2.f;
        float d2 = glm::dot(d, d);
        if (d2 < bd) { bd = d2; best = int(i); }
    }
    return best;
}
std::vector<int> findPath(const TeamMatch& M, int a, int b) {
    std::vector<int> out;
    if (a < 0 || b < 0) return out;
    const int n = int(M.nav.size());
    std::vector<float> dist(n, 1e30f);
    std::vector<int> prev(n, -1);
    std::vector<bool> done(n, false);
    dist[a] = 0.f;
    for (int it = 0; it < n; ++it) {
        int u = -1;
        for (int i = 0; i < n; ++i) if (!done[i] && (u < 0 || dist[i] < dist[u])) u = i;
        if (u < 0 || dist[u] >= 1e29f) break;
        if (u == b) break;
        done[u] = true;
        for (int v : M.navAdj[u]) {
            float w = glm::length(M.nav[v] - M.nav[u]);
            if (dist[u] + w < dist[v]) { dist[v] = dist[u] + w; prev[v] = u; }
        }
    }
    if (dist[b] >= 1e29f) return out;
    for (int v = b; v >= 0; v = prev[v]) out.push_back(v);
    std::reverse(out.begin(), out.end());
    return out;
}

glm::vec3 chestOf(Engine* e, Fighter& f, float k = 0.7f) {
    int ci = charIndexOfRoot(e, f.root);
    GameObject* g = e->scene.get(f.root);
    if (!g || ci < 0) return glm::vec3(1e9f);
    return g->transform.position + glm::vec3(0, e->characters[ci].height * k, 0);
}

}  // namespace
int playerFighter(Engine* e);  // (mais abaixo, fora do namespace anônimo)
namespace {

// Caixa de munição no chão (offline): some em 40 s; o jogador pega passando por cima
void dropAmmo(Engine* e, glm::vec3 p) {
    TeamMatch& M = matchOf(e);
    json spec = {{"tipo", "forma"}, {"forma", "cubo"}, {"fisica", false}, {"paleta", "ouro"}, {"escala", {0.34f, 0.2f, 0.24f}},
                 {"pos", {p.x, p.y + 0.25f, p.z}}, {"nome", "Munição"}};
    Scene::Handle selected = e->selected;
    int id = createFromSpecJson(e, spec.dump(), nullptr);
    e->selected = selected;
    TeamMatch::Drop d;
    if (GameObject* g = objectById(e, id)) { d.h = Scene::handleOf(*g); e->game.spawned.push_back(d.h); }
    d.pos = p;
    M.drops.push_back(d);
}

// Pega a munição: um pente a mais da arma na mão e um da outra (até o dobro da reserva normal)
void pickAmmo(Engine* e, Fighter& f) {
    int ci = charIndexOfRoot(e, f.root);
    if (ci < 0) return;
    CharacterInfo& c = e->characters[ci];
    for (int s = 0; s < 2; ++s) {
        const std::string& key = s == 0 ? f.primary : f.secondary;
        if (key.empty()) continue;
        const GunDef& g = gunOf(key);
        if (s == f.slot) { if (c.reserve >= 0) c.reserve = std::min(c.reserve + g.mag, g.reserve * 2); }
        else f.reserve[s] = std::min(f.reserve[s] + g.mag, g.reserve * 2);
    }
    playSound(e, "recarga", e->scene.get(f.root) ? e->scene.get(f.root)->transform.position : glm::vec3(0.f), 0.6f);
    e->game.message = "+ Munição";
}

void updateDrops(Engine* e, float dt) {
    TeamMatch& M = matchOf(e);
    const int pf = playerFighter(e);
    GameObject* pg = pf >= 0 && !M.f[pf].dead ? e->scene.get(M.f[pf].root) : nullptr;
    for (size_t i = 0; i < M.drops.size();) {
        TeamMatch::Drop& d = M.drops[i];
        d.t += dt;
        GameObject* g = e->scene.get(d.h);
        bool pega = pg && glm::length(glm::vec2(pg->transform.position.x - d.pos.x, pg->transform.position.z - d.pos.z)) < 1.3f &&
                    std::abs(pg->transform.position.y - d.pos.y) < 1.6f;
        if (pega) pickAmmo(e, M.f[pf]);
        if (pega || d.t > 40.f || !g) {
            if (g) deleteObject(e, d.h);
            M.drops.erase(M.drops.begin() + i);
            continue;
        }
        g->transform.setPosition(d.pos + glm::vec3(0.f, 0.25f + 0.06f * std::sin(d.t * 3.f), 0.f));  // flutua e gira
        g->transform.setRotation(glm::angleAxis(d.t * 2.f, glm::vec3(0.f, 1.f, 0.f)));
        ++i;
    }
}

// Vida volta sozinha fora do combate: 5 s sem levar tiro nem atirar, 12 de vida por segundo
void regenerate(Engine* e, float dt) {
    TeamMatch& M = matchOf(e);
    GameState& G = e->game;
    const int pf = playerFighter(e);
    if (pf >= 0) {
        const int ci = charIndexOfRoot(e, M.f[pf].root);
        const int shots = ci >= 0 ? e->characters[ci].shots : 0;
        if (shots != M.playerShots) { M.playerShots = shots; M.playerShotAgo = 0.f; }
        else M.playerShotAgo += dt;
        if (!M.f[pf].dead && G.playerHp > 0.f && M.lastDamage > 5.f && M.playerShotAgo > 5.f)
            G.playerHp = std::min(G.playerMax, G.playerHp + 12.f * dt);
    }
    for (Fighter& f : M.f) {
        f.hurtAgo += dt;
        if (f.bot && !f.remote && !f.dead && f.hurtAgo > 5.f && f.shotAgo > 5.f) f.hp = std::min(100.f, f.hp + 12.f * dt);
    }
}

// skin da arma que o lutador está usando agora ("" = sem skin): a minha, a de outra pessoa da sala; robô não tem
std::string skinNaMao(const TeamMatch& M, const Fighter& f) {
    const std::map<std::string, std::string>* skins = f.remote ? &f.gunSkins : !f.bot ? &M.gunSkins : nullptr;
    if (!skins) return "";
    auto it = skins->find(gunOf(f.weapon).key);
    return it == skins->end() ? "" : it->second;
}

void killFighter(Engine* e, int vi, int ai) {
    TeamMatch& M = matchOf(e);
    Fighter& v = M.f[vi];
    if (v.dead) return;
    v.dead = true; v.deadTime = 0.f; ++v.deaths; v.path.clear();
    int ci = charIndexOfRoot(e, v.root);
    if (ci >= 0) playDeath(e->characters[ci]);
    if (GameObject* g = e->scene.get(v.root); g && g->rigidBody) e->physics.setEnabled(*g->rigidBody, false);
    // offline: o inimigo abatido derruba uma caixa de munição onde caiu
    if (!M.online && v.bot && ai >= 0 && ai < int(M.f.size()) && !M.f[ai].bot)
        if (GameObject* g = e->scene.get(v.root)) dropAmmo(e, g->transform.position);
    std::string msg;
    {
        TeamMatch::Feed fd;
        fd.victim = v.name; fd.vt = v.team; fd.you = !v.bot;
        if (ai >= 0 && ai != vi && M.f[ai].team != v.team) {
            fd.killer = M.f[ai].name; fd.kt = M.f[ai].team; fd.you = fd.you || !M.f[ai].bot;
            fd.weapon = g_killWeapon.empty() ? gunOf(M.f[ai].weapon).name : g_killWeapon;
            // abate com a arma (não granada) de skin: a tela usa a mensagem própria dela, se tiver
            const bool deTiro = g_killWeapon.empty() || g_killWeapon == gunOf(M.f[ai].weapon).name;  // (granada não)
            const std::string sk = deTiro ? skinNaMao(M, M.f[ai]) : "";
            if (!sk.empty()) fd.skin = std::string(gunOf(M.f[ai].weapon).key) + ":" + sk;
        }
        M.feed.push_back(fd);
        if (M.feed.size() > 5) M.feed.erase(M.feed.begin());
    }
    if (ai >= 0 && ai != vi && M.f[ai].team != v.team) {
        Fighter& a = M.f[ai];
        ++a.kills;
        if (M.modo == 0) { ++M.score[a.team]; a.money += 300; }
        msg = std::string(kTeamIcon[a.lado]) + " " + a.name + " abateu " + kTeamIcon[v.lado] + " " + v.name;
        if (!a.bot && M.modo == 0) msg += "  (+$300)";
    } else msg = std::string(kTeamIcon[v.lado]) + " " + v.name + " caiu";
    if (!v.bot) {
        if (M.modo == 0) msg = "Você foi abatido! Renasce na base em 5 s";
        else {
            bool companheiro = false;
            for (const Fighter& o : M.f) if (&o != &v && o.team == v.team && !o.dead && !o.fora) companheiro = true;
            msg = M.zona.fase >= 4 ? "Você caiu!" : M.tamanhoTime == 1 ? "Você caiu! Volta em 10 s" : companheiro ? "Você caiu! Renasce em 10 s perto do seu time" : "Você caiu!";
        }
    }
    e->game.message = msg;
    if (M.online && !v.remote)  // online: quem caiu aqui avisa os outros celulares
        M.netOut.push_back(json{{"t", "abate"}, {"v", v.netId}, {"a", ai >= 0 && ai < int(M.f.size()) ? M.f[ai].netId : ""},
                                {"arma", g_killWeapon.empty() && ai >= 0 && ai < int(M.f.size()) ? gunOf(M.f[ai].weapon).name : g_killWeapon}}.dump());
}

// Distância até o inimigo vivo mais perto
float enemyDist(Engine* e, int team, glm::vec3 p) {
    float best = 1e9f;
    for (Fighter& o : matchOf(e).f) {
        if (o.team == team || o.dead) continue;
        if (GameObject* g = e->scene.get(o.root)) best = std::min(best, glm::length(g->transform.position - p));
    }
    return best;
}

// Onde renascer: um ponto do time sem inimigo por perto (sorteado entre os livres). Se todos estão tomados,
// os bandidos renascem num lugar qualquer dentro da favela longe da polícia; a polícia, no ponto mais longe dos bandidos.
glm::vec3 spawnPoint(Engine* e, int team) {
    TeamMatch& M = matchOf(e);
    const float safe = 25.f;
    std::vector<glm::vec3> free;
    glm::vec3 far = M.base[team];
    float farD = -1.f;
    for (const glm::vec3& s : M.spawns[team]) {
        float d = enemyDist(e, team, s);
        if (d > safe) free.push_back(s);
        if (d > farD) { farD = d; far = s; }
    }
    if (!free.empty()) return free[e->fxRng.irange(0, int(free.size()) - 1)];
    if (team == 1 && !M.nav.empty()) {
        std::vector<glm::vec3> spots;
        for (const glm::vec3& n : M.nav) {
            float ax = n.x / 64.f, az = n.z / 62.f;
            if (ax * ax * ax * ax + az * az * az * az > 1.25f * 1.25f * 1.25f * 1.25f) continue;  // fora da favela
            if (enemyDist(e, team, n) > safe + 10.f) spots.push_back(n);
        }
        if (!spots.empty()) return spots[e->fxRng.irange(0, int(spots.size()) - 1)];
    }
    return far;
}

void reviveAt(Engine* e, Fighter& f, glm::vec3 p, const char* aviso);
void respawn(Engine* e, Fighter& f) {
    float a = e->fxRng.range(0.f, 6.2831853f), r = e->fxRng.range(0.5f, 2.5f);
    reviveAt(e, f, groundAt(e, spawnPoint(e, f.team) + glm::vec3(std::cos(a) * r, 0.f, std::sin(a) * r)), "De volta na base");
}
// Volta à vida em p (vida cheia, munição do começo)
void reviveAt(Engine* e, Fighter& f, glm::vec3 p, const char* aviso) {
    GameObject* g = e->scene.get(f.root);
    int ci = charIndexOfRoot(e, f.root);
    if (!g || ci < 0) return;
    g->transform.setPosition(p);
    if (g->rigidBody) { e->physics.setEnabled(*g->rigidBody, true); e->physics.syncFromTransform(*g->rigidBody); g->rigidBody->setVelocity(glm::vec3(0.f)); }
    CharacterInfo& c = e->characters[ci];
    c.dying = false; c.ammo = c.magSize; c.reloadTimer = 0.f;
    if (c.anim && c.clipMap[CC_Idle] >= 0) { c.anim->setClip(c.clipMap[CC_Idle]); c.anim->loop = true; c.anim->speed = 1.f; }
    f.dead = false; f.hp = 100.f; f.path.clear(); f.target = -1;
    if (!f.bot) {
        e->game.playerHp = e->game.playerMax; e->game.message = aviso;
        int was = f.slot;
        f.slot = 0;
        fillAmmo(f);
        if (was != 0) equipGun(e, f);
        else { c.ammo = f.mag[0]; c.reserve = f.reserve[0]; }
    }
}

void botShoot(Engine* e, int fi, int ti) {
    TeamMatch& M = matchOf(e);
    Fighter& f = M.f[fi];
    int ci = charIndexOfRoot(e, f.root);
    GameObject* go = e->scene.get(f.root);
    if (ci < 0 || !go) return;
    CharacterInfo& c = e->characters[ci];
    const GunDef& gun = gunOf(f.weapon);
    glm::vec3 from = chestOf(e, f, 0.72f), to = chestOf(e, M.f[ti], 0.6f);
    float dist = glm::length(to - from);
    btRigidBody* self = go->rigidBody ? go->rigidBody->body : nullptr;
    glm::vec3 muzzle = from;
    if (GameObject* w = e->scene.get(c.weapon)) muzzle = glm::vec3(w->transform.world() * glm::vec4(c.muzzle, 1.f));
    playSound(e, c.shotSound.empty() ? "tiro_pistola" : c.shotSound.c_str(), muzzle, 1.f);
    c.aimTimer = 1.5f; c.fireAnim = 0.3f;
    f.shotAgo = 0.f;
    ++c.shots;
    for (int p = 0; p < gun.pellets; ++p) {
        float err = gun.spread + 0.012f + dist * 0.0012f;  // robô erra um pouco (mais de longe)
        glm::vec3 j(e->fxRng.range(-1.f, 1.f), e->fxRng.range(-0.7f, 0.7f), e->fxRng.range(-1.f, 1.f));
        glm::vec3 dir = glm::normalize(glm::normalize(to - from) + j * err);
        glm::vec3 hit; GameObject* obj = nullptr; float frac;
        bool any = e->physics.raycastIgnoring(from, from + dir * (dist + 25.f), self, hit, obj, frac);
        glm::vec3 end = any ? hit : from + dir * (dist + 25.f);
        if (p == 0) muzzleFx(e, c.weapon, muzzle, glm::normalize(end - muzzle));
        bulletFx(e, c.weapon, muzzle, end);
        if (any) {
            impactFx(e, c.weapon, hit, dir);
            M.attacker = fi;
            const float far = dist > gun.falloff ? std::max(0.3f, 1.f - 0.7f * (dist - gun.falloff) / (1.5f * gun.falloff)) : 1.f;
            damageAt(e, obj, gun.dmg * far, from);
            M.attacker = -1;
        }
    }
}

bool visible(Engine* e, Fighter& a, Fighter& b) {
    GameObject* ga = e->scene.get(a.root);
    GameObject* gb = e->scene.get(b.root);
    if (!ga || !gb) return false;
    glm::vec3 from = chestOf(e, a, 0.85f), to = chestOf(e, b, 0.7f);
    glm::vec3 hit; GameObject* obj = nullptr; float frac;
    btRigidBody* self = ga->rigidBody ? ga->rigidBody->body : nullptr;
    if (!e->physics.raycastIgnoring(from, to, self, hit, obj, frac)) return true;
    return rootOf(e, obj) == gb;
}

// ======================= Battle Royale Ressurgência =======================
// 10 times de 4 caem do céu espalhados pela favela. Quem cai renasce em 10 s perto de um companheiro vivo; o time
// que fica todo no chão ao mesmo tempo está fora. A zona fecha em 6 fases (fora dela a vida vai embora) e nas duas
// últimas ninguém renasce mais. Vence o último time de pé.
struct FaseZona { float espera, fecha, raio, dano; };
const FaseZona kFases[] = {{60.f, 45.f, 75.f, 2.f}, {50.f, 40.f, 50.f, 4.f}, {45.f, 35.f, 32.f, 6.f},
                           {40.f, 30.f, 18.f, 10.f}, {35.f, 25.f, 8.f, 15.f}, {30.f, 30.f, 0.f, 25.f}};
constexpr int kFasesN = 6;
constexpr int kFaseSemRenascer = 4;  // da 5ª fase em diante, quem cai está fora
constexpr int kTimesBR = 10, kPorTimeBR = 4, kMaxTimesBR = 40;  // offline (testes): 10 x 4; online: até 40 times (solo)
constexpr float kRenasceBR = 10.f, kZonaInicial = 110.f, kAlturaQueda = 22.f;
const char* kNomesBR[] = {"Biel", "Dedé", "Neguin", "Tico", "Juninho", "Kaká", "Lipe", "Nando", "Rafa", "Tuta", "Vini", "Zé",
                          "Bruninho", "Caio", "Davi", "Edu", "Fael", "Guto", "Hugo", "Igor", "Jow", "Kauã", "Léo", "Márcio",
                          "Nico", "Pedrinho", "Quinho", "Rodrigo", "Sid", "Teteu", "Uri", "Valdo", "Wesley", "Xandi", "Yuri",
                          "Zeca", "Batata", "Chico", "Duda"};

bool naFavela(glm::vec3 n) {  // dentro da favela (o resto do mapa é morro e rua de fora)
    const float ax = n.x / 64.f, az = n.z / 62.f;
    return ax * ax * ax * ax + az * az * az * az <= 1.25f * 1.25f * 1.25f * 1.25f;
}
float distZona(const TeamMatch::Zona& z, glm::vec3 p) { return glm::length(glm::vec2(p.x, p.z) - z.c); }

// centro do próximo círculo: um ponto de caminho da favela que deixa o círculo novo inteiro dentro do atual
glm::vec2 proximoCentro(Engine* e, glm::vec2 c, float r, float rProx) {
    const TeamMatch& M = matchOf(e);
    const float folga = std::max(0.f, r - rProx);
    for (int t = 0; t < 60 && !M.nav.empty(); ++t) {
        const glm::vec3& n = M.nav[matchOf(e).zonaRng.irange(0, int(M.nav.size()) - 1)];
        if (naFavela(n) && glm::length(glm::vec2(n.x, n.z) - c) <= folga) return {n.x, n.z};
    }
    return c;
}

// um lugar para ir: dentro do círculo seguro (o próximo, se já foi anunciado)
glm::vec3 lugarNaZona(Engine* e) {
    const TeamMatch& M = matchOf(e);
    const auto& z = M.zona;
    const glm::vec2 c = z.rPara > 0.f ? z.para : z.c;
    const float r = std::max(3.f, (z.rPara > 0.f ? z.rPara : z.r) * 0.8f);
    int perto = -1; float pd = 1e9f;
    for (int t = 0; t < 40 && !M.nav.empty(); ++t) {
        const int i = e->fxRng.irange(0, int(M.nav.size()) - 1);
        const float d = glm::length(glm::vec2(M.nav[i].x, M.nav[i].z) - c);
        if (d <= r) return M.nav[i];
        if (d < pd) { pd = d; perto = i; }
    }
    return perto >= 0 ? M.nav[perto] : glm::vec3(c.x, 0.f, c.y);
}

void botUpdate(Engine* e, int fi, float dt);
void updateNades(Engine* e, float dt);
void remoteUpdate(Engine* e, Fighter& f, float dt);

void brStart(Engine* e) {
    TeamMatch& M = matchOf(e);
    GameState& G = e->game;
    // online: a sala manda a semente (zona e pontos de queda iguais em todos os celulares) e as pessoas, cada uma
    // com o time (0..9) que quem criou a sala sorteou. Só gente: nenhum robô.
    const json ro = M.online ? json::parse(M.roster, nullptr, false) : json();
    const bool online = ro.is_object() && ro.contains("jogadores") && ro["jogadores"].is_array();
    M.zonaRng = Rng(online ? ro.value("semente", 1u) : uint64_t(e->fxRng.next()) + 1u);
    M.times = kTimesBR;
    M.tamanhoTime = online ? std::clamp(ro.value("tamanho", 4), 1, 4) : kPorTimeBR;
    if (online) {
        int maior = 0;
        for (const json& j : ro["jogadores"]) if (j.is_object()) maior = std::max(maior, std::clamp(j.value("time", 0), 0, kMaxTimesBR - 1));
        M.times = maior + 1;
    }
    M.colocacao.assign(M.times, 0);
    // pontos de queda: espalhados pela favela (cada novo, o mais longe possível dos já escolhidos)
    std::vector<glm::vec3> cand;
    for (const glm::vec3& n : M.nav) if (naFavela(n)) cand.push_back(n);
    if (cand.empty()) cand.push_back(glm::vec3(0.f));
    std::vector<glm::vec3> pousos{cand[M.zonaRng.irange(0, int(cand.size()) - 1)]};
    while (int(pousos.size()) < std::max(M.times, kTimesBR)) {
        int best = 0; float bd = -1.f;
        for (size_t i = 0; i < cand.size(); ++i) {
            float d = 1e9f;
            for (const glm::vec3& p : pousos) d = std::min(d, glm::length(glm::vec2(cand[i].x - p.x, cand[i].z - p.z)));
            if (d > bd) { bd = d; best = int(i); }
        }
        pousos.push_back(cand[best]);
    }
    static const char* armas[] = {"m4a1", "m4colt", "ak47", "ak74", "ak74_tatica"};
    int nome = e->fxRng.irange(0, int(std::size(kNomesBR)) - 1);
    if (online) {
        std::vector<int> noTime(kMaxTimesBR, 0);
        for (const json& j : ro["jogadores"]) {
            if (!j.is_object()) continue;
            Fighter f;
            f.netId = j.value("id", "");
            if (f.netId.empty()) continue;
            f.team = std::clamp(j.value("time", 0), 0, kMaxTimesBR - 1);
            f.lado = std::clamp(j.value("lado", 0), 0, 1);
            f.skin = clampSkin(f.lado, j.value("skin", 0));
            const bool eu = f.netId == M.localId;
            f.bot = !eu;
            f.remote = !eu;
            f.name = eu ? "Você" : j.value("nome", "Jogador");
            f.money = 0;
            const std::string arma = j.value("arma", "");
            f.weapon = knownGun(arma) && arma != "tt33" ? arma : "m4a1";
            f.primary = f.weapon;
            const std::string arma2 = j.value("arma2", "tt33");
            f.secondary = knownGun(arma2) ? arma2 : "";
            if (eu) {
                if (knownGun(M.playerPrimary) && M.playerPrimary != "tt33") f.weapon = f.primary = M.playerPrimary;
                f.slot = 0;
                fillAmmo(f);
            } else if (auto sk = j.find("skins"); sk != j.end() && sk->is_object())
                for (auto it = sk->begin(); it != sk->end(); ++it)
                    if (it->is_string() && knownGun(it.key())) f.gunSkins[it.key()] = it->get<std::string>();
            f.netWeapon = f.weapon;
            const int k = noTime[f.team]++;
            const float a = 6.2831853f * k / std::max(1, M.tamanhoTime) + 0.3f;
            glm::vec3 p = groundAt(e, pousos[f.team] + glm::vec3(std::cos(a), 0.f, std::sin(a)) * 1.8f);
            p.y += kAlturaQueda;
            M.f.push_back(f);
            spawnFighterBody(e, M.f.back(), p, a);
            M.f.back().lastPos = p;
            if (M.f.back().remote)  // comandado pela rede: o corpo só segue as posições que chegam
                if (GameObject* rg = e->scene.get(M.f.back().root); rg && rg->rigidBody) e->physics.setKinematic(*rg->rigidBody, true);
        }
    } else
    for (int t = 0; t < kTimesBR; ++t)
        for (int k = 0; k < kPorTimeBR; ++k) {
            Fighter f;
            f.team = t;
            f.bot = !(t == 0 && k == 0);
            f.lado = f.bot ? e->fxRng.irange(0, 1) : std::clamp(M.playerTeam, 0, 1);
            f.skin = f.bot ? e->fxRng.irange(0, kSkinCount[f.lado] - 1) : clampSkin(f.lado, std::max(0, M.playerSkin));
            f.name = f.bot ? kNomesBR[nome++ % int(std::size(kNomesBR))] : "Você";
            f.money = 0;
            if (f.bot) {
                f.weapon = armas[e->fxRng.irange(0, 4)];
                f.primary = f.weapon;
                f.secondary = "tt33";
            } else {  // começa com a arma escolhida (e a skin dela)
                f.weapon = knownGun(M.playerPrimary) ? M.playerPrimary : "m4a1";
                f.primary = f.weapon;
                f.secondary = knownGun(M.playerSecondary) ? M.playerSecondary : "";
                f.slot = 0;
                fillAmmo(f);
            }
            const float a = 6.2831853f * k / kPorTimeBR + e->fxRng.range(0.f, 0.6f);
            glm::vec3 p = groundAt(e, pousos[t] + glm::vec3(std::cos(a), 0.f, std::sin(a)) * 1.8f);
            p.y += kAlturaQueda;  // cai do céu
            M.f.push_back(f);
            spawnFighterBody(e, M.f.back(), p, e->fxRng.range(0.f, 6.2831853f));
            M.f.back().lastPos = p;
        }
    // a zona: começa cobrindo a favela inteira; o primeiro círculo já é anunciado
    auto& z = M.zona;
    z = TeamMatch::Zona{};
    z.c = glm::vec2(0.f);
    z.r = kZonaInicial;
    z.rPara = kFases[0].raio;
    z.para = proximoCentro(e, z.c, z.r, z.rPara);
    G.playerHp = G.playerMax = 100.f;
    G.message = "Ressurgência: " + std::to_string(M.times) + " times. Fique dentro da zona!";
}

// Volta perto de um companheiro vivo (dentro da zona, sem inimigo colado), caindo do céu
void brRenascer(Engine* e, Fighter& f) {
    TeamMatch& M = matchOf(e);
    glm::vec3 alvo(1e9f);
    for (const Fighter& o : M.f)
        if (&o != &f && o.team == f.team && !o.dead && !o.fora)
            if (GameObject* g = e->scene.get(o.root)) { alvo = g->transform.position; break; }
    std::vector<glm::vec3> spots;
    if (alvo.x > 1e8f) {  // solo: qualquer lugar dentro da zona sem ninguém perto
        for (const glm::vec3& n : M.nav)
            if (naFavela(n) && distZona(M.zona, n) < M.zona.r - 4.f && enemyDist(e, f.team, n) > 25.f) spots.push_back(n);
        if (spots.empty()) for (const glm::vec3& n : M.nav) if (distZona(M.zona, n) < M.zona.r) spots.push_back(n);
        if (spots.empty()) return;
        glm::vec3 p = groundAt(e, spots[e->fxRng.irange(0, int(spots.size()) - 1)]);
        p.y += kAlturaQueda;
        reviveAt(e, f, p, "De volta! Caiu de novo na zona");
        return;
    }
    for (const glm::vec3& n : M.nav) {
        const float d = glm::length(glm::vec2(n.x - alvo.x, n.z - alvo.z));
        if (d < 10.f || d > 40.f || distZona(M.zona, n) > M.zona.r - 4.f) continue;
        if (enemyDist(e, f.team, n) > 18.f) spots.push_back(n);
    }
    glm::vec3 p = spots.empty() ? alvo + glm::vec3(e->fxRng.range(-3.f, 3.f), 0.f, e->fxRng.range(-3.f, 3.f))
                                : spots[e->fxRng.irange(0, int(spots.size()) - 1)];
    p = groundAt(e, p);
    p.y += kAlturaQueda;
    reviveAt(e, f, p, "De volta! Caiu perto do seu time");
}

void brUpdate(Engine* e, float dt) {
    TeamMatch& M = matchOf(e);
    GameState& G = e->game;
    auto& z = M.zona;
    updateNades(e, dt);
    for (auto& fd : M.feed) fd.age += dt;
    for (auto& f : M.f) f.shotAgo += dt;
    M.lastDamage += dt;
    updateDrops(e, dt);
    if (G.state == 0) regenerate(e, dt);
    // a zona fecha em fases: espera (o próximo círculo já aparece) e depois fecha até ele
    if (G.state == 0 && z.fase < kFasesN) {
        const FaseZona& F = kFases[z.fase];
        z.t += dt;
        if (!z.fechando) {
            if (z.t >= F.espera) { z.fechando = true; z.t = 0.f; z.de = z.c; z.rDe = z.r; }
        } else {
            const float u = std::min(1.f, z.t / F.fecha);
            z.c = glm::mix(z.de, z.para, u);
            z.r = glm::mix(z.rDe, z.rPara, u);
            if (u >= 1.f) {
                z.fechando = false; z.t = 0.f; ++z.fase;
                if (z.fase < kFasesN) { z.rPara = kFases[z.fase].raio; z.para = proximoCentro(e, z.c, z.r, z.rPara); }
                else z.rPara = 0.f;
                e->game.message = z.fase == kFaseSemRenascer ? "Zona final: quem cair agora está fora!" : "A zona fechou. Corra para o próximo círculo!";
            }
        }
    }
    // queda do céu (começo e renascer): cai devagar, como de paraquedas (rápido demais atravessava o chão);
    // se mesmo assim alguém passar para baixo do mapa, volta para o chão ali em cima
    for (Fighter& f : M.f) {
        if (f.dead || f.remote) continue;
        GameObject* g = e->scene.get(f.root);
        if (!g || !g->rigidBody || !g->rigidBody->body) continue;
        btRigidBody* b = g->rigidBody->body;
        const btVector3 v = b->getLinearVelocity();
        if (v.y() < -6.f) b->setLinearVelocity(btVector3(v.x(), -6.f, v.z()));
        const glm::vec3 p = g->transform.position;
        if (p.y < mapHeight(e, p.x, p.z) - 3.f) {
            g->transform.setPosition(groundAt(e, glm::vec3(p.x, mapHeight(e, p.x, p.z) + 2.f, p.z)));
            e->physics.syncFromTransform(*g->rigidBody);
            g->rigidBody->setVelocity(glm::vec3(0.f));
        }
    }
    // a parede da zona: fumaça vermelha subindo na borda do círculo, no pedaço perto do jogador
    if (G.state == 0 && z.r > 0.5f) {
        const int pf = playerFighter(e);
        GameObject* pg = pf >= 0 ? e->scene.get(M.f[pf].root) : nullptr;
        if (pg) {
            const glm::vec3 pp = pg->transform.position;
            const glm::vec2 d = glm::vec2(pp.x, pp.z) - z.c;
            const float dist = glm::length(d);
            if (std::abs(dist - z.r) < 70.f) {
                const float ang = std::atan2(d.y, d.x), meia = std::min(3.14159f, 45.f / std::max(z.r, 1.f));
                const int n = int(360.f * dt + e->fxRng.f());
                for (int k = 0; k < n; ++k) {
                    const float a = ang + e->fxRng.range(-meia, meia);
                    ParticleSpawn s;
                    s.pos = glm::vec3(z.c.x + std::cos(a) * z.r, pp.y + e->fxRng.range(-3.f, 12.f), z.c.y + std::sin(a) * z.r);
                    s.vel = glm::vec3(0.f, e->fxRng.range(0.3f, 0.9f), 0.f);
                    s.life = e->fxRng.range(1.8f, 2.8f);
                    s.size0 = e->fxRng.range(2.2f, 3.f); s.size1 = e->fxRng.range(3.8f, 5.2f);
                    s.color0 = glm::vec4(0.95f, 0.22f, 0.16f, 0.f); s.color1 = glm::vec4(0.75f, 0.12f, 0.1f, 0.f);
                    s.color0.a = 0.24f;
                    s.shape = PS_Smoke; s.additive = false; s.rot = e->fxRng.range(0.f, 6.28f);
                    e->particles.emit(s);
                }
            }
        }
    }
    // fora da zona perde vida (duas vezes por segundo)
    if (G.state == 0 && (z.tick += dt) >= 0.5f) {
        z.tick -= 0.5f;
        const float dano = kFases[std::min(z.fase, kFasesN - 1)].dano * 0.5f;
        for (size_t i = 0; i < M.f.size(); ++i) {
            Fighter& f = M.f[i];
            GameObject* g = f.dead || f.remote ? nullptr : e->scene.get(f.root);
            if (!g || distZona(z, g->transform.position) <= z.r) continue;
            if (!f.bot) {
                hurtPlayer(e, dano);
                M.lastDamage = 0.f;
                if (G.playerHp <= 0.f) { g_killWeapon = "Zona"; killFighter(e, int(i), -1); g_killWeapon.clear(); }
            } else {
                f.hp -= dano;
                f.hurtAgo = 0.f;
                if (f.hp <= 0.f) { g_killWeapon = "Zona"; killFighter(e, int(i), -1); g_killWeapon.clear(); }
            }
        }
    }
    auto temVivo = [&](int t) {
        for (const Fighter& o : M.f) if (o.team == t && !o.dead && !o.fora) return true;
        return false;
    };
    for (size_t i = 0; i < M.f.size(); ++i) {
        Fighter& f = M.f[i];
        if (f.dead) {
            if (f.fora) continue;
            // de outro celular: só o celular dela sabe se ela volta (o meu pode estar atrasado); saiu da sala: fora
            if (f.remote) {
                if (f.netFora || f.left) { f.fora = true; continue; }
                f.deadTime += dt;
                remoteUpdate(e, f, dt);
                continue;
            }
            // sem ninguém do time de pé (ou zona final): fora da partida
            if ((M.tamanhoTime > 1 && !temVivo(f.team)) || z.fase >= kFaseSemRenascer || f.left) { f.fora = true; continue; }
            f.deadTime += dt;
            if (f.remote) remoteUpdate(e, f, dt);  // de outro celular: volta quando ele voltar lá
            else if (f.deadTime >= kRenasceBR) brRenascer(e, f);
            continue;
        }
        if (f.remote) { remoteUpdate(e, f, dt); continue; }
        if (!f.bot) {
            if (G.playerHp <= 0.f) killFighter(e, int(i), M.attacker);
            continue;
        }
        if (G.state == 0) botUpdate(e, int(i), dt);
    }
    // times que ficaram todos fora: a colocação é quantos times ainda estavam vivos
    int vivos = 0;
    for (int t = 0; t < M.times; ++t) if (!M.colocacao[t]) ++vivos;
    for (int t = 0; t < M.times; ++t) {
        if (M.colocacao[t]) continue;
        bool algum = false;
        for (const Fighter& o : M.f) if (o.team == t && !o.fora) algum = true;
        if (algum) continue;
        M.colocacao[t] = vivos--;
        TeamMatch::Feed fd;
        fd.victim = "Time " + std::to_string(t + 1) + " eliminado";
        fd.vt = t;
        M.feed.push_back(fd);
        if (M.feed.size() > 5) M.feed.erase(M.feed.begin());
    }
    if (G.state != 0) return;
    const int pf = playerFighter(e);
    if (pf < 0) return;
    const int meu = M.f[pf].team;
    if (M.colocacao[meu]) {
        G.state = 2;
        G.message = "Seu time ficou em " + std::to_string(M.colocacao[meu]) + "º lugar";
    } else if (vivos <= 1) {
        M.colocacao[meu] = 1;
        G.state = 1;
        G.message = "Vitória! Último time de pé";
    }
}

void botUpdate(Engine* e, int fi, float dt) {
    TeamMatch& M = matchOf(e);
    Fighter& f = M.f[fi];
    GameObject* go = e->scene.get(f.root);
    int ci = charIndexOfRoot(e, f.root);
    if (!go || ci < 0) return;
    CharacterInfo& c = e->characters[ci];
    glm::vec3 pos = go->transform.position;
    // escolhe o alvo: inimigo vivo mais perto que dá para ver
    if ((f.think -= dt) <= 0.f) {
        f.think = e->fxRng.range(0.2f, 0.35f);
        int best = -1; float bd = M.modo == 1 ? 42.f : 55.f;  // Ressurgência: vê um pouco menos longe (são 40)
        for (size_t k = 0; k < M.f.size(); ++k) {
            Fighter& o = M.f[k];
            if (o.team == f.team || o.dead) continue;
            GameObject* og = e->scene.get(o.root);
            if (!og) continue;
            float d = glm::length(og->transform.position - pos);
            if (d < bd && visible(e, f, o)) { bd = d; best = int(k); }
        }
        if (best != f.target) {
            f.burst = 0;
            // Ressurgência: viu alguém novo, leva um tempinho para reagir (como gente), e não já sai atirando
            if (M.modo == 1 && best >= 0) f.fireTimer = std::max(f.fireTimer, e->fxRng.range(0.6f, 1.2f));
        }
        f.target = best;
    }
    glm::vec3 move(0.f), face(0.f);
    float speed = 5.f;
    bool jump = false;
    if (f.target >= 0 && !M.f[f.target].dead && e->scene.get(M.f[f.target].root)) {
        glm::vec3 to = e->scene.get(M.f[f.target].root)->transform.position - pos;
        to.y = 0.f;
        float d = glm::length(to);
        face = to;
        speed = 2.8f;
        glm::vec3 dir = d > 1e-3f ? to / d : glm::vec3(0, 0, 1), side(-dir.z, 0.f, dir.x);
        if ((f.strafeTimer -= dt) <= 0.f) { f.strafeTimer = e->fxRng.range(0.8f, 2.2f); f.strafe = e->fxRng.chance(0.5f) ? 1.f : -1.f; }
        if (d > 24.f) move = dir;
        else if (d < 7.f) move = -dir * 0.7f + side * f.strafe * 0.5f;
        else move = side * f.strafe * 0.8f;
        // atira em rajadas
        const GunDef& gun = gunOf(f.weapon);
        if ((f.fireTimer -= dt) <= 0.f) {
            botShoot(e, fi, f.target);
            if (++f.burst >= gun.burst) { f.burst = 0; f.fireTimer = gun.interval + e->fxRng.range(0.5f, 1.2f); }
            else f.fireTimer = gun.interval;
        }
    } else {
        // sem inimigo à vista: segue pelas escadarias até um objetivo
        if (!M.nav.empty()) {
            // Ressurgência: destino que a zona engoliu (ou longe dela) é trocado por um lugar dentro da zona
            if (M.modo == 1 && !f.path.empty() && glm::length(glm::vec2(f.goal.x, f.goal.z) - M.zona.c) > M.zona.r * 0.9f) f.path.clear();
            if (f.path.empty() || f.pathPos >= f.path.size()) {
                glm::vec3 goal;
                float pick = e->fxRng.f();
                if (M.modo == 1) goal = lugarNaZona(e);
                else if (f.team == 0) goal = pick < 0.55f ? M.base[1] : M.nav[e->fxRng.irange(0, int(M.nav.size()) - 1)];
                else goal = pick < 0.2f ? M.base[0] : M.nav[e->fxRng.irange(0, int(M.nav.size()) - 1)];
                f.goal = goal;
                f.path = findPath(M, nearestNode(M, pos), nearestNode(M, goal));
                f.pathPos = 0;
            }
            if (f.pathPos < f.path.size()) {
                glm::vec3 wp = M.nav[f.path[f.pathPos]], to = wp - pos;
                to.y = 0.f;
                float d = glm::length(to);
                if (d < 1.6f) ++f.pathPos;
                else move = to / d;
            }
        } else {
            glm::vec3 to = M.base[1 - f.team] - pos;
            to.y = 0.f;
            if (glm::length(to) > 4.f) move = glm::normalize(to);
        }
    }
    // preso (parede, quina): pula e muda o caminho
    if ((f.stuckTimer += dt) > 1.5f) {
        if (glm::length(move) > 0.1f && glm::length(glm::vec2(pos.x - f.lastPos.x, pos.z - f.lastPos.z)) < 0.6f) {
            jump = true;
            if (!f.path.empty()) { if (f.pathPos + 1 < f.path.size()) ++f.pathPos; else f.path.clear(); }
            f.strafe = -f.strafe;
        }
        f.stuckTimer = 0.f;
        f.lastPos = pos;
    }
    driveCharacter(e, c, move, speed, jump, dt, face);
}

void teamStart(Engine* e) {
    TeamMatch& M = matchOf(e);
    int chosen = M.playerTeam, skin = M.playerSkin;
    std::string primary = M.playerPrimary, secondary = M.playerSecondary;
    auto gunSkins = M.gunSkins;
    bool online = M.online, host = M.host;
    std::string localId = M.localId, roster = M.roster;
    const int modo = M.modo;
    M = TeamMatch{};
    M.playerTeam = chosen; M.playerSkin = skin; M.playerPrimary = primary; M.playerSecondary = secondary;
    M.gunSkins = gunSkins;
    M.online = online; M.host = host; M.localId = localId; M.roster = roster;
    M.modo = modo;
    if (e->rules.objetivo != "times") return;
    M.active = true;
    // a partida começa de manhã: o dia passa (manhã, tarde, noite) e volta a amanhecer até acabar o tempo
    // (um dia inteiro em ciclo_min do projeto: 10 min, numa partida de 20 min são dois dias)
    setTimeOfDay(e, 6.5f);
    // bases e lojas do projeto
    glm::vec3 bsum[2]{}; int bn[2] = {0, 0}, tn[2] = {0, 0};
    glm::vec3 tsum[2]{};
    struct Slot { Scene::Handle h; int team; bool player; };
    std::vector<Slot> slots;
    Scene::Handle playerRoot = e->player >= 0 && e->player < int(e->characters.size()) ? e->characters[e->player].root : Scene::Handle{};
    for (GameObject* go : [&] { std::vector<GameObject*> v; e->scene.forEach([&](GameObject& g, Scene::Handle) { if (g.id >= 0 && !g.transform.parent && g.recipe >= 0 && g.active) v.push_back(&g); }); return v; }()) {
        json r = recipeOf(e, *go);
        std::string tipo = r.value("tipo", "");
        int team = teamOf(r);
        if (tipo == "base" && team >= 0) { bsum[team] += go->transform.position; ++bn[team]; M.spawns[team].push_back(go->transform.position); }
        else if (tipo == "loja") M.shops.push_back({Scene::handleOf(*go), r.value("loja", "armas") == "roupas" ? 1 : 0, std::max(team, 0)});
        else if (charOf(e, go)) {
            bool isPlayer = sameHandle(Scene::handleOf(*go), playerRoot);
            if (team < 0 && !isPlayer) continue;  // personagem sem time: fica de fora
            int t = isPlayer ? (M.playerTeam >= 0 ? M.playerTeam : std::max(team, 0)) : team;
            slots.push_back({Scene::handleOf(*go), t, isPlayer});
            tsum[t] += go->transform.position; ++tn[t];
        }
    }
    for (int t = 0; t < 2; ++t) M.base[t] = bn[t] ? bsum[t] / float(bn[t]) : tn[t] ? tsum[t] / float(tn[t]) : glm::vec3(t ? 0.f : 0.f, 0.f, t ? -20.f : 20.f);
    for (int t = 0; t < 2; ++t) if (M.spawns[t].empty()) M.spawns[t].push_back(M.base[t]);
    // com o jogador num time, um robô desse time sai (5 x 5)
    int perTeam[2] = {0, 0};
    for (auto& s : slots) ++perTeam[s.team];
    std::vector<json> slotRemote(slots.size());
    std::vector<int> slotBot(slots.size(), -1);
    if (M.online) {
        // online: os robôs de cada time, na ordem do projeto (igual em todos os celulares). Os primeiros dão
        // lugar às pessoas da sala desse time (os outros jogadores entram no lugar deles; a vaga do jogador
        // local some); os que sobram continuam robôs, comandados por quem criou a sala.
        json ro = json::parse(M.roster, nullptr, false);
        std::vector<json> remote[2];
        int total[2] = {0, 0};
        if (ro.is_array())
            for (const json& h : ro) {
                if (!h.is_object()) continue;
                int t = std::clamp(h.value("time", 0), 0, 1);
                ++total[t];
                if (h.value("id", "") != M.localId) remote[t].push_back(h);
            }
        int idx[2] = {0, 0};
        for (size_t k = 0; k < slots.size(); ++k) {
            Slot& sl = slots[k];
            if (sl.player || sl.team < 0) continue;
            int t = sl.team, i = idx[t]++;
            slotBot[k] = i;
            if (i < total[t] && i < int(remote[t].size())) slotRemote[k] = remote[t][i];
            else sl.team = -1;  // online é só gente: as vagas sem jogador ficam vazias (sem robôs)
        }
    } else
    for (auto& s : slots) if (s.player && perTeam[s.team] > 5) {
        for (auto& b : slots) if (!b.player && b.team == s.team) { b.team = -1; --perTeam[s.team]; break; }
    }
    int pc = 0;
    for (auto& s : slots) {
        GameObject* g = e->scene.get(s.h);
        if (!g) continue;
        // o do projeto fica escondido; em jogo entra uma cópia (apagada no Stop)
        int ci = charIndexOfRoot(e, s.h);
        if (ci >= 0) if (GameObject* w = e->scene.get(e->characters[ci].weapon)) { setSubtreeActive(*w, false); e->game.hidden.push_back(e->characters[ci].weapon); }
        setSubtreeActive(*g, false);
        e->game.hidden.push_back(s.h);
        if (g->rigidBody) { e->physics.setEnabled(*g->rigidBody, false); e->game.disabledBodies.push_back(g->rigidBody); }
        if (s.team < 0 || M.modo == 1) continue;  // Ressurgência: os lutadores são criados depois (brStart)
        Fighter f;
        f.original = s.h;
        f.team = s.team;
        f.lado = s.team;
        f.bot = !s.player;
        json r = recipeOf(e, *g);
        f.name = f.bot ? std::string(g->name) : "Você";
        f.skin = clampSkin(f.team, r.value("roupa", 0));
        // robôs: a arma do projeto se for do lado (pistola, escopeta e sniper saíram: viram o fuzil do lado)
        static const char* botGuns[2][5] = {{"m4a1", "m4colt", "m4a1", "m4colt", "m4a1"}, {"ak47", "ak74", "ak74_tatica", "ak47", "ak74"}};
        std::string arma = r.value("arma", "");
        f.weapon = knownGun(arma) ? gunForTeam(arma, f.team) : botGuns[f.team][M.f.size() % 5];
        f.primary = f.weapon;
        f.secondary = "tt33";  // robôs também levam a pistola na coxa
        if (!f.bot) {  // escolhas do jogo: roupa, arma principal e a segunda (troca rápida; "" = só uma)
            if (M.playerSkin >= 0) f.skin = clampSkin(f.team, M.playerSkin);
            if (!M.playerPrimary.empty()) f.weapon = gunForTeam(M.playerPrimary, f.team);
            f.primary = f.weapon;
            f.secondary = knownGun(M.playerSecondary) ? gunForTeam(M.playerSecondary, f.team) : "";
            f.slot = 0;
            fillAmmo(f);
        }
        if (M.online) {
            const size_t k = size_t(&s - slots.data());
            if (s.player) f.netId = M.localId;
            else if (slotRemote[k].is_object()) {  // outra pessoa da sala
                const json& h = slotRemote[k];
                f.remote = true;
                f.netId = h.value("id", "");
                f.name = h.value("nome", "Jogador");
                f.skin = clampSkin(f.team, h.value("skin", 0));
                f.weapon = gunForTeam(h.value("arma", ""), f.team);
                f.primary = f.weapon;
                f.secondary = knownGun(h.value("arma2", "")) ? gunForTeam(h.value("arma2", ""), f.team) : "";
                if (auto sk = h.find("skins"); sk != h.end() && sk->is_object())
                    for (auto it = sk->begin(); it != sk->end(); ++it)
                        if (it->is_string() && knownGun(it.key())) f.gunSkins[it.key()] = it->get<std::string>();
            } else {
                f.netId = "b" + std::to_string(f.team) + "_" + std::to_string(slotBot[k]);
                f.remote = !M.host;  // os robôs são de quem criou a sala
            }
            f.netWeapon = f.weapon;
        }
        glm::vec3 p = g->transform.position;
        if (s.player && M.playerTeam >= 0) p = groundAt(e, M.spawns[s.team][0] + glm::vec3(1.5f, 0.f, 1.5f));
        glm::vec3 fw = g->transform.rotation * glm::vec3(0, 0, 1);
        M.f.push_back(f);
        spawnFighterBody(e, M.f.back(), p, std::atan2(fw.x, fw.z));
        M.f.back().lastPos = p;
        if (M.f.back().remote)  // comandado pela rede: o corpo só segue as posições que chegam
            if (GameObject* rg = e->scene.get(M.f.back().root); rg && rg->rigidBody) e->physics.setKinematic(*rg->rigidBody, true);
        ++pc;
    }
    if (mapIs(e, "favela")) {
        std::vector<glm::vec3> bases;
        for (int t = 0; t < 2; ++t) { bases.push_back(M.base[t]); for (const glm::vec3& sp : M.spawns[t]) bases.push_back(sp); }
        favelaNavGraph(e, M.nav, M.navAdj, bases);
    }
    if (M.modo == 1) { brStart(e); return; }
    e->game.playerHp = e->game.playerMax = 100.f;
    char buf[160];
    int pf = -1;
    for (size_t i = 0; i < M.f.size(); ++i) if (!M.f[i].bot) pf = int(i);
    std::snprintf(buf, sizeof(buf), "%s x %s — %s", kTeamName[0], kTeamName[1],
                  pf >= 0 ? (std::string("você é da ") + kTeamName[M.f[pf].team]).c_str() : "só robôs");
    e->game.message = buf;
    (void)pc;
}

bool teamDamage(Engine* e, GameObject* root, float dmg);

// Tiro de um lutador de outro celular: só o efeito (clarão, som, traçante até onde bateria); o dano vem pela rede
void remoteShotFx(Engine* e, Fighter& f, CharacterInfo& c, GameObject& g) {
    glm::vec3 fw = g.transform.rotation * glm::vec3(0, 0, 1);
    glm::vec3 dir = glm::normalize(fw * std::cos(f.netPitch) + glm::vec3(0, std::sin(f.netPitch), 0));
    glm::vec3 from = g.transform.position + glm::vec3(0, c.height * (c.crouch ? 0.55f : 0.85f), 0);
    glm::vec3 muzzle = from + dir * 0.6f;
    if (GameObject* w = e->scene.get(c.weapon)) muzzle = glm::vec3(w->transform.world() * glm::vec4(c.muzzle, 1.f));
    glm::vec3 hit; GameObject* obj = nullptr; float frac = 1.f;
    btRigidBody* self = g.rigidBody ? g.rigidBody->body : nullptr;
    bool any = e->physics.raycastIgnoring(from, from + dir * 200.f, self, hit, obj, frac);
    glm::vec3 end = any ? hit : from + dir * 200.f;
    muzzleFx(e, c.weapon, muzzle, glm::normalize(end - muzzle));
    bulletFx(e, c.weapon, muzzle, end);
    if (any) impactFx(e, c.weapon, hit, dir);
    playSound(e, c.shotSound.empty() ? "tiro_m4" : c.shotSound.c_str(), muzzle, 1.f);
    c.fireAnim = 0.3f;
    c.aimTimer = 1.f;
}

// Lutador de outro celular: vai suavemente até a última posição recebida, vira para onde ele olha, anima pela
// velocidade, troca de arma junto e mostra os tiros. Cai pelo evento de abate e volta quando chega "vivo" de novo.
void remoteUpdate(Engine* e, Fighter& f, float dt) {
    GameObject* g = e->scene.get(f.root);
    int ci = charIndexOfRoot(e, f.root);
    if (!g || ci < 0 || f.left) return;
    CharacterInfo& c = e->characters[ci];
    c.fireAnim = std::max(0.f, c.fireAnim - dt);
    c.aimTimer = std::max(0.f, c.aimTimer - dt);
    if (!f.netHas) return;
    if (f.dead) {
        if (!f.netAlive) return;
        f.dead = false; f.hp = 100.f; f.deadTime = 0.f;  // renasceu lá
        c.dying = false;
        g->transform.setPosition(f.netPos);
        if (g->rigidBody) { e->physics.setEnabled(*g->rigidBody, true); e->physics.setKinematic(*g->rigidBody, true); }
        if (c.anim && c.clipMap[CC_Idle] >= 0) { c.anim->setClip(c.clipMap[CC_Idle]); c.anim->loop = true; c.anim->speed = 1.f; }
    } else if (!f.netAlive) {  // caiu lá: espera o evento de abate; se ele não chegar em 1,5 s, cai assim mesmo
        if ((f.netMorto += dt) > 1.5f) {
            TeamMatch& M = matchOf(e);
            for (size_t k = 0; k < M.f.size(); ++k)
                if (&M.f[k] == &f) { killFighter(e, int(k), -1); break; }
        }
        return;
    }
    f.netMorto = 0.f;
    // onde ele deve estar agora: a última posição + a velocidade desde que ela chegou (até 0,25 s à frente)
    f.netAge += dt;
    glm::vec3 want = f.netPos + f.netVel * std::min(f.netAge, 0.25f);
    glm::vec3 cur = g->transform.position;
    glm::vec3 next = glm::length(want - cur) > 4.f ? want : glm::mix(cur, want, 1.f - std::exp(-18.f * dt));
    glm::vec3 v = dt > 1e-4f ? (next - cur) / dt : glm::vec3(0.f);
    g->transform.setPosition(next);
    glm::quat q = glm::angleAxis(f.netYaw, glm::vec3(0, 1, 0));
    g->transform.setRotation(glm::normalize(glm::slerp(g->transform.rotation, q, 1.f - std::exp(-16.f * dt))));
    if (g->rigidBody) e->physics.syncFromTransform(*g->rigidBody);
    c.crouch = f.netCrouch;
    c.aimHold = f.netAim;
    c.viewPitch = f.netPitch;
    if (f.netWeapon != f.weapon && knownGun(f.netWeapon)) {
        f.weapon = f.netWeapon;
        if (f.weapon != f.primary && f.weapon != f.secondary) (f.weapon == "tt33" ? f.secondary : f.primary) = f.weapon;  // comprou outra
        equipGun(e, f);
    }
    if (f.netReload) c.reloadTimer = std::max(c.reloadTimer, 0.2f);
    else c.reloadTimer = std::max(0.f, c.reloadTimer - dt);
    c.airborne = f.netAir;
    animateCharacter(c, g->transform.rotation, v, !f.netAir, f.netAir);
    if (f.netShots > c.shots) {
        f.shotAgo = 0.f;  // atirou: aparece no minimapa dos adversários
        int n = std::min(3, f.netShots - c.shots);
        c.shots = f.netShots;
        for (int k = 0; k < n; ++k) remoteShotFx(e, f, c, *g);
    }
}

// Granada: voa com gravidade, quica no chão e nas paredes e explode em 3 s. Até 4 m abate; de 4 a 8 m, 100 a 20 de
// dano. Parede entre a granada e a pessoa protege. Sem fogo amigo.
void updateNades(Engine* e, float dt) {
    TeamMatch& M = matchOf(e);
    for (size_t i = 0; i < M.nades.size();) {
        TeamMatch::Nade& n = M.nades[i];
        n.t -= dt;
        n.vel.y -= 9.81f * dt;
        glm::vec3 next = n.pos + n.vel * dt;
        glm::vec3 hit; GameObject* obj = nullptr; float frac = 1.f;
        bool rest = glm::length(n.vel) < 0.3f;
        if (!rest && e->physics.raycast(n.pos, next + glm::normalize(n.vel) * 0.04f, hit, obj)) {
            GameObject* r = rootOf(e, obj);
            bool person = false;
            for (const Fighter& f : M.f) if (e->scene.get(f.root) == r) person = true;
            if (!person) {
                glm::vec3 d = next - n.pos;
                bool floor = n.vel.y < 0.f && std::abs(d.y) > 0.5f * glm::length(glm::vec2(d.x, d.z));
                next = hit - glm::normalize(n.vel) * 0.05f;
                if (floor) { n.vel.y = -n.vel.y * 0.3f; n.vel.x *= 0.55f; n.vel.z *= 0.55f; if (std::abs(n.vel.y) < 0.8f) n.vel.y = 0.f; }
                else { n.vel.x = -n.vel.x * 0.35f; n.vel.z = -n.vel.z * 0.35f; }
            }
        }
        (void)frac;
        if (!rest) n.pos = next;
        if (GameObject* g = e->scene.get(n.h)) g->transform.setPosition(n.pos);
        if (n.t > 0.f) { ++i; continue; }
        // explosão
        spawnEffect(e, FX_Explosion, 0, n.pos + glm::vec3(0, 0.2f, 0));
        playSound(e, "explosao", n.pos, 1.f);
        if (n.visual) {  // granada de outro celular: o dano é calculado lá
            if (e->scene.get(n.h)) deleteObject(e, n.h);
            M.nades.erase(M.nades.begin() + i);
            continue;
        }
        g_killWeapon = "granada";
        for (size_t k = 0; k < M.f.size(); ++k) {
            Fighter& v = M.f[k];
            if (v.dead || (n.owner >= 0 && n.owner < int(M.f.size()) && M.f[n.owner].team == v.team)) continue;
            GameObject* vg = e->scene.get(v.root);
            if (!vg) continue;
            glm::vec3 chest = chestOf(e, v, 0.6f);
            float d = glm::length(chest - n.pos);
            if (d > 8.f) continue;
            glm::vec3 from = n.pos + glm::vec3(0, 0.25f, 0);
            if (e->physics.raycast(from, chest, hit, obj) && rootOf(e, obj) != vg) continue;  // parede protege
            float dmg = d <= 4.f ? 250.f : 100.f - 80.f * (d - 4.f) / 4.f;
            M.attacker = n.owner;
            teamDamage(e, vg, dmg);
            M.attacker = -1;
        }
        g_killWeapon.clear();
        if (e->scene.get(n.h)) deleteObject(e, n.h);
        M.nades.erase(M.nades.begin() + i);
    }
}

void teamUpdate(Engine* e, float dt) {
    TeamMatch& M = matchOf(e);
    GameState& G = e->game;
    if (M.modo == 1) { brUpdate(e, dt); return; }
    updateNades(e, dt);
    for (auto& fd : M.feed) fd.age += dt;
    for (auto& f : M.f) f.shotAgo += dt;
    M.lastDamage += dt;
    updateDrops(e, dt);
    if (G.state == 0) regenerate(e, dt);
    for (size_t i = 0; i < M.f.size(); ++i) {
        Fighter& f = M.f[i];
        if (f.remote) { remoteUpdate(e, f, dt); continue; }
        if (f.dead) {
            if ((f.deadTime += dt) > 5.f) respawn(e, f);
            continue;
        }
        if (!f.bot) {
            if (G.playerHp <= 0.f) killFighter(e, int(i), M.attacker);
            else if (M.piloto && G.state == 0) botUpdate(e, int(i), dt);  // piloto automático (vídeo)
            continue;
        }
        if (G.state == 0) botUpdate(e, int(i), dt);
    }
    if (G.state != 0) return;
    int need = e->rules.quantidade > 0 ? e->rules.quantidade : 50;
    int winner = M.score[0] >= need ? 0 : M.score[1] >= need ? 1 : -2;
    if (winner < -1 && e->rules.tempo > 0.f && G.time >= e->rules.tempo)
        winner = M.score[0] > M.score[1] ? 0 : M.score[1] > M.score[0] ? 1 : -1;
    if (winner >= -1) {
        int pt = -1;
        for (auto& f : M.f) if (!f.bot) pt = f.team;
        G.state = winner >= 0 && (pt < 0 || winner == pt) ? 1 : 2;
        G.message = winner < 0 ? "Empate!" : std::string("Vitória: ") + kTeamName[winner] + "!";
    }
}

// Dano em lutador (sem fogo amigo); devolve false se o atingido não é lutador
bool teamDamage(Engine* e, GameObject* root, float dmg) {
    TeamMatch& M = matchOf(e);
    const int att = M.attacker >= 0 ? M.attacker : playerFighter(e);  // tiro sem robô atirando: foi o jogador
    for (size_t i = 0; i < M.f.size(); ++i) {
        Fighter& f = M.f[i];
        if (e->scene.get(f.root) != root) continue;
        if (f.dead) return true;
        if (att >= 0 && att < int(M.f.size()) && M.f[att].team == f.team) return true;  // sem fogo amigo
        if (f.remote) {  // online: o dano vai para o celular de quem levou o tiro (lá ele perde vida e cai)
            if (M.online && att >= 0 && att < int(M.f.size()) && !M.f[att].remote)
                M.netOut.push_back(json{{"t", "dano"}, {"alvo", f.netId}, {"dano", std::round(dmg * 10.f) / 10.f}, {"de", M.f[att].netId},
                                        {"arma", g_killWeapon.empty() ? gunOf(M.f[att].weapon).name : g_killWeapon}}.dump());
            return true;
        }
        if (!f.bot) {
            hurtPlayer(e, dmg);
            M.lastDamage = 0.f;
            if (e->game.playerHp <= 0.f) killFighter(e, int(i), att);
        } else {
            f.hp -= dmg;
            f.hurtAgo = 0.f;
            if (att >= 0) { f.target = att; f.think = 0.3f; }  // vira para quem atirou
            if (f.hp <= 0.f) killFighter(e, int(i), att);
        }
        return true;
    }
    return false;
}

void teamStop(Engine* e) {
    TeamMatch& M = matchOf(e);
    int chosen = M.playerTeam, skin = M.playerSkin;
    std::string primary = M.playerPrimary, secondary = M.playerSecondary;
    auto gunSkins = M.gunSkins;
    bool online = M.online, host = M.host;
    std::string localId = M.localId, roster = M.roster;
    const int modo = M.modo;
    M = TeamMatch{};
    M.playerTeam = chosen; M.playerSkin = skin; M.playerPrimary = primary; M.playerSecondary = secondary;
    M.gunSkins = gunSkins;
    M.online = online; M.host = host; M.localId = localId; M.roster = roster;
    M.modo = modo;
}
}  // namespace


// Armas da partida carregadas junto com o mapa (a partida começa e as compras trocam a arma na hora)
void preloadMatchWeapons(Engine* e) {
    for (const GunDef& g : kGuns) warmWeaponModel(e, std::string("kit:armas/") + g.key, std::string(g.key) == "tt33" ? int(WK_Pistol) : int(WK_Rifle));
    // as minhas skins com modelo próprio também (a partida não trava na hora de aparecer com elas)
    for (const auto& [gun, skin] : matchOf(e).gunSkins) {
        const std::string f = gunModelFile(e, gun, skin, e->nivel >= 1);
        if (f != std::string("kit:armas/") + gun) warmWeaponModel(e, f, gun == "tt33" ? int(WK_Pistol) : int(WK_Rifle));
    }
}

std::string gunModelFile(Engine* e, const std::string& gun, const std::string& skin, bool alta) {
    if (!skin.empty() && skin.find_first_of("/.\\") == std::string::npos) {
        auto existe = [&](const std::string& nome) {
            FILE* f = std::fopen(kitPath(e, "armas/" + nome + ".glb").c_str(), "rb");
            if (f) std::fclose(f);
            return f != nullptr;
        };
        const std::string nome = gun + "_" + skin;
        if (alta && existe(nome + "_alta")) return "kit:armas/" + nome + "_alta";  // (o site não leva a versão alta)
        if (existe(nome)) return "kit:armas/" + nome;
    }
    return "kit:armas/" + gun;
}

// Acabamento das skins de fuzil: Dourada = metal polido; as outras = pintura (pouco metal).
// As da TT-33 foram pintadas no molde da arma e mantêm o metal/brilho de cada peça (-1).
void gunSkinFinish(const std::string& gun, const std::string& skin, float& metal, float& rough) {
    if (gun == "tt33") { metal = rough = -1.f; return; }
    if (skin == "dourada") { metal = 1.f; rough = 0.28f; return; }
    metal = 0.15f; rough = 0.55f;
}

}  // namespace eng

namespace eng {
int playerFighter(Engine* e) {
    for (size_t i = 0; i < matchOf(e).f.size(); ++i) if (!matchOf(e).f[i].bot) return int(i);
    return -1;
}
void teamChoose(Engine* e, int team) { matchOf(e).playerTeam = team < 0 ? -1 : std::clamp(team, 0, 1); }
void teamMode(Engine* e, int modo) { matchOf(e).modo = modo == 1 ? 1 : 0; }

namespace {
int nearShop(Engine* e) {
    if (matchOf(e).modo != 0) return -1;  // Ressurgência: sem base nem loja
    int pf = playerFighter(e);
    if (pf < 0 || matchOf(e).f[pf].dead) return -1;
    GameObject* pg = e->scene.get(matchOf(e).f[pf].root);
    if (!pg) return -1;
    for (size_t i = 0; i < matchOf(e).shops.size(); ++i) {
        GameObject* s = e->scene.get(matchOf(e).shops[i].obj);
        if (!s) continue;
        glm::vec3 front = s->transform.position + s->transform.rotation * glm::vec3(0, 0, 1.6f);
        glm::vec3 d = front - pg->transform.position;
        if (glm::length(glm::vec2(d.x, d.z)) < 2.6f && std::abs(d.y) < 2.5f) return int(i);
    }
    // na base do próprio time (onde nasce) a loja de armas do time também abre, sem ir até a barraca
    const int team = matchOf(e).f[pf].team;
    for (const glm::vec3& sp : matchOf(e).spawns[team]) {
        glm::vec3 d = sp - pg->transform.position;
        if (glm::length(glm::vec2(d.x, d.z)) > 12.f || std::abs(d.y) > 4.f) continue;
        for (size_t i = 0; i < matchOf(e).shops.size(); ++i)
            if (matchOf(e).shops[i].team == team && matchOf(e).shops[i].kind == 0) return int(i);
    }
    return -1;
}
}  // namespace

std::string shopLabel(Engine* e) {
    if (!matchOf(e).active) return "";
    int s = nearShop(e);
    if (s < 0) return "";
    const ShopRT& sh = matchOf(e).shops[s];
    if (sh.team != matchOf(e).f[playerFighter(e)].team) return std::string("🔒 Loja só para ") + (sh.team ? "bandidos" : "policiais");
    return sh.kind ? (sh.team ? "👕 Loja de roupas" : "👮 Loja de fardas") : "🛒 Loja de armas";
}
bool shopInteract(Engine* e) {
    int s = nearShop(e);
    if (s < 0 || matchOf(e).shops[s].team != matchOf(e).f[playerFighter(e)].team) return false;
    matchOf(e).openShop = s;
    return true;
}
void shopClose(Engine* e) { matchOf(e).openShop = -1; }

std::string shopJson(Engine* e) {
    TeamMatch& M = matchOf(e);
    int pf = playerFighter(e);
    if (M.openShop < 0 || M.openShop >= int(M.shops.size()) || pf < 0) return "";
    const ShopRT& sh = M.shops[M.openShop];
    Fighter& f = M.f[pf];
    json j;
    j["loja"] = sh.kind ? (sh.team ? "Loja de roupas da favela" : "Loja de fardas") : (sh.team ? "Armas da favela" : "Loja de armas");
    j["dinheiro"] = f.money;
    json items = json::array();
    auto owned = [&](int id) { return std::find(f.owned.begin(), f.owned.end(), id) != f.owned.end(); };
    if (sh.kind == 0) {
        for (int g = 0; g < kGunCount; ++g) {
            const GunDef& d = kGuns[g];
            // qualquer arma nos dois lados (a loja da polícia também vende AK; a dos bandidos, M4)
            const bool tem = d.price == 0 || owned(g) || f.primary == d.key || f.secondary == d.key;
            // ficha da arma para a loja: dano, cadência e precisão (0 a 1) e tiros para abater (vida 100)
            items.push_back({{"id", g}, {"arma", d.key}, {"nome", d.name}, {"preco", d.price}, {"tem", tem}, {"usando", f.weapon == d.key},
                             {"dano", int(d.dmg)}, {"cabeca", int(std::round(d.dmg * 2.5f))}, {"pente", d.mag},
                             {"tiros", int(std::ceil(100.f / d.dmg))}, {"cadencia_s", std::round(10.f / d.interval) / 10.f},
                             {"barra_dano", std::min(1.f, d.dmg / 40.f)}, {"barra_cadencia", std::min(1.f, (1.f / d.interval) / 10.f)},
                             {"barra_precisao", std::clamp(1.f - d.spread / 0.05f, 0.1f, 1.f)}});
        }
        items.push_back({{"id", 200}, {"arma", "granada"}, {"nome", "Granada"}, {"preco", 300}, {"tem", false}, {"usando", false},
                         {"qtd", f.grenades}, {"max", 2}});
    } else {
        for (int k = 0; k < kSkinCount[sh.team]; ++k)
            items.push_back({{"id", 100 + k}, {"nome", kSkins[sh.team][k].name}, {"preco", kSkins[sh.team][k].price},
                             {"tem", kSkins[sh.team][k].price == 0 || owned(100 + k)}, {"usando", f.skin == k}});
    }
    j["itens"] = items;
    return j.dump();
}

std::string shopBuy(Engine* e, int item) {
    TeamMatch& M = matchOf(e);
    int pf = playerFighter(e);
    if (pf < 0 || M.openShop < 0) return "Loja fechada";
    Fighter& f = M.f[pf];
    const ShopRT& sh = M.shops[M.openShop];
    if (item == 200) {  // granada: $300, até 2
        if (sh.kind != 0) return "Item inválido";
        if (f.grenades >= 2) return "Você já tem 2 granadas";
        if (f.money < 300) return "Dinheiro insuficiente: faltam $" + std::to_string(300 - f.money);
        f.money -= 300;
        ++f.grenades;
        return "Comprou: Granada  (💰 $" + std::to_string(f.money) + ")";
    }
    bool isSkin = item >= 100;
    int price;
    std::string name;
    if (isSkin) {
        int k = item - 100;
        if (sh.kind != 1 || k < 0 || k >= kSkinCount[sh.team]) return "Item inválido";
        price = kSkins[sh.team][k].price; name = kSkins[sh.team][k].name;
    } else {
        if (sh.kind != 0 || item < 0 || item >= kGunCount) return "Item inválido";
        price = kGuns[item].price; name = kGuns[item].name;
    }
    bool has = price == 0 || std::find(f.owned.begin(), f.owned.end(), item) != f.owned.end();
    if (!has) {
        if (f.money < price) return "Dinheiro insuficiente: faltam $" + std::to_string(price - f.money);
        f.money -= price;
        f.owned.push_back(item);
    }
    if (isSkin) {
        f.skin = item - 100;
        GameObject* g = e->scene.get(f.root);
        glm::vec3 p = g ? g->transform.position : M.base[f.team];
        glm::vec3 fw = g ? g->transform.rotation * glm::vec3(0, 0, 1) : glm::vec3(0, 0, 1);
        spawnFighterBody(e, f, p + glm::vec3(0, 0.05f, 0), std::atan2(fw.x, fw.z));
    } else {
        // fuzil vai para a arma principal, pistola para a segunda; já vem com pente e reserva cheios
        const GunDef& g = kGuns[item];
        const int slot = std::string(g.key) == "tt33" && !f.primary.empty() && f.primary != g.key ? 1 : 0;
        (slot ? f.secondary : f.primary) = g.key;
        f.slot = slot;
        f.weapon = g.key;
        f.mag[slot] = g.mag; f.reserve[slot] = g.reserve;
        equipGun(e, f);
    }
    return (has ? "Usando: " : "Comprou: ") + name + "  (💰 $" + std::to_string(f.money) + ")";
}

void teamLoadout(Engine* e, int team, int skin, const std::string& primary, const std::string& secondary) {
    TeamMatch& M = matchOf(e);
    M.playerTeam = team < 0 ? -1 : std::clamp(team, 0, 1);
    M.playerSkin = skin;
    M.playerPrimary = primary;
    M.playerSecondary = secondary;
}

// Aparência (JSON) do boneco de uma roupa: usada pelas fotos das roupas nas telas do jogo
std::string teamSkinLook(int team, int skin) {
    json a = fighterLook(team, skin, "retrato");
    a["pele"] = "#C68C69";
    return a.dump();
}

void teamGunSkin(Engine* e, const std::string& gun, const std::string& skin) { matchOf(e).gunSkins[gun] = skin; }

bool teamSwapWeapon(Engine* e) {
    TeamMatch& M = matchOf(e);
    int pf = playerFighter(e);
    if (!M.active || pf < 0 || M.f[pf].dead || e->game.state != 0) return false;
    Fighter& f = M.f[pf];
    if (f.secondary.empty() || f.primary == f.secondary) return false;
    int ci = charIndexOfRoot(e, f.root);
    if (ci < 0) return false;
    f.mag[f.slot] = e->characters[ci].ammo;
    f.reserve[f.slot] = e->characters[ci].reserve;
    f.slot = 1 - f.slot;
    f.weapon = f.slot ? f.secondary : f.primary;
    equipGun(e, f);
    return true;
}

bool teamThrowGrenade(Engine* e) {
    TeamMatch& M = matchOf(e);
    int pf = playerFighter(e);
    if (!M.active || pf < 0 || M.f[pf].dead || e->game.state != 0 || M.f[pf].grenades <= 0) return false;
    Fighter& f = M.f[pf];
    glm::vec3 view = glm::normalize(e->camera.rotation * glm::vec3(0, 0, -1));
    glm::vec3 from = e->camera.position + view * 0.45f - glm::vec3(0, 0.1f, 0);
    if (GameObject* g = e->scene.get(f.root); g && !e->firstPerson) from = g->transform.position + glm::vec3(0, 1.5f, 0) + view * 0.5f;
    // não nasce dentro da parede
    glm::vec3 hit; GameObject* obj = nullptr;
    if (GameObject* g = e->scene.get(f.root))
        if (e->physics.raycast(g->transform.position + glm::vec3(0, 1.4f, 0), from, hit, obj) && rootOf(e, obj) != g) from = hit - view * 0.15f;
    json spec = {{"tipo", "forma"}, {"forma", "esfera"}, {"fisica", false}, {"paleta", "metal"}, {"escala", {0.085f, 0.11f, 0.085f}},
                 {"pos", {from.x, from.y, from.z}}, {"nome", "Granada"}};
    Scene::Handle selected = e->selected;
    int id = createFromSpecJson(e, spec.dump(), nullptr);
    e->selected = selected;
    TeamMatch::Nade n;
    if (GameObject* g = objectById(e, id)) { n.h = Scene::handleOf(*g); e->game.spawned.push_back(n.h); }
    n.pos = from;
    n.vel = view * 15.f + glm::vec3(0, 3.5f, 0);
    n.owner = pf;
    M.nades.push_back(n);
    --f.grenades;
    if (M.online)
        M.netOut.push_back(json{{"t", "granada"}, {"de", f.netId}, {"p", {n.pos.x, n.pos.y, n.pos.z}}, {"v", {n.vel.x, n.vel.y, n.vel.z}}}.dump());
    e->lastError.clear();
    return true;
}

// Tudo o que o HUD do jogo mostra, numa leitura por quadro
std::string hudJson(Engine* e) {
    const TeamMatch& M = matchOf(e);
    const GameState& G = e->game;
    json j;
    j["ativo"] = M.active;
    if (!M.active) return j.dump();
    int pf = playerFighter(e);
    int need = e->rules.quantidade > 0 ? e->rules.quantidade : 50;
    j["estado"] = G.state;
    j["placar"] = {M.score[0], M.score[1]};
    j["meta"] = need;
    j["tempo"] = e->rules.tempo > 0.f ? std::max(0, int(std::ceil(e->rules.tempo - G.time))) : -1;
    j["vida"] = int(std::ceil(G.playerHp));
    j["vidaMax"] = int(G.playerMax);
    j["dano"] = M.lastDamage < 0.35f;
    j["msg"] = G.message;
    if (pf >= 0) {
        const Fighter& f = M.f[pf];
        j["time"] = f.team;
        j["abates"] = f.kills;
        j["mortes"] = f.deaths;
        j["morto"] = f.dead;
        j["renasce"] = f.dead ? (f.fora ? -1 : std::max(0, int(std::ceil((M.modo == 1 ? 10.f : 5.f) - f.deadTime)))) : 0;
        j["granadas"] = f.grenades;
        j["dinheiro"] = f.money;
        j["slot"] = f.slot;
        json slots = json::array();
        int ci = charIndexOfRoot(e, f.root);
        for (int s = 0; s < (f.secondary.empty() || f.secondary == f.primary ? 1 : 2); ++s) {
            const GunDef& g = gunOf(s ? f.secondary : f.primary);
            int mag = f.mag[s], res = f.reserve[s];
            if (s == f.slot && ci >= 0) { mag = e->characters[ci].ammo; res = e->characters[ci].reserve; }
            slots.push_back({{"arma", g.key}, {"nome", g.name}, {"balas", mag}, {"reserva", res}, {"pente", g.mag}});
        }
        j["armas"] = slots;
        if (ci >= 0) {
            const CharacterInfo& c = e->characters[ci];
            j["recarregando"] = c.reloadTimer > 0.f;
            j["mirando"] = c.adsW > 0.5f;
            j["luneta"] = c.scope;
            j["zoom"] = e->scopeZoom;
        }
    }
    json feed = json::array();
    for (const auto& fd : M.feed)
        if (fd.age < 7.f)
            feed.push_back({{"a", fd.killer}, {"at", fd.kt}, {"v", fd.victim}, {"vt", fd.vt}, {"arma", fd.weapon}, {"voce", fd.you}, {"skin", fd.skin}});
    j["abatidos"] = feed;
    json players = json::array();
    for (const Fighter& f : M.f) players.push_back({{"nome", f.name}, {"time", f.team}, {"abates", f.kills}, {"mortes", f.deaths}, {"voce", !f.bot}});
    j["jogadores"] = players;
    // minimapa: companheiros (e inimigos que atiraram agora há pouco) em metros, com a frente do jogador para cima
    json mm = json::array();
    if (pf >= 0)
        if (GameObject* pg = e->scene.get(M.f[pf].root)) {
            glm::vec3 fw = glm::normalize(e->editorCam.forward() * glm::vec3(1, 0, 1) + glm::vec3(0, 0, 1e-4f));
            glm::vec3 rt(-fw.z, 0.f, fw.x);
            for (size_t i = 0; i < M.f.size(); ++i) {
                const Fighter& f = M.f[i];
                if (int(i) == pf || f.dead || (f.team != M.f[pf].team && f.shotAgo > 2.75f)) continue;  // inimigo: só ~2,75 s depois de atirar
                GameObject* g = e->scene.get(f.root);
                if (!g) continue;
                glm::vec3 d = g->transform.position - pg->transform.position;
                float x = glm::dot(d, rt), y = glm::dot(d, fw);
                if (x * x + y * y > 45.f * 45.f) continue;
                mm.push_back({std::round(x * 10.f) / 10.f, std::round(y * 10.f) / 10.f, f.team == M.f[pf].team ? 0 : 1});
            }
        }
    j["mapa"] = mm;
    j["modo"] = M.modo;
    if (M.modo == 1 && pf >= 0) {  // Ressurgência: times vivos, zona e o meu time
        static const float kEspera[] = {60.f, 50.f, 45.f, 40.f, 35.f, 30.f}, kFecha[] = {45.f, 40.f, 35.f, 30.f, 25.f, 30.f};
        const auto& z = M.zona;
        const Fighter& eu = M.f[pf];
        json br;
        int timesVivos = 0, vivos = 0;
        for (int t = 0; t < M.times; ++t) if (t < int(M.colocacao.size()) && !M.colocacao[t]) ++timesVivos;
        for (const Fighter& f : M.f) if (!f.fora) ++vivos;
        br["times"] = timesVivos;
        br["totalTimes"] = M.times;
        br["jogadores"] = vivos;
        br["fase"] = std::min(z.fase + 1, 6);
        br["fechando"] = z.fechando;
        br["tempo"] = z.fase < 6 ? std::max(0, int(std::ceil((z.fechando ? kFecha[z.fase] : kEspera[z.fase]) - z.t))) : 0;
        br["renascer"] = z.fase < 4;
        br["colocacao"] = eu.team < int(M.colocacao.size()) ? M.colocacao[eu.team] : 0;
        br["meuTime"] = eu.team + 1;
        json time = json::array();
        for (const Fighter& f : M.f)
            if (f.team == eu.team)
                time.push_back({{"nome", f.name}, {"voce", !f.bot}, {"vivo", !f.dead}, {"fora", f.fora},
                                {"renasce", f.dead && !f.fora ? std::max(0, int(std::ceil(10.f - f.deadTime))) : 0},
                                {"vida", f.bot ? int(std::ceil(std::max(0.f, f.hp))) : int(std::ceil(G.playerHp))}});
        br["time"] = time;
        // zona no minimapa (metros, com a frente do jogador para cima) e fora dela?
        if (GameObject* pg = e->scene.get(eu.root)) {
            glm::vec3 fw = glm::normalize(e->editorCam.forward() * glm::vec3(1, 0, 1) + glm::vec3(0, 0, 1e-4f));
            glm::vec3 rt(-fw.z, 0.f, fw.x);
            auto rel = [&](glm::vec2 c) {
                glm::vec3 d = glm::vec3(c.x, 0.f, c.y) - pg->transform.position * glm::vec3(1, 0, 1);
                return json::array({std::round(glm::dot(d, rt) * 10.f) / 10.f, std::round(glm::dot(d, fw) * 10.f) / 10.f});
            };
            br["zona"] = {{"c", rel(z.c)}, {"r", std::round(z.r * 10.f) / 10.f}};
            if (z.rPara > 0.f || z.fase < 6) br["proxima"] = {{"c", rel(z.para)}, {"r", std::round(z.rPara * 10.f) / 10.f}};
            const float d = glm::length(glm::vec2(pg->transform.position.x, pg->transform.position.z) - z.c);
            br["fora"] = !eu.dead && d > z.r;
            br["distancia"] = int(std::max(0.f, d - z.r));
        }
        j["br"] = br;
    }
    j["interagir"] = interactLabel(e);
    j["acerto"] = e->hitMarker > 0.f;
    j["cabeca"] = e->hitHead;
    return j.dump();
}

// ---------------- partida online ----------------
namespace {
int fighterByNet(const TeamMatch& M, const std::string& id) {
    if (id.empty()) return -1;
    for (size_t i = 0; i < M.f.size(); ++i) if (M.f[i].netId == id) return int(i);
    return -1;
}
double r2(float x) { return std::round(double(x) * 100.0) / 100.0; }
}  // namespace

void netRoster(Engine* e, bool host, const std::string& localId, const std::string& rosterJson) {
    TeamMatch& M = matchOf(e);
    M.online = !rosterJson.empty() && !localId.empty();
    M.host = host;
    M.localId = M.online ? localId : "";
    M.roster = M.online ? rosterJson : "";
    M.netOut.clear();
}

std::string netCollect(Engine* e) {
    const TeamMatch& M = matchOf(e);
    json out = json::object();
    if (!M.active || !M.online) return out.dump();
    for (const Fighter& f : M.f) {
        if (f.remote || f.netId.empty()) continue;
        GameObject* g = e->scene.get(f.root);
        int ci = charIndexOfRoot(e, f.root);
        if (!g || ci < 0) continue;
        const CharacterInfo& c = e->characters[ci];
        glm::vec3 p = g->transform.position, fw = g->transform.rotation * glm::vec3(0, 0, 1);
        glm::vec3 vel(0.f);
        if (g->rigidBody && g->rigidBody->body) { btVector3 lv = g->rigidBody->body->getLinearVelocity(); vel = {lv.x(), lv.y(), lv.z()}; }
        out[f.netId] = {{"p", {r2(p.x), r2(p.y), r2(p.z)}}, {"y", r2(std::atan2(fw.x, fw.z))}, {"pi", r2(c.viewPitch)},
                        {"c", c.crouch}, {"a", c.aimHold || c.aimTimer > 0.f}, {"w", f.weapon}, {"v", !f.dead}, {"n", c.shots},
                        {"j", c.airborne}, {"r", c.reloadTimer > 0.f}, {"f", f.fora}, {"vel", {r2(vel.x), r2(vel.y), r2(vel.z)}}};
    }
    return out.dump();
}

void netApply(Engine* e, const std::string& states) {
    TeamMatch& M = matchOf(e);
    if (!M.active || !M.online) return;
    json j = json::parse(states, nullptr, false);
    if (!j.is_object()) return;
    for (auto it = j.begin(); it != j.end(); ++it) {
        int i = fighterByNet(M, it.key());
        if (i < 0 || !M.f[i].remote || !it->is_object()) continue;
        Fighter& f = M.f[i];
        const json& st = *it;
        if (auto p = st.find("p"); p != st.end() && p->is_array() && p->size() == 3)
            f.netPos = {(*p)[0].get<float>(), (*p)[1].get<float>(), (*p)[2].get<float>()};
        f.netYaw = st.value("y", f.netYaw);
        f.netPitch = st.value("pi", f.netPitch);
        f.netCrouch = st.value("c", false);
        f.netAim = st.value("a", false);
        f.netAlive = st.value("v", true);
        f.netWeapon = st.value("w", f.netWeapon);
        f.netAir = st.value("j", false);
        f.netReload = st.value("r", false);
        f.netFora = f.netFora || st.value("f", false);  // fora é para sempre
        if (auto v = st.find("vel"); v != st.end() && v->is_array() && v->size() == 3)
            f.netVel = {(*v)[0].get<float>(), (*v)[1].get<float>(), (*v)[2].get<float>()};
        f.netAge = 0.f;
        int shots = st.value("n", f.netShots);
        if (!f.netHas) {  // primeira vez: não repete os tiros antigos
            int ci = charIndexOfRoot(e, f.root);
            if (ci >= 0) e->characters[ci].shots = shots;
            if (GameObject* g = e->scene.get(f.root)) g->transform.setPosition(f.netPos);
        }
        f.netShots = shots;
        f.netHas = true;
    }
}

std::string netTakeEvents(Engine* e) {
    TeamMatch& M = matchOf(e);
    std::string out = "[";
    for (size_t i = 0; i < M.netOut.size(); ++i) out += (i ? "," : "") + M.netOut[i];
    M.netOut.clear();
    return out + "]";
}

void netEvent(Engine* e, const std::string& evJson) {
    TeamMatch& M = matchOf(e);
    if (!M.active || !M.online) return;
    json ev = json::parse(evJson, nullptr, false);
    if (!ev.is_object()) return;
    const std::string t = ev.value("t", "");
    if (t == "dano") {  // levei tiro (eu ou um robô meu)
        int vi = fighterByNet(M, ev.value("alvo", "")), ai = fighterByNet(M, ev.value("de", ""));
        if (vi < 0 || M.f[vi].remote || M.f[vi].dead || e->game.state != 0) return;
        Fighter& v = M.f[vi];
        if (ai >= 0 && M.f[ai].team == v.team) return;
        float dmg = ev.value("dano", 0.f);
        g_killWeapon = ev.value("arma", "");
        if (!v.bot) {
            hurtPlayer(e, dmg);
            M.lastDamage = 0.f;
            if (e->game.playerHp <= 0.f) killFighter(e, vi, ai);
        } else {
            v.hp -= dmg;
            if (ai >= 0) { v.target = ai; v.think = 0.3f; }
            if (v.hp <= 0.f) killFighter(e, vi, ai);
        }
        g_killWeapon.clear();
    } else if (t == "abate") {  // alguém de outro celular caiu
        int vi = fighterByNet(M, ev.value("v", "")), ai = fighterByNet(M, ev.value("a", ""));
        if (vi < 0 || !M.f[vi].remote || M.f[vi].dead) return;
        g_killWeapon = ev.value("arma", "");
        killFighter(e, vi, ai);
        M.f[vi].netAlive = false;
        g_killWeapon.clear();
    } else if (t == "granada") {  // granada de outro celular: só o efeito
        int oi = fighterByNet(M, ev.value("de", ""));
        if (oi < 0 || !M.f[oi].remote) return;
        auto vec = [&](const char* k) {
            const json& a = ev[k];
            return a.is_array() && a.size() == 3 ? glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>()) : glm::vec3(0.f);
        };
        TeamMatch::Nade n;
        n.pos = vec("p"); n.vel = vec("v"); n.owner = oi; n.visual = true;
        json spec = {{"tipo", "forma"}, {"forma", "esfera"}, {"fisica", false}, {"paleta", "metal"}, {"escala", {0.085f, 0.11f, 0.085f}},
                     {"pos", {n.pos.x, n.pos.y, n.pos.z}}, {"nome", "Granada"}};
        Scene::Handle selected = e->selected;
        int id = createFromSpecJson(e, spec.dump(), nullptr);
        e->selected = selected;
        if (GameObject* g = objectById(e, id)) { n.h = Scene::handleOf(*g); e->game.spawned.push_back(n.h); }
        M.nades.push_back(n);
        e->lastError.clear();
    } else if (t == "saiu") {  // a pessoa saiu da sala: o corpo some
        int i = fighterByNet(M, ev.value("id", ""));
        if (i < 0 || !M.f[i].remote) return;
        Fighter& f = M.f[i];
        f.left = true; f.dead = true;
        if (M.modo == 1) f.fora = true;  // Ressurgência: saiu, está fora
        if (GameObject* g = e->scene.get(f.root)) {
            setSubtreeActive(*g, false);
            if (g->rigidBody) e->physics.setEnabled(*g->rigidBody, false);
        }
        int ci = charIndexOfRoot(e, f.root);
        if (ci >= 0) if (GameObject* w = e->scene.get(e->characters[ci].weapon)) setSubtreeActive(*w, false);
    }
}
}  // namespace eng


// ---------------- o jogo ligado ao motor ----------------
namespace eng {
namespace { Scene::Handle g_origPlayer; bool g_hadMatch = false; }

TeamMatch& matchOf(Engine* e) {
    static TeamMatch vazio;
    Partida* p = e ? dynamic_cast<Partida*>(e->module.get()) : nullptr;
    return p ? p->M : vazio;
}
void jogoInstalar(Engine* e) {
    registrarMapaFavela();  // o mapa do jogo entra no cadastro de mapas do motor
    if (e && !dynamic_cast<Partida*>(e->module.get())) e->module = std::make_unique<Partida>();
}

void Partida::onPlayStart(Engine* e) { teamStart(e); }  // Polícia x Bandidos (objetivo "times")
void Partida::onPlayStop(Engine* e) {
    g_origPlayer = {};
    for (const Fighter& f : M.f) if (!f.bot) g_origPlayer = f.original;
    g_hadMatch = M.active;
    teamStop(e);
}
void Partida::onPlayEnded(Engine* e) {
    if (!g_hadMatch) return;  // o jogador volta a ser o personagem do projeto
    int ci = -1;
    for (size_t i = 0; i < e->characters.size(); ++i) if (sameHandle(e->characters[i].root, g_origPlayer)) ci = int(i);
    if (ci >= 0) e->player = ci;
    else if (e->player >= int(e->characters.size())) e->player = -1;
    g_hadMatch = false;
}
bool Partida::onUpdate(Engine* e, float dt) {
    if (!M.active) return false;
    teamUpdate(e, dt);
    return true;
}
bool Partida::onDamage(Engine* e, GameObject* root, float dmg) { return M.active && teamDamage(e, root, dmg); }
bool Partida::onInteract(Engine* e) { return M.active && shopInteract(e); }
std::string Partida::interactLabel(Engine* e) { return M.active ? shopLabel(e) : std::string(); }
void Partida::onProjectLoaded(Engine* e) {
    // mapa da partida: as armas já carregam aqui (atrás da tela de carregamento), não no começo da partida
    if (mapIs(e, "favela")) preloadMatchWeapons(e);
}
std::string Partida::statusText(Engine* e) {
    if (e->rules.objetivo != "times" || !M.active || M.modo != 0) return "";
    int pf = -1;
    for (size_t i = 0; i < M.f.size(); ++i) if (!M.f[i].bot) pf = int(i);
    int need = e->rules.quantidade > 0 ? e->rules.quantidade : 50;
    char buf[200];
    if (pf >= 0)
        std::snprintf(buf, sizeof(buf), "🚓 Polícia %d x %d Bandidos 🔫  (até %d)   💰 $%d   abates %d / mortes %d", M.score[0], M.score[1], need,
                      M.f[pf].money, M.f[pf].kills, M.f[pf].deaths);
    else std::snprintf(buf, sizeof(buf), "🚓 Polícia %d x %d Bandidos 🔫  (até %d)", M.score[0], M.score[1], need);
    return buf;
}
}  // namespace eng
