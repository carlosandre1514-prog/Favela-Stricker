#include "assets/CacheDisco.h"
#include "platform/engine_c_api.h"
#include "procgen/Mocap.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <memory>
#include <vector>
#include <stb_image.h>
#include "assets/TextureManager.h"
#include "engine/Engine.h"

// O tipo opaco da API C é o estado do motor (eng::Engine, em motor/engine/Engine.h)
struct Engine : eng::Engine {};

using eng::AABB;
using eng::Animator;
using eng::AnimationClip;
using eng::ColliderType;
using eng::GameObject;
using eng::Mat;
using eng::RigidBody;
using eng::RigidBodyPool;
using eng::Scene;
using eng::TouchPoint;

// Cria pelo formato de projeto (receita salva no objeto) e seleciona
static int createSpec(Engine* e, const std::string& spec, bool select = true) {
    std::string err;
    int id = eng::createFromSpecJson(e, spec, &err);
    if (id >= 0 && select) eng::selectHandle(e, e->objects[id]);
    return id;
}
static std::string fmt3(const char* key, glm::vec3 v) {
    char b[96];
    std::snprintf(b, sizeof(b), "\"%s\":[%.3f,%.3f,%.3f]", key, v.x, v.y, v.z);
    return b;
}
// Edição de material/cor/textura vai para a receita (raiz) ou para "pecas" (peça selecionada)
static void recordEdit(Engine* e, const char* key, const std::string& value) {
    GameObject* sel = e->scene.get(e->selected);
    GameObject* root = eng::rootOf(e, sel);
    if (!root) return;
    if (root == sel) {
        if (!std::strcmp(key, "material")) { eng::recipeSet(e, *root, "cor", "null"); eng::recipeSet(e, *root, "textura", "null"); eng::recipeSet(e, *root, "pecas", "null"); }
        if (!std::strcmp(key, "textura")) eng::recipeSet(e, *root, "cor", "null");
        eng::recipeSet(e, *root, key, value);
        return;
    }
    int idx = 0, found = -1;
    Scene::forEachInSubtree(*root, [&](GameObject& g) { if (&g == sel) found = idx; ++idx; });
    if (found < 0) return;
    eng::recipePartSet(e, *root, found, key, value);
}

// ---------- ciclo de vida ----------
namespace eng {
// Nível gráfico escolhido no jogo. Vale ao abrir o motor (texturas e modelos são preparados no carregamento).
//   texturas dos personagens: 256 · 512 · 512 · original   armas: 256 · 512 · 512 · original
//   triângulos dos personagens: 25% · 35% · 50% · 100% (armas: no mínimo 50%)
void applyQualityLevel(Engine* e, int nivel) {
    nivel = std::clamp(nivel, 0, 3);
    static const int kQuality[4] = {0, 0, 1, 2};
    static const int kCharMax[4] = {256, 512, 512, 0};
    static const int kTexMax[4] = {256, 512, 512, 0};  // armas: eram 212 MB de textura na Intermediária (1024)
    static const float kTris[4] = {0.25f, 0.35f, 0.5f, 1.f};
    static const float kAniso[4] = {1.f, 2.f, 4.f, 16.f};
    e->quality = kQuality[nivel];
    e->nivel = nivel;
    e->lowTextures = nivel <= 1;
    TextureManager::instance().charMax = kCharMax[nivel];
    TextureManager::instance().modelMax = kTexMax[nivel];
    e->renderer.simplify = kTris[nivel];
    TextureManager::instance().anisotropia = kAniso[nivel];
    e->renderer.shadowCascadesMax = nivel >= 3 ? 3 : 2;
    e->renderer.leve = nivel == 0;  // Leve: shader sem sombra e sem mapa de normais (mais leve por pixel)  // Intermediária: 2 faixas de sombra (cada faixa desenha o mapa de novo)
}
}  // namespace eng

Engine* engine_create(const char* shader_dir) {
    eng::CacheDisco::instance().configurar(std::string(shader_dir ? shader_dir : "") + "/cache");  // armas já prontas guardadas
    eng::TextureManager::instance().forgetAll();  // contexto GL novo: ids antigos não valem
    Engine* e = new Engine();
    // qualidade.txt: 0 = Leve, 1 = Padrão, 2 = Intermediária, 3 = Ultra (gravada pelo app antes de abrir)
    int nivel = 2;
    if (FILE* f = std::fopen((std::string(shader_dir) + "/qualidade.txt").c_str(), "r")) {
        if (std::fscanf(f, "%d", &nivel) != 1) nivel = 2;
        std::fclose(f);
    }
    // Ultra: sombra com o dobro de resolução e reflexo do ambiente mais nítido
    e->renderer.shadowSize = nivel >= 3 ? 2048 : eng::kShadowMapSize;
    e->renderer.envSize = nivel >= 3 ? 256 : eng::Renderer::kEnvSize;
    if (!e->renderer.init(shader_dir)) { delete e; return nullptr; }
    if (e->particles.init(e->renderer)) e->renderer.particles = &e->particles;
    e->objects.reserve(Scene::kMaxObjects);
    eng::applyQualityLevel(e, nivel);
    eng::loadMocapLibrary(std::string(shader_dir) + "/anim");  // animações capturadas (antes de criar personagens)
    eng::ensureBasics(e);
    return e;
}

void engine_destroy(Engine* e) {
    if (!e) return;
    eng::clearWorld(e);
    e->scene.forEach([&](GameObject& g, Scene::Handle) { eng::releaseComponents(e, g); });
    eng::TextureManager::instance().releaseAll();
    e->particles.shutdown();
    e->renderer.shutdown();
    delete e;
}

void engine_resize(Engine* e, int w, int h) {
    if (!e || w <= 0 || h <= 0) return;
    e->width = w; e->height = h;
    e->camera.aspect = float(w) / float(h);
    e->renderer.resize(w, h);
}

void engine_set_target_fbo(Engine* e, uint32_t fbo) { if (e) e->renderer.targetFbo = fbo; }

void engine_frame(Engine* e, float dt) {
    if (!e) return;
    auto t0 = std::chrono::steady_clock::now();
    dt = std::clamp(dt, 0.f, 0.1f);
    e->renderer.time = std::fmod(e->renderer.time + dt, 3600.f);

    if (e->playing) {
        eng::updatePlayer(e, dt);
        eng::gameUpdate(e, dt);
        e->physics.step(dt);
        eng::weatherUpdate(e, dt);
    } else {
        for (auto& t : e->tracers) t.life = 0.f;
    }
    for (Animator* a : e->animators) a->update(dt, e->playing || e->previewAnim);
    eng::updateDoors(e, dt);
    auto tw = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) { return std::chrono::duration<float, std::milli>(b - a).count(); };
    e->profWorld = ms(tw, std::chrono::steady_clock::now());
    eng::updateEffects(e, dt);

    // Câmera: near/far e distância de sombra acompanham o zoom e o tamanho do mapa
    float d = e->editorCam.distance;
    e->camera.nearZ = std::clamp(d * 0.01f, 0.05f, 5.f);
    if (e->nearOverride > 0.f) e->camera.nearZ = e->nearOverride;
    e->camera.farZ = std::max(e->mapKind >= 0 ? 900.f : 200.f, d * 40.f);
    // jogando: vê até uma distância (o resto nem é enviado para a placa de vídeo); leve vê menos
    // por nível (Leve, Padrão, Intermediária, Ultra): visão, sombras e quando os detalhes somem
    static const float kViewDist[4] = {120.f, 170.f, 240.f, 900.f};
    static const float kFogMin[4] = {0.0085f, 0.006f, 0.003f, 0.f};  // neblina mínima jogando: esconde onde a visão acaba
    static const float kShadowMax[4] = {0.f, 0.f, 35.f, 200.f};  // 0 = sem sombra (o mapa desenhado de novo custa caro)
    static const float kLod[4] = {0.4f, 0.5f, 0.65f, 1.35f};  // Ultra: detalhes (plantas, objetos) aparecem mais longe
    const int nv = std::clamp(e->nivel, 0, 3);
    const float viewDist = !e->playing ? 1e9f : kViewDist[nv];
    if (e->playing) e->camera.farZ = std::min(e->camera.farZ, viewDist * 1.2f);
    // sombras: só perto (de longe ninguém nota e custam o mapa inteiro desenhado de novo 3 vezes)
    e->renderer.shadowDistance = std::clamp(d * 3.5f, e->playing ? 40.f : 12.f, std::max(40.f, kShadowMax[nv]));
    eng::DirectionalLight sun = e->sun;
    sun.castShadows = sun.castShadows && kShadowMax[nv] > 0.f && (e->playing || d < 350.f);
    const float lodScale = kLod[nv];  // detalhes somem mais cedo
    e->editorCam.apply(e->camera);
    e->scene.updateTransforms();
    eng::updateAttachments(e);
    eng::updateFirstPersonEye(e, dt);
    eng::updateSupportHands(e, dt);
    eng::applyFirstPerson(e);
    eng::atualizarAncoras(e, dt);  // fogo do disparo preso à boca da arma já posicionada
    eng::atualizarEnergia(e, dt);  // skin de energia: núcleo, cristais e raios na arma já posicionada
    eng::updateCuts(e);
    eng::updateLamps(e);

    auto ts = std::chrono::steady_clock::now();
    e->renderer.beginFrame(e->camera, sun);
    const glm::vec3 camPos = e->camera.position;
    e->scene.forEach([&](GameObject& go, Scene::Handle) {
        const eng::MeshRenderer* mr = go.meshRenderer;
        if (!mr) return;
        const glm::mat4* palette;
        glm::mat4 m = eng::renderMatrix(e, go, &palette);
        if (mr->lodNear > 0.f || mr->lodFar < 1e29f || (e->playing && !palette)) {  // LOD por distância ao centro do mesh
            const eng::Mesh* mesh = e->renderer.mesh(mr->mesh);
            if (!mesh) return;
            glm::vec3 c = glm::vec3(m * glm::vec4((mesh->bounds.min + mesh->bounds.max) * 0.5f, 1.f));
            glm::vec3 d = c - camPos;
            float d2 = glm::dot(d, d);
            float lf = mr->lodFar * lodScale, ln = mr->lodNear * lodScale;  // perto e longe na mesma escala (sem buraco)
            if (d2 < ln * ln || d2 >= lf * lf) return;
            if (e->playing) {  // além da distância de visão: nem desenha
                glm::vec3 half = (mesh->bounds.max - mesh->bounds.min) * 0.5f;
                float rad = glm::length(half) * std::max({glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))});
                float dist = std::sqrt(d2) - rad;
                if (dist > viewDist) return;
            }
        }
        if (palette && mr->distLonge > 0.f) {  // personagem longe: a versão leve do corpo (mesmos ossos)
            const eng::Mesh* mesh = e->renderer.mesh(mr->mesh);
            const glm::vec3 c = glm::vec3(m[3]) + (mesh ? glm::vec3(0.f, (mesh->bounds.max.y + mesh->bounds.min.y) * 0.5f, 0.f) : glm::vec3(0.f));
            const float dl = mr->distLonge * lodScale;
            if (glm::dot(c - camPos, c - camPos) > dl * dl) { e->renderer.submit(go, m, palette, mr->meshLonge); return; }
        }
        e->renderer.submit(go, m, palette);
    });

    e->profSubmit = ms(ts, std::chrono::steady_clock::now());
    glm::mat4 vp = e->camera.viewProj();
    eng::drawEffectsOverlay(e, vp, false);

    // luzes: as da cena + as mais relevantes dos efeitos (fogo, magia, explosões)
    eng::PointLight lights[eng::kMaxPointLights];
    int nLights = eng::gatherLights(e, lights, e->nivel <= 0 ? 3 : e->nivel == 1 ? 4 : eng::kMaxPointLights);  // leve: menos luzes no shader
    auto tr = std::chrono::steady_clock::now();
    eng::Environment env = e->env;
    float fog = e->renderer.fogDensity;
    eng::weatherLook(e, env, sun, fog);  // chuva: céu fechado, neblina, clarão dos raios
    if (e->playing) fog = std::max(fog, kFogMin[nv]);  // neblina mínima do nível: esconde onde a visão acaba
    const float keepFog = e->renderer.fogDensity;
    e->renderer.fogDensity = fog;
    e->renderer.render(e->camera, sun, env, lights, nLights);
    e->renderer.fogDensity = keepFog;
    e->profRender = ms(tr, std::chrono::steady_clock::now());

    // estatísticas (janela de 0,5 s)
    float cpu = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    e->statTime += dt;
    e->cpuAccum += cpu;
    ++e->statFrames;
    if (e->statTime >= 0.5f) {
        e->fps = e->statFrames / e->statTime;
        e->frameMs = e->statTime * 1000.f / e->statFrames;
        e->cpuMs = e->cpuAccum / e->statFrames;
        e->statTime = 0.f; e->cpuAccum = 0.f; e->statFrames = 0;
    }
}

// ---------- Play / Stop ----------
void engine_set_play(Engine* e, int playing) {
    if (!e) return;
    bool p = playing != 0;
    if (p == e->playing) return;
    if (p) {
        e->snapshot.clear();
        e->scene.forEach([&](GameObject& g, Scene::Handle h) {
            if (g.id >= 0) e->snapshot.push_back({h, g.transform.position, g.transform.scale, g.transform.rotation});
        });
        e->animSnap.clear();
        for (Animator* a : e->animators) e->animSnap.push_back({a->time, a->clipIndex});
        e->bodies.forEach([&](RigidBody& rb, RigidBodyPool::Handle) { e->physics.syncFromTransform(rb); });
        e->physics.resetAccumulator();
        e->savedCam = {e->editorCam.target, e->editorCam.yaw, e->editorCam.pitch, e->editorCam.distance};
        e->moveInput = glm::vec2(0.f);
        e->runInput = e->jumpRequest = e->fireRequest = e->crouchInput = false;
        e->playing = true;  // a lógica de jogo só cria/danifica com o Play ligado
        e->playStartHour = e->timeOfDay;
        e->scopeZoom = 0;  // luneta começa no zoom pequeno
        eng::weatherStart(e);
        e->sounds.clear();
        eng::gameStart(e);
        if (e->player >= 0) {  // câmera atrás do jogador
            if (GameObject* pg = e->scene.get(e->characters[e->player].root)) {
                glm::vec3 f = pg->transform.rotation * glm::vec3(0, 0, 1);
                e->editorCam.yaw = std::atan2(-f.x, -f.z);
                e->editorCam.pitch = 0.3f;
                e->editorCam.distance = 5.5f;
            }
        }
    } else {
        eng::gameStop(e);
        if (e->dayCycleMin > 0.f) eng::setTimeOfDay(e, e->playStartHour);  // volta à hora de antes do Play
        e->rain = e->rainTarget = e->lightning = 0.f;
        e->sounds.clear();
        for (auto& s : e->snapshot)
            if (GameObject* g = e->scene.get(s.h)) {
                g->transform.setPosition(s.p); g->transform.setRotation(s.r); g->transform.setScale(s.s);
            }
        for (size_t i = 0; i < e->animators.size() && i < e->animSnap.size(); ++i) {
            e->animators[i]->clipIndex = e->animSnap[i].clip;
            e->animators[i]->time = e->animSnap[i].time;
            e->animators[i]->speed = 1.f;
            e->animators[i]->loop = true;
        }
        for (auto& c : e->characters) { c.dying = false; c.ammo = 30; c.reloadTimer = 0.f; c.fireAnim = 0.f; }
        e->bodies.forEach([&](RigidBody& rb, RigidBodyPool::Handle) { e->physics.syncFromTransform(rb); });
        e->editorCam.target = e->savedCam.target; e->editorCam.yaw = e->savedCam.yaw;
        e->editorCam.pitch = e->savedCam.pitch; e->editorCam.distance = e->savedCam.distance;
    }
    e->playing = p;
}
int engine_is_playing(Engine* e) { return e && e->playing; }

// Sons pedidos desde a última chamada: linhas "nome\tvolume\tpan" (volume 0..1 já com a distância; pan -1..1)
const char* engine_poll_sounds(Engine* e) {
    if (!e) return "";
    e->soundText.clear();
    const glm::vec3 cam = e->camera.position;
    const glm::mat4 v = e->camera.view();
    const glm::vec3 right(v[0][0], v[1][0], v[2][0]);
    char b[160];
    for (const auto& s : e->sounds) {
        float vol = s.gain, pan = 0.f;
        std::string name = s.name;
        if (s.positional) {
            glm::vec3 d = s.pos - cam;
            float dist = glm::length(d);
            vol = s.gain / (1.f + std::pow(dist / 14.f, 1.4f));
            if (dist > 1e-3f) pan = std::clamp(glm::dot(d / dist, right), -1.f, 1.f) * std::min(1.f, dist / 6.f);
            if (dist > 45.f) name += "_longe";  // de longe: o app usa a versão abafada (se tiver)
        }
        if (vol < 0.01f) continue;
        std::snprintf(b, sizeof(b), "%s\t%.3f\t%.2f\n", name.c_str(), std::min(vol, 1.f), pan);
        e->soundText += b;
    }
    e->sounds.clear();
    return e->soundText.c_str();
}
// Estado do tempo para o app: chuva 0..1, noite 0..1, hora
void engine_get_weather(Engine* e, float* out3) {
    if (!e || !out3) return;
    out3[0] = e->playing ? e->rain : 0.f;
    out3[1] = e->night;
    out3[2] = e->timeOfDay;
}

// ---------- cenas ----------
void engine_new_scene(Engine* e) {
    if (!e) return;
    eng::clearScene(e);
    createSpec(e, R"({"tipo":"chao","tamanho":40})", false);
    e->editorCam.target = glm::vec3(0.f, 0.5f, 0.f);
    e->editorCam.distance = 8.f;
}

const char* engine_last_error(Engine* e) { return e ? e->lastError.c_str() : ""; }

int engine_get_transform(Engine* e, int id, float* p, float* r, float* s) {
    GameObject* g = eng::objectById(e, id);
    if (!g) return 0;
    glm::vec3 eu = g->transform.eulerDegrees();
    for (int i = 0; i < 3; ++i) { p[i] = g->transform.position[i]; r[i] = eu[i]; s[i] = g->transform.scale[i]; }
    return 1;
}
void engine_set_transform(Engine* e, int id, const float* p, const float* r, const float* s) {
    GameObject* g = eng::objectById(e, id);
    if (!g) return;
    g->transform.setPosition({p[0], p[1], p[2]});
    g->transform.setEulerDegrees({r[0], r[1], r[2]});
    g->transform.setScale({s[0], s[1], s[2]});
    if (g->rigidBody) e->physics.syncFromTransform(*g->rigidBody);
    ++e->revision;
}

// ---------- visual ----------
void engine_set_exposure(Engine* e, float x) { if (e) { e->env.exposure = std::clamp(x, 0.05f, 8.f); ++e->revision; } }
float engine_get_exposure(Engine* e) { return e ? e->env.exposure : 1.f; }
void engine_set_sun_intensity(Engine* e, float x) { if (e) { e->sun.intensity = std::clamp(x, 0.f, 20.f); ++e->revision; } }
float engine_get_sun_intensity(Engine* e) { return e ? e->sun.intensity : 0.f; }

// ---------- animação ----------
static void animatorsOfSelection(Engine* e, std::vector<Animator*>& out) {
    out.clear();
    if (GameObject* sel = eng::rootOf(e, e->scene.get(e->selected)))  // toque numa peça vale pelo modelo inteiro
        Scene::forEachInSubtree(*sel, [&](GameObject& g) {
            if (g.animator && std::find(out.begin(), out.end(), g.animator) == out.end()) out.push_back(g.animator);
        });
    if (out.empty()) out = e->animators;
}

void engine_player_aim(Engine* e, int on) { if (e) e->aimInput = on != 0; }
int engine_scope_zoom(Engine* e, int delta) { if (!e) return 0; e->scopeZoom = std::clamp(e->scopeZoom + delta, 0, 2); return e->scopeZoom; }
const char* engine_interact_label(Engine* e) { if (!e) return ""; e->toolResult = eng::interactLabel(e); return e->toolResult.c_str(); }
int engine_interact(Engine* e) { return e && eng::interact(e); }

// nível 0..3: visão, sombras e luzes mudam na hora; texturas e modelos na próxima abertura do jogo
void engine_set_quality(Engine* e, int q) { if (e) eng::applyQualityLevel(e, q); }

void engine_get_profile(Engine* e, float* o) {
    if (!e || !o) return;
    o[0] = e->profWorld; o[1] = e->profSubmit; o[2] = e->profRender;
}

void engine_get_stats(Engine* e, float* o) {
    if (!e || !o) return;
    const eng::FrameStats& s = e->renderer.stats();
    o[0] = e->fps; o[1] = e->frameMs; o[2] = e->cpuMs;
    o[3] = float(s.drawCalls); o[4] = float(s.triangles); o[5] = float(e->scene.count());
}

// memória: [0] texturas MB, [1] malhas MB, [2] malhas, [3] triângulos carregados (mil)
void engine_memory_stats(Engine* e, float* o) {
    if (!e || !o) return;
    o[0] = float(double(eng::TextureManager::instance().bytes) / 1048576.0);
    o[1] = float(double(e->renderer.meshBytes()) / 1048576.0);
    o[2] = float(e->renderer.meshTotal());
    o[3] = 0.f;
}

int engine_create_random_character(Engine* e, uint32_t seed, int controllable) {
    if (!e) return -1;
    char spec[120];
    std::snprintf(spec, sizeof(spec), R"({"tipo":"personagem","semente":%u,"jogador":%s})", seed ? seed : 1u, controllable ? "true" : "false");
    return createSpec(e, spec);
}

template <class F> static void forSelectedRenderers(Engine* e, F&& f) {
    GameObject* sel = e->scene.get(e->selected);
    if (!sel) return;
    Scene::forEachInSubtree(*sel, [&](GameObject& g) { if (g.meshRenderer) f(*g.meshRenderer); });
}

void engine_set_near(Engine* e, float n) { if (e) e->nearOverride = std::max(0.f, n); }
int engine_player_id(Engine* e) {
    if (!e || e->player < 0 || e->player >= int(e->characters.size())) return -1;
    eng::GameObject* g = e->scene.get(e->characters[e->player].root);
    return g ? g->id : -1;
}
int engine_has_player(Engine* e) {
    return e && e->player >= 0 && e->player < int(e->characters.size()) && e->scene.get(e->characters[e->player].root);
}
void engine_player_input(Engine* e, float x, float y, int run) {
    if (!e) return;
    e->moveInput = glm::vec2(std::clamp(x, -1.f, 1.f), std::clamp(y, -1.f, 1.f));
    e->runInput = run != 0;
}
void engine_player_jump(Engine* e) { if (e) e->jumpRequest = true; }
void engine_player_crouch(Engine* e, int on) { if (e) e->crouchInput = on != 0; }
void engine_player_fire(Engine* e) { if (e) e->fireRequest = true; }
int engine_player_weapon(Engine* e) { return engine_has_player(e) ? e->characters[e->player].weaponKind : -1; }
void engine_set_first_person(Engine* e, int on) {
    if (!e) return;
    e->firstPerson = on != 0;
    e->fpEyeValid = false;
    e->editorCam.turnSpeed = on ? 0.0042f : 0.008f;
    e->editorCam.minPitch = on ? -1.25f : -1.5f;
    e->editorCam.maxPitch = on ? 1.25f : 1.5f;
    e->editorCam.zoomEnabled = !on;
    if (on) e->editorCam.pitch = 0.05f;
}
void engine_player_reload(Engine* e) { if (e) e->reloadRequest = true; }
// apaga o objeto (e o que estiver preso nele)
void engine_remove_object(Engine* e, int id) { if (e) if (GameObject* g = eng::objectById(e, id)) eng::deleteObject(e, Scene::handleOf(*g)); }
void engine_look(Engine* e, float dx, float dy) {
    if (!e) return;
    eng::EditorCamera& c = e->editorCam;
    c.yaw -= dx * c.turnSpeed;
    c.pitch = std::clamp(c.pitch + dy * c.turnSpeed, c.minPitch, c.maxPitch);
}

// ---------- efeitos ----------

void engine_set_camera(Engine* e, float tx, float ty, float tz, float yaw, float pitch, float distance) {
    if (!e) return;
    e->editorCam.target = {tx, ty, tz};
    e->editorCam.yaw = yaw;
    e->editorCam.pitch = std::clamp(pitch, -1.5f, 1.5f);
    e->editorCam.distance = std::clamp(distance, 0.3f, 5000.f);
}

int engine_import_project(Engine* e, const char* json) {
    if (!e || !json) return 0;
    std::string err;
    bool ok = eng::importProject(e, json, &err);
    if (!ok) e->lastError = err;
    return ok ? 1 : 0;
}
int engine_add_spec(Engine* e, const char* json) { return e && json ? createSpec(e, json) : -1; }
// Teste: um tiro da arma (objeto id) na direção do cano, com o efeito da skin se ela tiver
void engine_test_shot(Engine* e, int id) {
    if (!e) return;
    eng::GameObject* w = eng::objectById(e, id);
    if (!w) return;
    eng::AABB b;
    if (!eng::subtreeBounds(e, *w, b)) return;
    const glm::mat4& m = w->transform.world();
    const glm::vec3 dir = glm::normalize(glm::vec3(m[2]));
    const glm::vec3 c = (b.min + b.max) * 0.5f;
    const float meio = glm::dot(b.max - b.min, glm::abs(dir)) * 0.5f;
    const glm::vec3 boca = c + dir * meio + glm::vec3(0.f, (b.max.y - b.min.y) * 0.25f, 0.f);
    eng::muzzleFx(e, eng::Scene::handleOf(*w), boca, dir);
    eng::bulletFx(e, eng::Scene::handleOf(*w), boca, boca + dir * 30.f);
}
void engine_set_time_of_day(Engine* e, float hour) { if (e) eng::setTimeOfDay(e, hour); }
float engine_get_time_of_day(Engine* e) { return e ? e->timeOfDay : 13.f; }

// ---------- jogo ----------
void engine_game_status(Engine* e, float* o) {
    if (!e || !o) return;
    const eng::GameState& G = e->game;
    o[0] = float(G.state); o[1] = G.playerHp; o[2] = G.playerMax; o[3] = float(G.score);
    o[4] = G.time; o[5] = e->rules.tempo; o[6] = G.hurtTimer; o[7] = e->rules.objetivo != "livre" ? 1.f : 0.f;
}
const char* engine_game_text(Engine* e, int which) {
    if (!e) return "";
    switch (which) {
        case 0: e->toolResult = eng::gameStatusText(e); break;
        case 1: e->toolResult = e->game.message; break;
        case 2: e->toolResult = e->rules.titulo; break;
        default: e->toolResult = e->rules.descricao; break;
    }
    return e->toolResult.c_str();
}
