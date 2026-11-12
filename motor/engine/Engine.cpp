#include "engine/Engine.h"
#include <algorithm>
#include <map>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "assets/MeshUtils.h"
#include "procgen/Simplify.h"
#include <stb_image.h>
#include "assets/TextureManager.h"
#include "scene/Humanoid.h"
#include <glm/gtx/matrix_decompose.hpp>

namespace eng {

// ---------------- utilidades de cena ----------------
GameObject* objectById(Engine* e, int id) {
    if (!e || id < 0 || id >= int(e->objects.size())) return nullptr;
    return e->scene.get(e->objects[id]);
}

int registerObject(Engine* e, Scene::Handle h) {
    GameObject* g = e->scene.get(h);
    if (!g) return -1;
    g->id = int(e->objects.size());
    e->objects.push_back(h);
    return g->id;
}

// Skinned usa o espaço do esqueleto (o transform do nó do mesh é ignorado, como manda o glTF)
glm::mat4 renderMatrix(const Engine* e, const GameObject& go, const glm::mat4** palette) {
    *palette = nullptr;
    if (go.animator && go.meshRenderer) {
        const Mesh* m = e->renderer.mesh(go.meshRenderer->mesh);
        if (m && m->skinned) {
            *palette = go.animator->palette;
            return go.animator->space ? go.animator->space->world() : glm::mat4(1.f);
        }
    }
    return go.transform.world();
}

bool subtreeBounds(Engine* e, GameObject& root, AABB& out) {
    bool any = false;
    out = {glm::vec3(1e30f), glm::vec3(-1e30f)};
    Scene::forEachInSubtree(root, [&](GameObject& g) {
        if (!g.meshRenderer) return;
        const Mesh* m = e->renderer.mesh(g.meshRenderer->mesh);
        if (!m) return;
        const glm::mat4* pal;
        AABB b = transformAABB(m->bounds, renderMatrix(e, g, &pal));
        out.min = glm::min(out.min, b.min);
        out.max = glm::max(out.max, b.max);
        any = true;
    });
    return any;
}

void releaseComponents(Engine* e, GameObject& g) {
    if (g.rigidBody) {
        RigidBody* rb = g.rigidBody;
        e->physics.detach(*rb);
        e->bodies.release(rb);
    }
    if (g.meshRenderer) { e->meshRenderers.release(g.meshRenderer); g.meshRenderer = nullptr; }
    if (g.emitter) { e->emitters.release(g.emitter); g.emitter = nullptr; }
}

void ensureBasics(Engine* e) {
    if (e->cube != kInvalidId) return;
    std::vector<Vertex> v; std::vector<uint32_t> idx;
    buildCube(v, idx);
    e->cube = e->renderer.createMesh(v.data(), uint32_t(v.size()), idx.data(), uint32_t(idx.size()), false);
    v.clear(); idx.clear(); buildSphere(v, idx);
    e->sphere = e->renderer.createMesh(v.data(), uint32_t(v.size()), idx.data(), uint32_t(idx.size()), false);
    v.clear(); idx.clear(); buildCapsule(v, idx, 2.f);
    e->capsule = e->renderer.createMesh(v.data(), uint32_t(v.size()), idx.data(), uint32_t(idx.size()), false);
    v.clear(); idx.clear(); buildRock(v, idx, e->rockHull);
    e->rock = e->renderer.createMesh(v.data(), uint32_t(v.size()), idx.data(), uint32_t(idx.size()), false);

    TextureManager& tx = TextureManager::instance();
    auto mat = [&](glm::vec4 albedo, float rough, float metal) {
        Material m;
        m.albedo = tx.white(); m.normal = tx.flatNormal(); m.orm = tx.white(); m.emissive = tx.white();
        m.albedoFactor = albedo; m.roughness = rough; m.metallic = metal;
        return e->renderer.createMaterial(m);
    };
    e->matFloor = mat({0.3f, 0.32f, 0.3f, 1.f}, 0.9f, 0.f);
    e->matPalette[0] = mat({0.9f, 0.35f, 0.1f, 1.f}, 0.4f, 0.f);    // laranja plástico
    e->matPalette[1] = mat({0.95f, 0.95f, 0.97f, 1.f}, 0.2f, 1.f);  // aço polido
    e->matPalette[2] = mat({1.f, 0.78f, 0.34f, 1.f}, 0.3f, 1.f);    // ouro
    e->matPalette[3] = mat({0.15f, 0.45f, 0.85f, 1.f}, 0.25f, 0.f); // azul brilhante
    e->matPalette[4] = mat({0.55f, 0.36f, 0.22f, 1.f}, 0.8f, 0.f);  // madeira
    e->matPalette[5] = mat({0.5f, 0.5f, 0.52f, 1.f}, 0.9f, 0.f);    // pedra

    // Biblioteca procedural (texturas geradas uma vez e reenviadas à GPU a cada cena); usa as imagens de shaders/materials
    setMaterialTextureDir((e->renderer.shaderDir() + "/materials").c_str());
    pregenerateTextures();  // todos os núcleos; da 2ª abertura em diante vem do cache
    // celular fraco: metade da resolução (1/4 da memória de vídeo)
    auto half = [](const std::vector<uint8_t>& src, int w, int h) {
        std::vector<uint8_t> out(size_t(w / 2) * (h / 2) * 4);
        for (int y = 0; y < h / 2; ++y)
            for (int x = 0; x < w / 2; ++x)
                for (int c = 0; c < 4; ++c) {
                    int a = src[((2 * y) * w + 2 * x) * 4 + c], b = src[((2 * y) * w + 2 * x + 1) * 4 + c];
                    int d = src[((2 * y + 1) * w + 2 * x) * 4 + c], f = src[((2 * y + 1) * w + 2 * x + 1) * 4 + c];
                    out[(size_t(y) * (w / 2) + x) * 4 + c] = uint8_t((a + b + d + f + 2) / 4);
                }
        return out;
    };
    for (int k = 0; k < int(Mat::Count); ++k) {
        const TexData& t0 = proceduralTexture(Mat(k));
        TexData small;
        const bool shrink = e->lowTextures && t0.w >= 256 && t0.h >= 256;
        if (shrink) {
            small.w = t0.w / 2; small.h = t0.h / 2;
            small.albedo = half(t0.albedo, t0.w, t0.h);
            small.orm = half(t0.orm, t0.w, t0.h);
            if (!t0.normal.empty()) small.normal = half(t0.normal, t0.w, t0.h);
        }
        const TexData& t = shrink ? small : t0;
        MatParams mp = matParams(Mat(k));
        Material m;
        m.albedo = tx.fromPixels(t.albedo.data(), t.w, t.h, 4, true);
        m.orm = tx.fromPixels(t.orm.data(), t.w, t.h, 4, false);
        m.normal = t.normal.empty() ? tx.flatNormal() : tx.fromPixels(t.normal.data(), t.w, t.h, 4, false);
        m.emissive = tx.white();
        m.roughness = mp.roughness; m.metallic = mp.metallic; m.mode = mp.mode; m.doubleSided = mp.doubleSided;
        if (Mat(k) == Mat::Water) {
            // água padrão: oceano animado (shader "ocean"); as ondas vêm de waternormals.jpg, junto dos shaders
            int w, h, comp;
            std::string path = e->renderer.shaderDir() + "/waternormals.jpg";
            if (unsigned char* px = stbi_load(path.c_str(), &w, &h, &comp, 4)) {
                m.normal = tx.fromPixels(px, w, h, 4, false);
                stbi_image_free(px);
            }
            m.mode = 2;
            m.albedoFactor = glm::vec4(0.01f, 0.07f, 0.12f, 1.f);  // cor da água funda (azul-esverdeado escuro)
            m.doubleSided = true;
        }
        if (Mat(k) == Mat::ChainLink) m.alphaCutoff = 0.5f;  // alambrado: só o arame aparece
        if (Mat(k) == Mat::Mural) m.alphaCutoff = 0.35f;     // pichação: só a tinta, a parede aparece atrás
        if (Mat(k) == Mat::Lamp) m.emissive_factor = glm::vec3(0.f);  // acende à noite (updateLamps)
        if (Mat(k) == Mat::Terrain) {
            // terreno padrão: camadas de textura reais (shaders/terrain); a ordem é a mesma do pbr.frag
            static const char* kLayers[] = {"grama", "campo", "floresta", "chao", "areia", "terra", "seixos"};
            if (e->nivel >= 1 && e->renderer.loadTerrainLayers(kLayers, 7)) { m.mode = 3; m.roughness = 0.95f; }  // leve: terreno simples
        }
        e->mats[k] = e->renderer.createMaterial(m);
    }
    // Leve e Padrão: a cópia das texturas na RAM sai (já estão na placa de vídeo; numa cena nova são refeitas)
    if (e->nivel <= 1) releaseProceduralTextures();
    e->tintCache.clear();
}

void selectHandle(Engine* e, Scene::Handle h) {
    GameObject* g = e->scene.get(h);
    e->selected = g ? h : Scene::Handle{};
}

Scene::Handle addMeshObject(Engine* e, const char* name, MeshId mesh, MaterialId mat, glm::vec3 pos, glm::vec3 scale,
                            glm::vec3 eulerDeg, bool reg) {
    Scene::Handle h = e->scene.create(name);
    GameObject* go = e->scene.get(h);
    if (!go) return h;
    go->transform.setPosition(pos);
    go->transform.setScale(scale);
    go->transform.setEulerDegrees(eulerDeg);
    auto rh = e->meshRenderers.acquire();
    if (MeshRenderer* mr = e->meshRenderers.get(rh)) {
        mr->mesh = mesh; mr->material = mat;
        go->meshRenderer = mr;
    }
    if (reg) registerObject(e, h);
    return h;
}

static RigidBody* attachShape(Engine* e, GameObject& go, ColliderType type, btCollisionShape* shape, float mass, glm::vec3 offset) {
    auto bh = e->bodies.acquire();
    RigidBody* rb = e->bodies.get(bh);
    if (!rb) { delete shape; return nullptr; }
    rb->centerOffset = offset;
    if (mass > 0.f) go.noCut = true;  // objeto que se move (caixa, barril): não é cortado ao passar por uma abertura
    e->physics.attach(go, *rb, type, shape, mass);
    e->physics.syncFromTransform(*rb);
    return rb;
}

// Collider no tamanho da malha básica (lado/diâmetro 1); a escala do Transform é aplicada pelo Physics
void addBody(Engine* e, Scene::Handle h, ColliderType type, float mass) {
    GameObject* go = e->scene.get(h);
    if (!go) return;
    btCollisionShape* shape = type == ColliderType::Capsule
        ? Physics::makeShape(type, {1.f, 2.f, 1.f})
        : type == ColliderType::ConvexMesh
        ? Physics::makeShape(type, glm::vec3(1.f), e->rockHull.data(), int(e->rockHull.size()))
        : Physics::makeShape(type, glm::vec3(1.f));
    attachShape(e, *go, type, shape, mass, glm::vec3(0.f));
}

void addBoxCollider(Engine* e, Scene::Handle root, glm::vec3 centerLocal, glm::vec3 half) {
    GameObject* go = e->scene.get(root);
    if (!go || go->rigidBody) return;
    auto* comp = new btCompoundShape(true, 1);
    btTransform t;
    t.setIdentity();
    t.setOrigin({centerLocal.x, centerLocal.y, centerLocal.z});
    comp->addChildShape(t, new btBoxShape({half.x, half.y, half.z}));
    attachShape(e, *go, ColliderType::Compound, comp, 0.f, glm::vec3(0.f));
}

void addFloor(Engine* e, float size) {
    Scene::Handle f = addMeshObject(e, "Chao", e->cube, e->matFloor, {0.f, -0.1f, 0.f}, {size, 0.2f, size});
    addBody(e, f, ColliderType::Box, 0.f);
}

void freeStatic(Engine* e, StaticCollision& s) {
    if (s.body) e->physics.removeStatic(s.body);
    delete s.shape;
    for (auto* c : s.children) delete c;
    s = StaticCollision{};
}

void addStaticBoxes(Engine* e, const std::vector<BoxCollider>& boxes, StaticCollision* into) {
    if (boxes.empty()) return;
    StaticCollision s;
    auto* comp = new btCompoundShape(true, int(boxes.size()));
    for (const BoxCollider& b : boxes) {
        auto* box = new btBoxShape({b.half.x, b.half.y, b.half.z});
        btTransform t;
        t.setIdentity();
        t.setOrigin({b.center.x, b.center.y, b.center.z});
        t.setRotation(btQuaternion(btVector3(0, 1, 0), b.yaw) * btQuaternion(btVector3(1, 0, 0), b.pitch));
        comp->addChildShape(t, box);
        s.children.push_back(box);
    }
    s.shape = comp;
    s.body = e->physics.addStatic(comp, glm::vec3(0.f));
    if (into) *into = s;
    else e->statics.push_back(s);
}

void spawnGeoExtras(Engine* e, Geo& g, Scene::Handle rootH, bool local, float detailFar) {
    GameObject* root = e->scene.get(rootH);
    if (!root) return;
    // portas (filhas da raiz: acompanham a construção) e elevadores
    for (const DoorSpec& ds : g.doors) {
        if (e->doorMesh == kInvalidId) {
            MeshBuilder mb;
            mb.box({0.f, 0.f, -0.5f}, {1.f, 1.f, 1.f});  // dobradiça na origem, folha para +X
            e->doorMesh = mb.build(e->renderer);
        }
        glm::vec3 sc, tr, skew; glm::vec4 persp; glm::quat rot;
        glm::decompose(ds.xf, sc, rot, tr, skew, persp);
        Scene::Handle dh = e->scene.create(ds.style ? "Porta de vidro" : "Porta", root);
        GameObject* d = e->scene.get(dh);
        if (!d) break;
        d->transform.setPosition(tr);
        d->transform.setRotation(rot);
        d->transform.setScale({ds.w, ds.h, ds.style ? 0.04f : 0.06f});
        auto rh = e->meshRenderers.acquire();
        if (MeshRenderer* mr = e->meshRenderers.get(rh)) {
            mr->mesh = e->doorMesh;
            mr->material = e->mats[int(ds.style ? Mat::Glass : Mat::Wood)];
            mr->lodFar = detailFar;
            d->meshRenderer = mr;
        }
        if (local) registerObject(e, dh);
        e->doors.push_back({dh, rot, 0.f, ds.w});
    }
    for (const TeleSpec& ts : g.teles) e->teles.push_back({rootH, ts.a, ts.b, ts.la, ts.lb});
}

Scene::Handle spawnGeo(Engine* e, Geo& g, const char* name, bool local, glm::vec3 pos, float yaw, float detailFar) {
    Scene::Handle rootH = e->scene.create(name);
    GameObject* root = e->scene.get(rootH);
    if (!root) return rootH;
    root->transform.setPosition(pos);
    root->transform.setRotation(glm::angleAxis(yaw, glm::vec3(0, 1, 0)));
    if (local) registerObject(e, rootH);  // raiz antes dos filhos na hierarquia
    for (int layer = 0; layer < Geo::kLayers; ++layer)
    for (int k = 0; k < int(Mat::Count); ++k) {
        MeshBuilder& mb = g.layer(layer)[k];
        if (mb.empty()) continue;
        MeshId id = mb.build(e->renderer);
        if (id == kInvalidId) continue;
        Scene::Handle ch = e->scene.create(matName(Mat(k)), root);
        GameObject* c = e->scene.get(ch);
        if (!c) break;
        auto rh = e->meshRenderers.acquire();
        if (MeshRenderer* mr = e->meshRenderers.get(rh)) {
            mr->mesh = id; mr->material = e->mats[k]; c->meshRenderer = mr;
            // pisos planos só recebem sombra (economiza draw calls no shadow pass)
            Mat m = Mat(k);
            mr->castShadows = !(m == Mat::Asphalt || m == Mat::Road || m == Mat::Sidewalk || m == Mat::Grass || m == Mat::Court || m == Mat::Terrain ||
                                 m == Mat::ChainLink || m == Mat::Mural || m == Mat::Cement);  // tela/pichação: a sombra sairia cheia
            if (layer == 3) mr->lodNear = detailFar;  // versão simples: só onde o detalhe já sumiu
            else if (layer) mr->lodFar = layer == 2 ? std::min(detailFar, kInteriorFar) : detailFar;  // LOD: detalhe/interior só perto
        }
        if (local) registerObject(e, ch);
    }
    spawnGeoExtras(e, g, rootH, local, detailFar);
    if (local) {
        if (!g.colliders.empty()) {
            auto* comp = new btCompoundShape(true, int(g.colliders.size()));
            for (const BoxCollider& b : g.colliders) {
                btTransform t;
                t.setIdentity();
                t.setOrigin({b.center.x, b.center.y, b.center.z});
                t.setRotation(btQuaternion(btVector3(0, 1, 0), b.yaw) * btQuaternion(btVector3(1, 0, 0), b.pitch));
                comp->addChildShape(t, new btBoxShape({b.half.x, b.half.y, b.half.z}));
            }
            attachShape(e, *root, ColliderType::Compound, comp, 0.f, glm::vec3(0.f));
        }
    } else {
        addStaticBoxes(e, g.colliders);
    }
    return rootH;
}

MaterialId tintedMaterial(Engine* e, MaterialId base, glm::vec3 c) {
    uint64_t key = (uint64_t(base) << 32) | (uint64_t(c.r * 255.f) << 16) | (uint64_t(c.g * 255.f) << 8) | uint64_t(c.b * 255.f);
    auto it = e->tintCache.find(key);
    if (it != e->tintCache.end()) return it->second;
    const Material* m = e->renderer.material(base);
    if (!m) return base;
    Material copy = *m;
    copy.albedoFactor = glm::vec4(std::pow(c.r, 2.2f), std::pow(c.g, 2.2f), std::pow(c.b, 2.2f), copy.albedoFactor.a);
    MaterialId id = e->renderer.createMaterial(copy);
    e->tintCache[key] = id;
    return id;
}

void clearScene(Engine* e) {
    e->playing = false;
    clearWorld(e);
    e->scene.forEach([&](GameObject& g, Scene::Handle) { releaseComponents(e, g); });
    e->scene.clear();
    e->meshRenderers.clear();
    e->emitters.clear();
    e->particles.clear();
    for (auto& f : e->flashes) f.life = 0.f;
    for (auto& p : e->projectiles) p.active = false;
    for (auto& b : e->bolts) b.life = 0.f;
    e->animatorPool.clear();
    e->bodies.clear();
    e->objects.clear();
    e->animators.clear();
    e->characters.clear();
    e->retargets.clear();
    e->veg.clear();
    e->recipes.clear();
    e->doors.clear();
    e->teles.clear();
    e->openings.clear();
    e->mapLamps.clear();
    e->renderer.cutCount = 0;
    e->physics.cuts.clear();
    e->doorMesh = kInvalidId;
    e->editorCam.maxDistance = 500.f;
    e->lightObjects.clear();
    e->game = GameState{};
    e->rules = GameRules{};
    ++e->revision;
    e->attachments.clear();
    e->models.clear();
    e->weaponParts.clear();
    e->snapshot.clear();
    e->player = -1;
    e->renderer.clearAssets();
    e->renderer.fogDensity = 0.f;
    TextureManager::instance().releaseAll();
    e->cube = e->sphere = e->capsule = e->rock = kInvalidId;
    e->selected = {};
    e->pointCount = 0;
    for (auto& t : e->tracers) t.life = 0.f;
    ensureBasics(e);
}

// ---------------- personagens ----------------
int createCharacter(Engine* e, const CharacterParams& p, glm::vec3 pos, float yaw, bool controllable) {
    if (e->characters.size() >= AnimatorPool::capacity()) { e->lastError = "limite de personagens"; return -1; }
    auto model = std::make_unique<LoadedModel>();
    MeshBuilder mb;
    CharacterParams pm = p;  // com as juntas do modelo, se tiver
    struct ModeloLido { std::vector<Vertex> v; std::vector<uint32_t> idx; int juntas = 0; glm::vec3 j[CJ_Total]; };
    const ModeloLido* lido = nullptr;
    if (!p.modelo.empty()) {  // personagem de modelo (Meshy): a malha com os pesos dos ossos, lida uma vez por modelo
        static std::map<std::string, ModeloLido> cache;
        auto it = cache.find(p.modelo);
        if (it == cache.end()) {
            ModeloLido m;
            if (FILE* f = std::fopen(kitPath(e, "personagens/meshy/" + p.modelo + ".bin").c_str(), "rb")) {
                char mg[4]; uint32_t nv = 0, ni = 0;
                const bool lido4 = std::fread(mg, 1, 4, f) == 4;
                // FSP1: sem juntas; FSP2: as 17 juntas do corpo; FSP3: também os dedos (CJ_Total)
                const int nj = !lido4 ? -1 : std::memcmp(mg, "FSP3", 4) == 0 ? int(CJ_Total) : std::memcmp(mg, "FSP2", 4) == 0 ? 17
                             : std::memcmp(mg, "FSP1", 4) == 0 ? 0 : -1;
                if (nj >= 0 && std::fread(&nv, 4, 1, f) == 1 && std::fread(&ni, 4, 1, f) == 1 && nv < 2000000 && ni < 6000000) {
                    float jf[CJ_Total * 3];
                    if (nj > 0 && std::fread(jf, sizeof(float) * 3 * nj, 1, f) == 1) {
                        m.juntas = nj;
                        for (int k = 0; k < nj; ++k) m.j[k] = glm::vec3(jf[k * 3], jf[k * 3 + 1], jf[k * 3 + 2]);
                    }
                    struct Reg { float p[3], n[3], u[2], t[4]; uint16_t j[4]; float w[4]; };
                    static_assert(sizeof(Reg) == 72, "registro do .bin");
                    std::vector<Reg> r(nv);
                    m.idx.resize(ni);
                    if (std::fread(r.data(), sizeof(Reg), nv, f) == nv && std::fread(m.idx.data(), 4, ni, f) == ni) {
                        m.v.resize(nv);
                        for (uint32_t i = 0; i < nv; ++i) {
                            Vertex& v = m.v[i];
                            v.pos = glm::vec3(r[i].p[0], r[i].p[1], r[i].p[2]);
                            v.normal = glm::vec3(r[i].n[0], r[i].n[1], r[i].n[2]);
                            v.uv = glm::vec2(r[i].u[0], r[i].u[1]);
                            v.tangent = glm::vec4(r[i].t[0], r[i].t[1], r[i].t[2], r[i].t[3]);
                            v.joints = glm::u16vec4(r[i].j[0], r[i].j[1], r[i].j[2], r[i].j[3]);
                            v.weights = glm::vec4(r[i].w[0], r[i].w[1], r[i].w[2], r[i].w[3]);
                            v.color = glm::vec4(1.f);
                        }
                        for (uint32_t& k : m.idx) if (k >= nv) k = 0;
                    } else m = {};
                }
                std::fclose(f);
            }
            it = cache.emplace(p.modelo, std::move(m)).first;
        }
        if (!it->second.v.empty()) {
            lido = &it->second;
            if (lido->juntas) {
                pm.temJuntas = true;
                pm.temDedos = lido->juntas == int(CJ_Total);
                for (int k = 0; k < lido->juntas; ++k) pm.juntas[k] = lido->j[k];
            }
        } else std::printf("personagem: modelo %s não abriu, ficou o boneco\n", p.modelo.c_str());
    }
    buildCharacter(pm, *model, mb);
    if (lido) { mb.v = lido->v; mb.idx = lido->idx; }
    // malha do modelo na GPU uma vez só (por cena: a cena nova apaga as malhas) + a versão de longe (35% dos
    // triângulos, mesmos ossos e pesos)
    struct MalhaModelo { uint32_t rev; MeshId perto, longe; };
    static std::map<std::string, MalhaModelo> naGpu;
    MeshId mesh = kInvalidId, meshLonge = kInvalidId;
    if (!p.modelo.empty() && mb.v.size() > 0 && !mb.idx.empty()) {
        auto g = naGpu.find(p.modelo);
        if (g != naGpu.end() && g->second.rev == uint32_t(e->renderer.geracao) && e->renderer.mesh(g->second.perto)) {
            mesh = g->second.perto; meshLonge = g->second.longe;
        } else {
            mesh = mb.build(e->renderer, true);
            std::vector<Vertex> v = mb.v;
            std::vector<uint32_t> idx = mb.idx;
            simplifyMesh(v, idx, 0.35f);
            meshLonge = e->renderer.createMesh(v.data(), uint32_t(v.size()), idx.data(), uint32_t(idx.size()), true);
            naGpu[p.modelo] = {uint32_t(e->renderer.geracao), mesh, meshLonge};
        }
    }
    if (mesh == kInvalidId) mesh = mb.build(e->renderer, true);
    if (mesh == kInvalidId) return -1;

    char name[32];
    std::snprintf(name, sizeof(name), "Personagem %d", int(e->characters.size()) + 1);
    Scene::Handle rootH = e->scene.create(name);
    GameObject* root = e->scene.get(rootH);
    if (!root) return -1;
    root->transform.setPosition(pos);
    root->transform.setRotation(glm::angleAxis(yaw, glm::vec3(0, 1, 0)));
    int rootId = registerObject(e, rootH);

    Scene::Handle bodyH = e->scene.create("Corpo", root);
    GameObject* body = e->scene.get(bodyH);
    auto ah = e->animatorPool.acquire();
    Animator* anim = e->animatorPool.get(ah);
    auto rh = e->meshRenderers.acquire();
    MeshRenderer* mr = e->meshRenderers.get(rh);
    if (!body || !anim || !mr) return -1;
    registerObject(e, bodyH);
    anim->skeleton = &model->skeletons[0];
    anim->clips = model->clips.data();
    anim->clipCount = int(model->clips.size());
    anim->space = &root->transform;
    anim->setClip(CC_Idle);
    anim->time = float(e->characters.size()) * 0.37f;  // personagens não ficam sincronizados
    anim->update(0.f, false);
    mr->mesh = mesh; mr->material = e->mats[int(Mat::Painted)];
    if (meshLonge != kInvalidId) { mr->meshLonge = meshLonge; mr->distLonge = 14.f; }
    body->meshRenderer = mr;
    body->animator = anim;
    e->animators.push_back(anim);
    e->models.push_back(std::move(model));

    // Cápsula de física com origem nos pés (rotação travada: o controle gira o personagem)
    float diam = 0.38f * p.height / 1.75f * (p.build == 2 ? 1.15f : 1.f);
    btCollisionShape* shape = Physics::makeShape(ColliderType::Capsule, {diam, p.height, diam});
    if (RigidBody* rb = attachShape(e, *root, ColliderType::Capsule, shape, 60.f, {0.f, p.height * 0.5f, 0.f})) {
        rb->lockRotX = rb->lockRotY = rb->lockRotZ = true;
        rb->applyAxisConstraints();
        rb->body->setFriction(0.4f);
        rb->body->setActivationState(DISABLE_DEACTIVATION);
    }
    CharacterInfo info;
    info.root = rootH;
    info.anim = anim;
    info.height = p.height;
    for (int k = 0; k < CC_Count; ++k) info.clipMap[k] = k;
    e->characters.push_back(info);
    if (controllable || e->player < 0) setPlayer(e, int(e->characters.size()) - 1);
    return rootId;
}

void setPlayer(Engine* e, int ch) {
    if (ch < 0 || ch >= int(e->characters.size())) { e->player = -1; return; }
    e->player = ch;
    if (GameObject* g = e->scene.get(e->characters[ch].root)) {
        if (g->rigidBody && g->rigidBody->body) g->rigidBody->body->setFriction(0.f);  // não gruda nas paredes
    }
}

static void destroyObject(Engine* e, Scene::Handle h) {
    GameObject* g = e->scene.get(h);
    if (!g) return;
    Scene::forEachInSubtree(*g, [&](GameObject& o) { releaseComponents(e, o); });
    e->scene.destroy(h);
}

static bool isGun(int kind) { return kind == WK_Pistol || kind == WK_Rifle || kind == WK_Shotgun || kind == WK_Sniper; }

// Peças (malha + matriz) viram filhos de um objeto novo; `fix` vai na frente de cada matriz
static Scene::Handle spawnParts(Engine* e, const std::vector<ProtoPart>& parts, const glm::mat4& fix, const char* name, glm::vec3 pos,
                                GameObject* parent = nullptr, MaterialId material = kInvalidId) {
    Scene::Handle rootH = e->scene.create(name, parent);
    GameObject* root = e->scene.get(rootH);
    if (!root) return rootH;
    root->transform.setPosition(pos);
    registerObject(e, rootH);
    for (const ProtoPart& pp : parts) {
        glm::vec3 sc, tr, skew; glm::vec4 persp; glm::quat rot;
        glm::decompose(fix * pp.local, sc, rot, tr, skew, persp);
        Scene::Handle ch = e->scene.create(pp.bomba ? "Bomba" : "Peça", root);
        GameObject* c = e->scene.get(ch);
        if (!c) break;
        c->transform.setPosition(tr); c->transform.setRotation(rot); c->transform.setScale(sc);
        auto rh = e->meshRenderers.acquire();
        if (MeshRenderer* mr = e->meshRenderers.get(rh)) {
            mr->mesh = pp.mesh; mr->material = material != kInvalidId ? material : pp.mat; mr->castShadows = true;
            mr->lodNear = pp.lodNear; mr->lodFar = pp.lodFar;
            c->meshRenderer = mr;
        }
        registerObject(e, ch);
    }
    return rootH;
}

// Acessórios táticos (Low.fbx: trilho, mira red dot, coronha e empunhadura frontal), encaixados pela caixa da arma.
// wb: caixa da arma no espaço dela (mão na origem, cano +Z).
static void addAccessories(Engine* e, GameObject& weapon, uint32_t acc, const AABB& wb, bool procedural) {
    if (!acc) return;
    const std::string file = e->renderer.shaderDir() + "/kit/armas/acessorios.fbx";
    std::vector<ProtoPart> all;
    std::vector<std::string> names;
    AABB fileBox;
    if (!modelParts(e, file, nullptr, all, fileBox, &names)) return;  // lido uma vez; as peças saem pelo nome
    auto pick = [&](const char* key, AABB& box) {
        std::vector<ProtoPart> out;
        box = AABB{glm::vec3(1e30f), glm::vec3(-1e30f)};
        for (size_t i = 0; i < all.size(); ++i) {
            if (names[i].find(key) == std::string::npos) continue;
            out.push_back(all[i]);
            if (const Mesh* m = e->renderer.mesh(all[i].mesh)) {
                AABB b = transformAABB(m->bounds, all[i].local);
                box.min = glm::min(box.min, b.min); box.max = glm::max(box.max, b.max);
            }
        }
        return out;
    };
    // eixo do arquivo: o lado mais comprido; a frente é o lado da mira (a coronha fica atrás)
    AABB dotBox, stockBox, mountBox;
    std::vector<ProtoPart> dot = pick("red_dot", dotBox), stock = pick("backstock", stockBox), mount = pick("mount", mountBox);
    glm::vec3 ext = fileBox.max - fileBox.min;
    glm::mat4 R(1.f);
    if (ext.x > ext.z) R = glm::rotate(glm::mat4(1.f), -1.5707963f, glm::vec3(0, 1, 0));
    glm::vec3 front = glm::vec3(R * glm::vec4((dotBox.min + dotBox.max) * 0.5f - (stockBox.min + stockBox.max) * 0.5f, 0.f));
    if (!dot.empty() && !stock.empty() && front.z < 0.f) R = glm::rotate(glm::mat4(1.f), 3.14159265f, glm::vec3(0, 1, 0)) * R;
    // escala: a mira red dot tem uns 12 cm
    AABB rd = transformAABB(dot.empty() ? fileBox : dotBox, R);
    const float s = 0.12f / std::max(1e-4f, rd.max.z - rd.min.z);
    const float len = wb.max.z - wb.min.z;
    const MaterialId dark = e->mats[int(Mat::GunMetal)], poly = e->mats[int(Mat::Polymer)];
    auto place = [&](const char* key, const char* name, MaterialId mat, auto&& anchorOf, glm::vec3 at) {
        AABB box;
        std::vector<ProtoPart> parts = pick(key, box);
        if (parts.empty()) return;
        AABB rb = transformAABB(box, R);
        glm::vec3 pivot = anchorOf(rb);
        glm::mat4 fix = glm::translate(glm::mat4(1.f), at) * glm::scale(glm::mat4(1.f), glm::vec3(s)) * glm::translate(glm::mat4(1.f), -pivot) * R;
        spawnParts(e, parts, fix, name, glm::vec3(0.f), &weapon, mat);
    };
    auto midX = [](const AABB& b) { return (b.min.x + b.max.x) * 0.5f; };
    auto midZ = [](const AABB& b) { return (b.min.z + b.max.z) * 0.5f; };
    if (acc & ACC_RedDot) {  // trilho + mira em cima, um pouco à frente da mão
        const float h = wb.max.y - wb.min.y;  // topo do corpo da arma (sem contar a massa de mira): ~80% da altura
        glm::vec3 at(0.f, wb.min.y + h * 0.8f, wb.min.z + len * 0.5f);
        place("mount", "Trilho", dark, [&](const AABB& b) { return glm::vec3(midX(b), b.min.y, midZ(b)); }, at);
        float mountH = mount.empty() ? 0.f : (transformAABB(mountBox, R).max.y - transformAABB(mountBox, R).min.y) * s;
        place("red_dot", "Mira red dot", poly, [&](const AABB& b) { return glm::vec3(midX(b), b.min.y, midZ(b)); },
              at + glm::vec3(0.f, mountH * 0.85f, 0.f));
    }
    if (acc & ACC_Stock)  // coronha atrás da empunhadura
        place("backstock", "Coronha tática", poly, [&](const AABB& b) { return glm::vec3(midX(b), b.max.y * 0.5f + b.min.y * 0.5f, b.max.z); },
              glm::vec3(0.f, 0.045f, -0.06f));
    if (acc & ACC_Foregrip)  // empunhadura frontal embaixo do guarda-mão
        place("front", "Empunhadura frontal", poly, [&](const AABB& b) { return glm::vec3(midX(b), b.max.y, midZ(b)); },
              glm::vec3(0.f, procedural ? 0.024f : wb.min.y + (wb.max.y - wb.min.y) * 0.57f, wb.max.z * 0.55f));  // embaixo do guarda-mão
}

// o que os acessórios da última arma montada mudaram (createWeapon usa logo depois)
static AcessorioInfo g_acessorio;

// Modelo da arma (cano em +Z, origem no cabo, no tamanho do tipo); muzzle e wb saem no espaço da arma
static Scene::Handle spawnWeaponModel(Engine* e, int kind, const WeaponLook& look, glm::vec3& muzzle, AABB& wb) {
    glm::vec3 pos = e->editorCam.target + glm::vec3(0, 0.6f, 0);
    Scene::Handle h;
    if (!look.file.empty()) {
        // modelo do usuário: deita com o cano em +Z, tamanho do tipo, mão no cabo
        std::vector<ProtoPart> parts;
        AABB b;
        // As peças de cada arma ficam guardadas depois da primeira vez: renascer, trocar e comprar arma não lê o
        // arquivo de novo (eram ~40 leituras de 1,5 a 6 MB no começo da partida, cada uma com 2 versões)
        if (auto it = e->weaponParts.find(look.file); it != e->weaponParts.end()) {
            parts = it->second.first;
            b = it->second.second;
        } else {
            if (!modelParts(e, look.file, nullptr, parts, b)) {
                if (e->lastError.empty()) e->lastError = "o arquivo não tem malha para virar arma";
                return {};
            }
            // Versão de longe (bem poucos triângulos): a arma da mão e as de perto ficam detalhadas; as dos outros
            // lutadores, a partir de alguns metros, usam esta (as armas eram mais da metade dos triângulos da partida)
            static const float kLowFrac[4] = {0.035f, 0.08f, 0.12f, 0.2f}, kLowDist[4] = {4.f, 5.f, 7.f, 12.f};  // Leve: 3,5%
            const int nv = std::clamp(e->nivel, 0, 3);
            std::vector<ProtoPart> low;
            AABB lb;
            // a versão com a geometria original (_alta, só na mão do jogador, sempre perto) não ganha cópia reduzida
            const bool original = look.file.size() > 5 && look.file.compare(look.file.size() - 5, 5, "_alta") == 0;
            e->renderer.simplifyOverride = kLowFrac[nv];
            const bool okLow = !original && modelParts(e, look.file, nullptr, low, lb);
            e->renderer.simplifyOverride = 0.f;
            if (okLow && low.size() == parts.size()) {
                for (ProtoPart& p : parts) p.lodFar = kLowDist[nv];
                for (ProtoPart& p : low) { p.lodNear = kLowDist[nv]; parts.push_back(p); }
            }
            e->weaponParts[look.file] = {parts, b};
        }
        glm::vec3 ext = b.max - b.min;
        glm::mat4 R(1.f);
        if (ext.x > ext.z) R = glm::rotate(glm::mat4(1.f), -1.5707963f, glm::vec3(0, 1, 0));
        if (look.flip) R = glm::rotate(glm::mat4(1.f), 3.14159265f, glm::vec3(0, 1, 0)) * R;
        AABB rb = transformAABB(b, R);
        static const float kLen[WK_Count] = {0.25f, 0.95f, 1.f, 1.15f, 0.3f, 0.85f};  // pistola um pouco maior: na mão do personagem a TT-33 ficava pequena
        static const float kGrip[WK_Count] = {0.25f, 0.35f, 0.35f, 0.35f, 0.12f, 0.12f};  // mão a partir de trás
        float s = kLen[kind] * std::clamp(look.size, 0.2f, 5.f) / std::max(1e-4f, rb.max.z - rb.min.z);
        rb.min *= s; rb.max *= s;
        bool gun = kind <= WK_Sniper;
        glm::vec3 grip((rb.min.x + rb.max.x) * 0.5f, gun ? rb.min.y + (rb.max.y - rb.min.y) * 0.3f : (rb.min.y + rb.max.y) * 0.5f,
                       rb.min.z + (rb.max.z - rb.min.z) * kGrip[kind]);
        glm::mat4 fix = glm::translate(glm::mat4(1.f), -grip) * glm::scale(glm::mat4(1.f), glm::vec3(s)) * R;
        muzzle = glm::vec3(0.f, (rb.min.y + rb.max.y) * 0.5f + (rb.max.y - rb.min.y) * 0.2f - grip.y, rb.max.z - grip.z);
        wb = AABB{rb.min - grip, rb.max - grip};
        h = spawnParts(e, parts, fix, weaponName(kind), pos);
        if (GameObject* wgo = e->scene.get(h)) montarAcessorios(e, *wgo, look.file, look.accessories, fix, g_acessorio);
        else g_acessorio = AcessorioInfo{};
        // versão detalhada (_alta, centenas de milhares de triângulos, só na mão do jogador): sem sombra, que
        // desenharia a arma de novo em cada faixa do mapa de sombra (a sombra de uma arma na mão quase não aparece)
        if (look.file.size() > 5 && look.file.compare(look.file.size() - 5, 5, "_alta") == 0)
            if (GameObject* wg = e->scene.get(h))
                Scene::forEachInSubtree(*wg, [](GameObject& o) { if (o.meshRenderer && o.meshRenderer->lodNear <= 0.f) o.meshRenderer->castShadows = false; });
    } else {
        Geo g;
        g.place(glm::vec3(0.f), 0.f);
        muzzle = buildWeapon(g, kind, !(look.accessories & ACC_Stock));
        h = spawnGeo(e, g, weaponName(kind), true, pos, 0.f);
        e->scene.updateTransforms();
        GameObject* w = e->scene.get(h);
        if (w && subtreeBounds(e, *w, wb)) { wb.min -= pos; wb.max -= pos; }
        else wb = AABB{glm::vec3(-0.03f, -0.1f, -0.4f), glm::vec3(0.03f, 0.1f, 0.7f)};
    }
    GameObject* w = e->scene.get(h);
    if (!w) return {};
    addAccessories(e, *w, look.accessories, wb, look.file.empty());
    Scene::forEachInSubtree(*w, [](GameObject& o) { o.noCut = true; });  // passa pela porta sem ser cortada
    return h;
}

// Carrega as peças de uma arma do kit sem deixar nada na cena (para a partida já começar com elas prontas)
void warmWeaponModel(Engine* e, const std::string& file, int kind) {
    if (e->weaponParts.count(file)) return;
    WeaponLook look;
    look.file = file;
    glm::vec3 muzzle;
    AABB wb;
    Scene::Handle h = spawnWeaponModel(e, kind, look, muzzle, wb);
    if (e->scene.get(h)) deleteObject(e, h);
    e->lastError.clear();
}

int createWeapon(Engine* e, int kind, int ch, const WeaponLook& look) {
    kind = std::clamp(kind, 0, WK_Count - 1);
    // armas feitas de formas geométricas foram retiradas: sem arquivo, vira o modelo realista (pistola: TT-33;
    // as outras: M4A1); faca e taco saem
    if (look.file.empty()) {
        if (kind == WK_Knife || kind == WK_Bat) { e->lastError = "faca e taco foram retirados (só armas realistas)"; return -1; }
        WeaponLook real = look;
        real.file = kind == WK_Pistol ? "kit:armas/tt33" : "kit:armas/m4a1";
        return createWeapon(e, kind == WK_Pistol ? WK_Pistol : WK_Rifle, ch, real);
    }
    glm::vec3 muzzle;
    AABB wb;
    g_acessorio = AcessorioInfo{};
    Scene::Handle h = spawnWeaponModel(e, kind, look, muzzle, wb);
    const AcessorioInfo acc = g_acessorio;
    GameObject* w = e->scene.get(h);
    if (!w) return -1;
    if (ch >= 0 && ch < int(e->characters.size())) {
        CharacterInfo& c = e->characters[ch];
        if (e->scene.get(c.weapon)) {  // troca a arma antiga
            e->attachments.erase(std::remove_if(e->attachments.begin(), e->attachments.end(),
                                                [&](const Attachment& a) { return a.obj.index == c.weapon.index && a.obj.gen == c.weapon.gen; }),
                                 e->attachments.end());
            destroyObject(e, c.weapon);
        }
        // empunhadura na palma: desce do pulso e gira para o cano seguir os dedos
        e->attachments.push_back({h, c.anim, c.handJoint, c.handFix * weaponGripOffset(kind, c.height)});
        c.weapon = h;
        c.weaponKind = kind;
        c.muzzle = acc.temBoca ? acc.muzzle : muzzle;  // silenciador/freio: o tiro sai da ponta dele
        c.silenciada = (look.accessories & ACC_Silenciador) && acc.temBoca;
        // som: cada fuzil do kit tem o seu; os outros pelo tipo
        {
            std::string f = look.file;
            std::transform(f.begin(), f.end(), f.begin(), [](unsigned char ch2) { return char(std::tolower(ch2)); });
            static const char* byKind[] = {"tiro_pistola", "tiro_m4", "tiro_escopeta", "tiro_sniper", "golpe_faca", "golpe_taco"};
            c.scope = f.find("m4colt") != std::string::npos;  // modelos com luneta
            c.shotSound = f.find("ak47") != std::string::npos ? "tiro_ak47"
                        : f.find("ak74") != std::string::npos ? "tiro_ak74"
                        : f.find("m4") != std::string::npos ? "tiro_m4" : byKind[std::clamp(kind, 0, WK_Count - 1)];
        }
        // onde a mão esquerda segura: guarda-mão (ou a empunhadura frontal, se tiver)
        c.hasSupport = kind == WK_Rifle || kind == WK_Shotgun || kind == WK_Sniper || kind == WK_Pistol;
        if (c.hasSupport && kind != WK_Pistol) {  // pistola: a mão esquerda segura o pulso direito (veja updateSupportHands)
            const float len = wb.max.z - wb.min.z, hgt = wb.max.y - wb.min.y;
            if (look.accessories & ACC_Foregrip)
                c.support = glm::vec3(0.f, (look.file.empty() ? 0.024f : wb.min.y + hgt * 0.57f) - 0.05f, wb.max.z * 0.55f);
            else if (look.file.empty())
                c.support = kind == WK_Shotgun ? glm::vec3(0.f, 0.01f, 0.43f) : glm::vec3(0.f, 0.03f, kind == WK_Sniper ? 0.4f : 0.38f);
            else
                c.support = glm::vec3(0.f, wb.min.y + hgt * 0.55f, wb.min.z + len * 0.64f);
            // alça de mira: no alto da arma, um terço do comprimento a partir da coronha
            c.sight = glm::vec3(0.f, wb.max.y - hgt * 0.06f, wb.min.z + len * 0.36f);
            // AK (alça de mira aberta): o olho passa rente por cima da alça traseira e a ponta da massa de mira
            // (lá na frente) fica bem no centro da tela, onde está o ponto vermelho (antes a alça tampava o centro)
            if (look.file.find("ak") != std::string::npos) c.sight.y = wb.max.y - 0.011f;  // (o olho fica 1,2 cm acima da alça)
            // linha da mira de cada modelo do kit: o centro da caixa da arma fica de lado (alavanca, ejetor), a alça
            // e a massa ficam alguns milímetros para +X (medido nos arquivos: tools/medir_mira_armas.py)
            {
                const std::string& fl = look.file;
                c.sight.x = fl.find("ak74_tatica") != std::string::npos ? 0.0088f : fl.find("ak74") != std::string::npos ? 0.0107f
                          : fl.find("ak47") != std::string::npos ? 0.0138f : fl.find("m4a1") != std::string::npos ? 0.0055f : 0.f;
                if (look.flip) c.sight.x = -c.sight.x;
            }
            // skin com modelo próprio (armas da Meshy: kit:armas/<arma>_<skin>): a alça de mira é um bloco sem furo,
            // que tampava o centro da tela mirando; o olho passa rente por cima da arma toda, no meio da largura
            {
                static const char* kBase[] = {"kit:armas/m4a1", "kit:armas/m4colt", "kit:armas/ak47", "kit:armas/ak74", "kit:armas/ak74_tatica"};
                const bool base = std::any_of(std::begin(kBase), std::end(kBase), [&](const char* b2) { return look.file == b2; });
                if (!base && look.file.rfind("kit:armas/", 0) == 0) c.sight = glm::vec3((wb.min.x + wb.max.x) * 0.5f, wb.max.y + 0.004f, wb.min.z + len * 0.3f);
            }
            c.butt = glm::vec3(0.f, wb.min.y + hgt * 0.62f, wb.min.z + 0.01f);
            c.gripPt = glm::vec3(0.f, wb.min.y + hgt * 0.4f, wb.min.z + len * 0.315f);
            c.guardPt = glm::vec3(0.f, wb.min.y + hgt * 0.68f, wb.min.z + len * 0.63f);
        } else if (kind == WK_Pistol) {  // pistola: alça de mira em cima, atrás; cabo embaixo, atrás
            const float len = wb.max.z - wb.min.z, hgt = wb.max.y - wb.min.y;
            c.sight = glm::vec3(0.f, wb.max.y - hgt * 0.03f, wb.min.z + len * 0.15f);
            c.gripPt = glm::vec3(0.f, wb.min.y + hgt * 0.35f, wb.min.z + len * 0.2f);
        }
        if (acc.temMira) { c.sight = acc.sight; c.scope = c.scope || acc.luneta; }  // mira holográfica / luneta
        // bomba (Doze): as peças que deslizam; a mão de apoio vai no meio delas
        c.bombaObjs.clear(); c.bombaBase.clear(); c.bombaT = 0.f; c.bombaOff = 0.f; c.cartucho = false; c.precisaBombear = false;
        AABB bb{glm::vec3(1e30f), glm::vec3(-1e30f)};
        Scene::forEachInSubtree(*w, [&](GameObject& o) {
            if (&o == w || std::strcmp(o.name, "Bomba") != 0) return;
            c.bombaObjs.push_back(Scene::handleOf(o));
            c.bombaBase.push_back(o.transform.position);
            if (o.meshRenderer && o.meshRenderer->lodNear <= 0.f)
                if (const Mesh* m = e->renderer.mesh(o.meshRenderer->mesh)) {
                    const AABB pb = transformAABB(m->bounds, o.transform.local());
                    bb.min = glm::min(bb.min, pb.min); bb.max = glm::max(bb.max, pb.max);
                }
        });
        if (!c.bombaObjs.empty() && bb.min.x < 1e29f) {
            c.support = glm::vec3((bb.min.x + bb.max.x) * 0.5f, bb.min.y + (bb.max.y - bb.min.y) * 0.45f, (bb.min.z + bb.max.z) * 0.5f);
            c.guardPt = glm::vec3(0.f, bb.min.y + (bb.max.y - bb.min.y) * 0.6f, (bb.min.z + bb.max.z) * 0.5f);
        }
        int base = armedClipBase(kind);
        if (c.anim && base >= 0 && c.clipMap[base + AP_Idle] >= 0) c.anim->setClip(c.clipMap[base + AP_Idle]);
        updateAttachments(e);
    }
    return w->id;
}

// Arma guardada no corpo (a que não está na mão), como no Free Fire: fuzil nas costas, na diagonal com o cano
// subindo pelo ombro direito; pistola na coxa direita. Presa ao osso do peito/quadril, acompanha a animação.
// kind < 0: só tira a que estava guardada.
// Peça (malha montada no espaço do personagem, na pose de origem) presa a um osso: acompanha as animações.
// Sai junto quando o personagem é apagado. Devolve o id do objeto ou -1.
int attachGearMesh(Engine* e, int ch, int charJoint, Geo& g, const char* name) {
    if (ch < 0 || ch >= int(e->characters.size())) return -1;
    CharacterInfo& c = e->characters[ch];
    Animator* a = c.anim;
    if (!a || !a->skeleton) return -1;
    const int joint = a->retarget ? a->retarget->targetOf[charJoint] : (a->skeleton->jointCount == CJ_Total ? charJoint : -1);
    if (joint < 0) return -1;
    Scene::Handle h = spawnGeo(e, g, name, true);
    GameObject* o = e->scene.get(h);
    if (!o) return -1;
    Scene::forEachInSubtree(*o, [](GameObject& x) { if (x.meshRenderer) x.meshRenderer->castShadows = true; });
    e->attachments.push_back({h, a, joint, a->skeleton->inverseBind[joint]});
    c.gear.push_back(h);
    updateAttachments(e);
    return o->id;
}

int holsterWeapon(Engine* e, int ch, int kind, const WeaponLook& look) {
    if (ch < 0 || ch >= int(e->characters.size())) return -1;
    CharacterInfo& c = e->characters[ch];
    if (GameObject* old = e->scene.get(c.holster)) {
        Scene::Handle oh = c.holster;
        e->attachments.erase(std::remove_if(e->attachments.begin(), e->attachments.end(),
                                            [&](const Attachment& a) { return a.obj.index == oh.index && a.obj.gen == oh.gen; }),
                             e->attachments.end());
        c.gear.erase(std::remove_if(c.gear.begin(), c.gear.end(), [&](Scene::Handle g) { return g.index == oh.index && g.gen == oh.gen; }), c.gear.end());
        (void)old;
        destroyObject(e, oh);
    }
    c.holster = {};
    Animator* a = c.anim;
    if (kind < 0 || look.file.empty() || !a || !a->skeleton) return -1;
    const bool pistol = kind == WK_Pistol;
    const int cj = pistol ? CJ_Hips : CJ_Chest;
    const int joint = a->retarget ? a->retarget->targetOf[cj] : (a->skeleton->jointCount == CJ_Total ? cj : -1);
    GameObject* body = e->scene.get(c.root);
    if (joint < 0 || !body) return -1;
    glm::vec3 muzzle;
    AABB wb;
    Scene::Handle h = spawnWeaponModel(e, kind, look, muzzle, wb);
    GameObject* w = e->scene.get(h);
    if (!w) return -1;
    // posição desejada no mundo, a partir do corpo de agora; o deslocamento até o osso fica guardado
    e->scene.updateTransforms();
    const glm::mat4 space = a->space ? a->space->world() : glm::mat4(1.f);
    const glm::mat4 boneW = space * a->global[joint];
    const glm::vec3 fwd = glm::normalize(body->transform.rotation * glm::vec3(0, 0, 1));
    const glm::vec3 up(0.f, 1.f, 0.f);
    const glm::vec3 rt = glm::normalize(glm::cross(fwd, up));  // lado direito do personagem
    const float sc = c.height / 1.75f;
    glm::vec3 z, x, pos = glm::vec3(boneW[3]);
    if (pistol) {  // cano para baixo, encostada na lateral da coxa
        z = -up;
        x = rt;
        pos += (rt * 0.17f - up * 0.05f + fwd * 0.02f) * sc;
    } else {       // deitada nas costas: a lateral da arma encosta no corpo
        z = glm::normalize(up * 0.86f + rt * 0.5f);
        x = -fwd;
        pos += (-fwd * 0.17f + up * 0.02f - rt * 0.1f) * sc;
    }
    const glm::vec3 y = glm::normalize(glm::cross(z, x));
    x = glm::normalize(glm::cross(y, z));
    glm::mat4 want(1.f);
    want[0] = glm::vec4(x, 0.f); want[1] = glm::vec4(y, 0.f); want[2] = glm::vec4(z, 0.f); want[3] = glm::vec4(pos, 1.f);
    e->attachments.push_back({h, a, joint, glm::inverse(boneW) * want});
    c.holster = h;
    c.gear.push_back(h);  // apagar o personagem apaga a arma guardada junto
    updateAttachments(e);
    return w->id;
}

void updateAttachments(Engine* e) {
    for (const Attachment& a : e->attachments) {
        GameObject* o = e->scene.get(a.obj);
        if (!o || !a.anim) continue;
        glm::mat4 space = a.anim->space ? a.anim->space->world() : glm::mat4(1.f);
        glm::mat4 m = space * a.anim->global[a.joint] * a.offset;
        glm::vec3 x(m[0]), y(m[1]), z(m[2]);
        glm::mat3 rot(glm::normalize(x), glm::normalize(y), glm::normalize(z));
        o->transform.position = glm::vec3(m[3]);
        o->transform.rotation = glm::normalize(glm::quat_cast(rot));
        o->transform.markDirty();
        if (!o->transform.parent) o->transform.updateWorld(glm::mat4(1.f), false);
    }
}

// Gira um osso e tudo o que vem depois dele em volta de `pivot` (espaço do esqueleto)
static void rotateJointSubtree(Animator& a, int root, const glm::quat& r, glm::vec3 pivot) {
    const Skeleton& S = *a.skeleton;
    glm::mat4 M = glm::translate(glm::mat4(1.f), pivot) * glm::mat4_cast(r) * glm::translate(glm::mat4(1.f), -pivot);
    bool in[kMaxJoints] = {};
    for (int k = 0; k < S.jointCount; ++k) {
        int i = S.order[k];
        in[i] = i == root || (S.parent[i] >= 0 && in[S.parent[i]]);
        if (!in[i]) continue;
        a.global[i] = M * a.global[i];
        a.palette[i] = a.global[i] * S.inverseBind[i];
    }
}

static int jointNamed(const Skeleton& S, const char* suffix) {
    size_t n = std::strlen(suffix);
    for (int j = 0; j < S.jointCount; ++j) {
        size_t m = std::strlen(S.name[j]);
        if (m >= n && std::strcmp(S.name[j] + m - n, suffix) == 0) return j;
    }
    return -1;
}

// Gira a mão (com os dedos) para os dedos apontarem em `fingers` e o polegar para o lado de `thumb`;
// depois fecha os dedos em volta de `curlAxis` (segurando o cabo/guarda-mão). Tudo no espaço do esqueleto.
static void poseHand(Animator& a, int hand, bool leftSide, glm::vec3 fingers, glm::vec3 thumb, glm::vec3 curlAxis, float curl, float w) {
    const Skeleton& S = *a.skeleton;
    const char* side = leftSide ? "LeftHand" : "RightHand";
    char buf[48];
    auto j = [&](const char* f, int k) { std::snprintf(buf, sizeof(buf), "%s%s%d", side, f, k); return jointNamed(S, buf); };
    int mid = j("Middle", 1), th = j("Thumb", 1);
    if (hand < 0 || mid < 0 || w <= 0.f) return;
    auto P = [&](int k) { return glm::vec3(a.global[k][3]); };
    glm::vec3 p = P(hand);
    glm::vec3 f = P(mid) - p;
    if (glm::length(f) < 1e-5f) return;
    glm::quat r1 = glm::rotation(glm::normalize(f), glm::normalize(fingers));
    rotateJointSubtree(a, hand, glm::slerp(glm::quat(1, 0, 0, 0), r1, w), p);
    if (th >= 0) {
        glm::vec3 fd = glm::normalize(P(mid) - p);
        glm::vec3 t = P(th) - p, want = thumb;
        t -= fd * glm::dot(t, fd); want -= fd * glm::dot(want, fd);
        if (glm::length(t) > 1e-5f && glm::length(want) > 1e-5f) {
            glm::quat r2 = glm::rotation(glm::normalize(t), glm::normalize(want));
            rotateJointSubtree(a, hand, glm::slerp(glm::quat(1, 0, 0, 0), r2, w), p);
        }
    }
    // dedos fechando (3 falanges de cada dedo)
    for (const char* f : {"Index", "Middle", "Ring", "Pinky"})
        for (int k = 1; k <= 3; ++k) {
            int jj = j(f, k);
            if (jj < 0) continue;
            rotateJointSubtree(a, jj, glm::angleAxis(curl * w, glm::normalize(curlAxis)), P(jj));
        }
}

// IK de dois ossos (ombro-cotovelo-pulso) direto nas matrizes globais da pose: leva o pulso até `target`
// mantendo o plano do cotovelo; gira cada osso (com tudo o que vem depois dele) em volta da própria junta.
// poleHint (opcional): para onde o cotovelo deve apontar; sem ele, mantém o plano do cotovelo da animação
static void twoBoneIK(Animator& a, int up, int fo, int ha, glm::vec3 target, glm::vec3 poleHint = glm::vec3(0.f)) {
    const Skeleton& S = *a.skeleton;
    auto pos = [&](int j) { return glm::vec3(a.global[j][3]); };
    auto rotateSubtree = [&](int root, const glm::quat& r, glm::vec3 pivot) {
        glm::mat4 M = glm::translate(glm::mat4(1.f), pivot) * glm::mat4_cast(r) * glm::translate(glm::mat4(1.f), -pivot);
        bool in[kMaxJoints] = {};
        for (int k = 0; k < S.jointCount; ++k) {
            int i = S.order[k];
            in[i] = i == root || (S.parent[i] >= 0 && in[S.parent[i]]);
            if (!in[i]) continue;
            a.global[i] = M * a.global[i];
            a.palette[i] = a.global[i] * S.inverseBind[i];
        }
    };
    glm::vec3 p0 = pos(up), p1 = pos(fo), p2 = pos(ha);
    float la = glm::length(p1 - p0), lb = glm::length(p2 - p1);
    if (la < 1e-5f || lb < 1e-5f) return;
    glm::vec3 to = target - p0;
    float d = glm::clamp(glm::length(to), std::abs(la - lb) + 1e-4f, la + lb - 1e-4f);
    glm::vec3 u = glm::length(to) > 1e-5f ? glm::normalize(to) : glm::normalize(p2 - p0);
    glm::vec3 pole = glm::length(poleHint) > 1e-5f ? poleHint : (p1 - p0);
    pole -= u * glm::dot(pole, u);
    pole = glm::length(pole) > 1e-5f ? glm::normalize(pole) : glm::vec3(0.f, -1.f, 0.f);
    float cosA = glm::clamp((la * la + d * d - lb * lb) / (2.f * la * d), -1.f, 1.f);
    glm::vec3 elbow = p0 + u * (la * cosA) + pole * (la * std::sqrt(1.f - cosA * cosA));
    rotateSubtree(up, glm::rotation(glm::normalize(p1 - p0), glm::normalize(elbow - p0)), p0);
    glm::vec3 wrist = pos(ha), e1 = pos(fo);
    glm::vec3 want = p0 + u * d;
    if (glm::length(wrist - e1) > 1e-5f && glm::length(want - e1) > 1e-5f)
        rotateSubtree(fo, glm::rotation(glm::normalize(wrist - e1), glm::normalize(want - e1)), e1);
}

void updateSupportHands(Engine* e, float dt) {
    static const bool off = std::getenv("MOTOR_SEM_IK") != nullptr;  // comparação nos testes (antes/depois)
    if (off) return;
    for (CharacterInfo& c : e->characters) {
        Animator* a = c.anim;
        GameObject* w = e->scene.get(c.weapon);
        if (!a || !a->skeleton || !w || !c.hasSupport) { c.supportW = 0.f; continue; }
        // só nas animações com as duas mãos na arma (não ao recarregar nem morrendo)
        // pistola: só correndo e mirando (parado/andando ela fica ao lado da perna, numa mão)
        const bool pistol = c.weaponKind == WK_Pistol;
        bool armed = false;
        if (pistol)
            for (int k : {CC_Pistol + AP_Run, CC_Pistol + AP_Aim, CC_Pistol + AP_AimWalk})
                armed |= c.clipMap[k] >= 0 && a->clipIndex == c.clipMap[k] && !a->posing && !a->preview;
        else
            for (int k : {CC_Rifle + AP_Idle, CC_Rifle + AP_Walk, CC_Rifle + AP_Run, CC_Rifle + AP_Aim, CC_Rifle + AP_AimWalk, int(CC_RifleBack),
                          int(CC_RifleLeft), int(CC_RifleRight), int(CC_RifleFire), int(CC_Jump), int(CC_CrouchWalk)})
                armed |= c.clipMap[k] >= 0 && a->clipIndex == c.clipMap[k] && !a->posing && !a->preview;
        // recarregando correndo: a animação continua a de correr (não a de recarregar); as mãos ficam na arma (antes
        // soltavam e os braços balançavam atravessando a arma) e a esquerda vai ao pente e volta
        const bool correndo = c.clipMap[CC_Rifle + AP_Run] >= 0 && a->clipIndex == c.clipMap[CC_Rifle + AP_Run];
        const bool recarregaCorrendo = c.reloadTimer > 0.f && correndo && !pistol;
        bool want = armed && !c.dying && (c.reloadTimer <= 0.f || recarregaCorrendo || c.cartucho);
        c.supportW = want ? std::min(1.f, c.supportW + dt * 6.f) : std::max(0.f, c.supportW - dt * 6.f);
        // mirando com arma longa: a arma sobe até o olho (bochecha na coronha, coronha no ombro)
        // primeira pessoa: atirar sem mirar só aponta a arma (no ombro); mirar leva a alça de mira ao olho
        const bool running = c.clipMap[CC_Rifle + AP_Run] >= 0 && a->clipIndex == c.clipMap[CC_Rifle + AP_Run];
        bool raise = want && (c.aimHold || c.aimTimer > 0.f) && !running && (!pistol || c.firstPerson);
        bool ads = raise && (c.firstPerson ? c.aimHold : !pistol);
        c.raiseW = raise ? std::min(1.f, c.raiseW + dt * 9.f) : std::max(0.f, c.raiseW - dt * 7.f);
        c.adsW = ads ? std::min(1.f, c.adsW + dt * 7.f) : std::max(0.f, c.adsW - dt * 7.f);
        if (c.supportW <= 0.f) continue;
        int up, fo, ha, rup, rfo, rha, head;
        if (const Retarget* rt = a->retarget) {
            up = rt->targetOf[CJ_LUpper]; fo = rt->targetOf[CJ_LFore]; ha = rt->targetOf[CJ_LHand];
            rup = rt->targetOf[CJ_RUpper]; rfo = rt->targetOf[CJ_RFore]; rha = rt->targetOf[CJ_RHand]; head = rt->targetOf[CJ_Head];
        } else if (a->skeleton->jointCount == CJ_Total) {
            up = CJ_LUpper; fo = CJ_LFore; ha = CJ_LHand; rup = CJ_RUpper; rfo = CJ_RFore; rha = CJ_RHand; head = CJ_Head;
        } else continue;  // esqueleto sem humanoide reconhecido
        if (up < 0 || fo < 0 || ha < 0 || rfo < 0 || rha < 0) continue;
        glm::mat4 space = a->space ? a->space->world() : glm::mat4(1.f);
        auto world = [&](int j) { return glm::vec3(space * a->global[j][3]); };
        // A animação foi feita com as duas mãos na arma: a arma vai da palma direita (cabo) na direção da palma
        // esquerda (guarda-mão), com o topo para cima. Cada arma se acomoda pelo próprio cabo e guarda-mão.
        const float palm = 0.075f * c.height / 1.75f;
        auto palmOf = [&](int fore, int hand) {
            glm::vec3 wr = world(hand), el = world(fore);
            return wr + (glm::length(wr - el) > 1e-4f ? glm::normalize(wr - el) : glm::vec3(0.f)) * palm;
        };
        glm::vec3 R = palmOf(rfo, rha), L = palmOf(fo, ha);
        GameObject* body = e->scene.get(c.root);
        glm::vec3 fwd = body ? body->transform.rotation * glm::vec3(0.f, 0.f, 1.f) : glm::vec3(0.f, 0.f, 1.f);
        // Empunhadura de fuzil como no treino de tiro: a soleira da coronha fica no "bolso" do ombro direito (entre a
        // clavícula e o deltoide). Pronto baixo: cano 35° para baixo e um pouco para dentro. Mirando: cano para a
        // frente e a cabeça desce até o olho direito ficar atrás da alça de mira (bochecha apoiada na coronha).
        // A mão direita vai ao cabo e a esquerda ao guarda-mão (IK); pernas e tronco seguem a animação.
        const float scale = c.height / 1.75f;
        glm::vec3 left = body ? body->transform.rotation * glm::vec3(1.f, 0.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
        const glm::vec3 upW(0.f, 1.f, 0.f);
        auto basis = [](glm::vec3 x, glm::vec3 y) {
            x = glm::normalize(x);
            glm::vec3 z = glm::normalize(glm::cross(x, y));
            return glm::mat3(x, glm::cross(z, x), z);
        };
        const bool fpView = c.firstPerson && e->fpEyeValid && !c.dying;
        // pronto baixo: 35° (em primeira pessoa 20°, para a arma aparecer na diagonal embaixo da tela)
        const float down = glm::radians(fpView ? 14.f : 35.f);
        // para onde se olha (o jogador em primeira pessoa olha para cima e para baixo; os robôs, reto)
        const glm::vec3 view = fwd * std::cos(c.viewPitch) + upW * std::sin(c.viewPitch);
        const glm::vec3 viewUp = -fwd * std::sin(c.viewPitch) + upW * std::cos(c.viewPitch);
        glm::vec3 dLow = glm::normalize(view * std::cos(down) - viewUp * std::sin(down) + left * (fpView ? 0.30f : 0.18f));
        glm::vec3 d = glm::normalize(glm::mix(dLow, view, c.firstPerson ? c.raiseW : c.adsW));
        if (fpView) {  // primeira pessoa: baixa na diagonal parado, quase reta atirando sem mirar, no olho mirando
            glm::vec3 dHip = glm::normalize(view * std::cos(0.05f) - viewUp * std::sin(0.05f) + left * 0.03f);
            d = glm::normalize(glm::mix(glm::mix(dLow, dHip, c.raiseW), view, c.adsW));
        }
        glm::mat4 toSkel = glm::inverse(space);
        if (pistol && fpView) {
            // pistola em primeira pessoa: à frente do olho, um pouco à direita e abaixo; mirando, a alça de mira
            // vem para o centro. A mão direita vai ao cabo (IK) e a esquerda envolve a direita.
            glm::vec3 dp = glm::normalize(glm::mix(view * std::cos(0.15f) - viewUp * std::sin(0.15f) + left * 0.06f, view, c.adsW));
            glm::quat qp = glm::normalize(glm::quat_cast(basis(dp, upW) * glm::transpose(basis(glm::vec3(0.f, 0.f, 1.f), upW))));
            glm::quat cur = w->transform.rotation;
            if (glm::dot(cur, qp) < 0.f) qp = -qp;
            glm::vec3 sightAt = e->fpEye + (view * glm::mix(0.34f, 0.30f, c.adsW) - viewUp * glm::mix(0.10f, 0.012f, c.adsW)
                                            - left * glm::mix(0.09f, 0.f, c.adsW)) * scale;
            w->transform.position = glm::mix(w->transform.position, sightAt - qp * c.sight, c.supportW);
            w->transform.rotation = glm::normalize(glm::slerp(cur, qp, c.supportW));
            w->transform.markDirty();
            if (!w->transform.parent) w->transform.updateWorld(glm::mat4(1.f), false);
            glm::vec3 wf = glm::normalize(glm::vec3(toSkel * glm::vec4(w->transform.rotation * glm::vec3(0, 0, 1), 0.f)));
            glm::vec3 wu = glm::normalize(glm::vec3(toSkel * glm::vec4(w->transform.rotation * glm::vec3(0, 1, 0), 0.f)));
            glm::vec3 wl = glm::normalize(glm::cross(wu, wf));
            if (rup >= 0) {
                glm::vec3 g = glm::vec3(toSkel * w->transform.world() * glm::vec4(c.gripPt, 1.f));
                glm::vec3 wristR = g - wf * (0.05f * scale) - wl * (0.02f * scale) + wu * (0.035f * scale);
                glm::vec3 poleR = glm::vec3(toSkel * glm::vec4(-upW - left * 0.6f, 0.f));
                twoBoneIK(*a, rup, rfo, rha, glm::mix(glm::vec3(a->global[rha][3]), wristR, c.supportW), poleR);
                poseHand(*a, rha, false, glm::normalize(wf * 0.6f - wu * 0.6f + wl * 0.2f), glm::normalize(wu + wf * 0.3f), glm::normalize(-wu + wf * 0.3f), -1.2f, c.supportW);
            }
            glm::vec3 wr = world(rha) + (left * 0.035f - upW * 0.04f) * scale;
            glm::vec3 poleL = glm::vec3(toSkel * glm::vec4(-upW + left * 0.5f, 0.f));
            twoBoneIK(*a, up, fo, ha, glm::mix(glm::vec3(a->global[ha][3]), glm::vec3(toSkel * glm::vec4(wr, 1.f)), c.supportW), poleL);
            // mão esquerda fechada por cima da direita (dedos envolvendo os dedos da mão do cabo)
            poseHand(*a, ha, true, glm::normalize(wf * 0.3f - wl * 0.7f - wu * 0.3f), glm::normalize(wu + wf * 0.4f), wf, -1.1f, c.supportW);
            continue;
        }
        if (pistol) {  // reforço para o coice: a mão esquerda envolve o pulso direito, por baixo e do lado de dentro
            glm::vec3 lft = body ? body->transform.rotation * glm::vec3(1.f, 0.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
            glm::vec3 wr = world(rha) + (lft * 0.04f - glm::vec3(0.f, 0.045f, 0.f)) * (c.height / 1.75f);
            glm::vec3 now = glm::vec3(a->global[ha][3]);
            twoBoneIK(*a, up, fo, ha, glm::mix(now, glm::vec3(glm::inverse(space) * glm::vec4(wr, 1.f)), c.supportW));
            continue;
        }
        glm::quat q = glm::normalize(glm::quat_cast(basis(d, upW) * glm::transpose(basis(glm::vec3(0.f, 0.f, 1.f), upW))));
        glm::vec3 shoulder = rup >= 0 ? world(rup) : R;
        // (os personagens de modelo têm peito e colete de verdade: a arma 6 cm mais para a frente, senão a mão do cabo
        // entrava no corpo)
        glm::vec3 pocket = shoulder + (fwd * glm::mix(0.15f, 0.09f, c.adsW) + left * 0.06f - upW * glm::mix(0.035f, -0.03f, c.adsW)) * scale;
        {
            glm::quat cur = w->transform.rotation;
            if (glm::dot(cur, q) < 0.f) q = -q;
            glm::vec3 place = pocket - q * c.butt;
            if (fpView) {  // primeira pessoa: a arma fica num ponto fixo em relação ao olho (a câmera)
                const glm::vec3 lowAt = e->fpEye + (view * 0.32f - viewUp * 0.115f - left * 0.10f) * scale;
                const glm::vec3 hipAt = e->fpEye + (view * 0.30f - viewUp * 0.105f - left * 0.095f) * scale;
                const glm::vec3 adsAt = e->fpEye + (view * 0.26f - viewUp * 0.012f) * scale;
                place = glm::mix(glm::mix(lowAt, hipAt, c.raiseW), adsAt, c.adsW) - q * c.sight;
            }
            w->transform.position = glm::mix(w->transform.position, place, c.supportW);
            w->transform.rotation = glm::normalize(glm::slerp(cur, q, c.supportW));
            w->transform.markDirty();
            if (!w->transform.parent) w->transform.updateWorld(glm::mat4(1.f), false);
        }
        if (c.adsW > 0.f && head >= 0 && !fpView) {  // cabeça na coronha: o olho direito vai para trás da alça de mira
            glm::vec3 hp = world(head);
            glm::vec3 eye = hp + (upW * 0.075f + fwd * 0.085f - left * 0.032f) * scale;
            glm::vec3 sightW = w->transform.position + w->transform.rotation * c.sight;
            glm::vec3 want = sightW - d * (0.075f * scale) + upW * (0.012f * scale);
            glm::vec3 pivot = glm::vec3(toSkel * glm::vec4(hp, 1.f));
            glm::vec3 from = glm::normalize(glm::vec3(toSkel * glm::vec4(eye - hp, 0.f)));
            glm::vec3 to = glm::normalize(glm::vec3(toSkel * glm::vec4(want - hp, 0.f)));
            glm::quat r = glm::rotation(from, to);
            float ang = glm::angle(r);
            const float maxAng = glm::radians(28.f);
            if (ang > maxAng) r = glm::slerp(glm::quat(1.f, 0.f, 0.f, 0.f), r, maxAng / ang);
            r = glm::slerp(glm::quat(1.f, 0.f, 0.f, 0.f), r, c.adsW * c.supportW);
            rotateJointSubtree(*a, head, r, pivot);
        }
        // eixos da arma no esqueleto: frente (cano), cima e esquerda
        glm::vec3 wf = glm::normalize(glm::vec3(toSkel * glm::vec4(w->transform.rotation * glm::vec3(0, 0, 1), 0.f)));
        glm::vec3 wu = glm::normalize(glm::vec3(toSkel * glm::vec4(w->transform.rotation * glm::vec3(0, 1, 0), 0.f)));
        glm::vec3 wl = glm::normalize(glm::cross(wu, wf));
        auto onGun = [&](glm::vec3 local) { return glm::vec3(toSkel * w->transform.world() * glm::vec4(local, 1.f)); };
        if (fpView) {
            // primeira pessoa: a arma fica parada em relação ao olho, mas os ombros seguem a animação (correndo o
            // tronco balança e inclina): as mãos não alcançavam a arma e ficavam balançando soltas. Os ombros vão para
            // um ponto fixo em relação ao olho (o braço de cima não aparece na primeira pessoa) e o IK sempre fecha.
            const glm::vec3 sideW = glm::normalize(glm::cross(view, viewUp));  // direita da tela
            for (int lado = 0; lado < 2; ++lado) {
                const int j = lado ? up : rup;
                if (j < 0) continue;
                const glm::vec3 alvo = e->fpEye + (-viewUp * 0.2f + sideW * (lado ? -0.17f : 0.17f) - view * 0.06f) * scale;
                const glm::vec3 agora = glm::vec3(a->global[j][3]);
                const glm::vec3 delta = glm::mix(glm::vec3(0.f), glm::vec3(toSkel * glm::vec4(alvo, 1.f)) - agora, c.supportW);
                const Skeleton& S = *a->skeleton;
                bool in[kMaxJoints] = {};
                for (int k = 0; k < S.jointCount; ++k) {
                    const int i = S.order[k];
                    in[i] = i == j || (S.parent[i] >= 0 && in[S.parent[i]]);
                    if (!in[i]) continue;
                    a->global[i][3] += glm::vec4(delta, 0.f);
                    a->palette[i] = a->global[i] * S.inverseBind[i];
                }
            }
        }
        if (rup >= 0) {  // mão direita no cabo: palma do lado direito do cabo, dedos dando a volta pela frente
            glm::vec3 g = onGun(c.gripPt);
            glm::vec3 wristR = g - wf * (0.06f * scale) - wl * (0.035f * scale) + wu * (0.03f * scale);
            glm::vec3 nowR = glm::vec3(a->global[rha][3]);
            glm::vec3 poleR = glm::vec3(toSkel * glm::vec4(-upW - left * 0.55f - fwd * 0.1f, 0.f));  // cotovelo direito para baixo e para fora
            twoBoneIK(*a, rup, rfo, rha, glm::mix(nowR, wristR, c.supportW), poleR);
            glm::vec3 gripAxis = glm::normalize(-wu + wf * 0.3f);
            const float cr = fpView ? -1.1f : -0.95f;  // dedos fechando em volta do cabo
            poseHand(*a, rha, false, glm::normalize(wf * 0.8f + wl * 0.35f - wu * 0.35f), glm::normalize(wu + wl * 0.4f), gripAxis, cr, c.supportW);
        }
        // mão esquerda no guarda-mão: palma por baixo, dedos subindo pelo lado direito, polegar pelo lado esquerdo
        {
            glm::vec3 h = onGun(c.guardPt - glm::vec3(0.f, 0.f, c.bombaOff));  // (Doze: a mão vai junto com a bomba)
            glm::vec3 wristL = h - wu * (0.06f * scale) + wl * (0.03f * scale) - wf * (0.05f * scale);
            if (c.cartucho) {  // Doze: a mão vai até a janela de baixo, põe o cartucho e volta (a cada 0,5 s)
                const float k = std::sin(std::clamp((0.5f - c.reloadTimer) / 0.5f, 0.f, 1.f) * 3.14159265f);
                const glm::vec3 porta = onGun(glm::mix(c.gripPt, c.guardPt, 0.3f)) - wu * (0.07f * scale);
                wristL = glm::mix(wristL, porta - wu * (0.03f * scale), k);
            }
            if (recarregaCorrendo) {  // troca o pente: a mão desce até o pente e volta (no último 1,2 s da recarga)
                const float k = c.reloadTimer >= 1.2f ? 0.f : std::sin(c.reloadTimer / 1.2f * 3.14159265f);
                const glm::vec3 pente = onGun(glm::mix(c.gripPt, c.guardPt, 0.45f)) - wu * (0.09f * scale);
                wristL = glm::mix(wristL, pente - wu * (0.04f * scale) - wf * (0.03f * scale), k);
            }
            glm::vec3 nowL = glm::vec3(a->global[ha][3]);
            glm::vec3 poleL = glm::vec3(toSkel * glm::vec4(-upW + left * 0.35f, 0.f));  // cotovelo esquerdo por baixo da arma
            twoBoneIK(*a, up, fo, ha, glm::mix(nowL, wristL, c.supportW), poleL);
            const float cl = fpView ? -1.0f : -1.15f;  // dedos subindo pelo lado do guarda-mão (de perto: bem fechados)
            // dedos por baixo do guarda-mão, mas sem quebrar o pulso: na terceira pessoa a direção dos dedos puxa
            // para a do antebraço (a mão saía torta, dobrada para o lado)
            glm::vec3 dedosL = glm::normalize(wf * 0.5f - wl * 0.6f + wu * 0.25f);
            if (!fpView) {
                const glm::vec3 ant = glm::vec3(a->global[ha][3]) - glm::vec3(a->global[fo][3]);
                if (glm::length(ant) > 1e-4f) dedosL = glm::normalize(glm::mix(dedosL, glm::normalize(ant), 0.45f));
            }
            poseHand(*a, ha, true, dedosL, glm::normalize(wf + wu * 0.5f), wf, cl, c.supportW);
        }
    }
}

// ---------------- primeira pessoa ----------------
static int headJointOf(const Animator& a) {
    if (const Retarget* rt = a.retarget) return rt->targetOf[CJ_Head];
    return a.skeleton && a.skeleton->jointCount == CJ_Total ? int(CJ_Head) : -1;
}

// Olho do jogador: um pouco acima e à frente do osso da cabeça (pose da animação, antes do IK das mãos).
// A altura é suavizada (o balanço da corrida não sacode a tela); na horizontal acompanha o corpo sem atraso.
void updateFirstPersonEye(Engine* e, float dt) {
    if (!e->playing || !e->firstPerson || e->player < 0 || e->player >= int(e->characters.size())) { e->fpEyeValid = false; return; }
    CharacterInfo& c = e->characters[e->player];
    GameObject* body = e->scene.get(c.root);
    Animator* a = c.anim;
    int head = a && a->skeleton ? headJointOf(*a) : -1;
    if (!body || c.dying) { e->fpEyeValid = false; return; }
    const float scale = c.height / 1.75f;
    glm::vec3 fwd = body->transform.rotation * glm::vec3(0.f, 0.f, 1.f);
    glm::vec3 eye;
    if (head >= 0) {
        glm::mat4 space = a->space ? a->space->world() : glm::mat4(1.f);
        eye = glm::vec3(space * a->global[head][3]) + (glm::vec3(0.f, 0.085f, 0.f) + fwd * 0.11f) * scale;
    } else {
        eye = body->transform.position + glm::vec3(0.f, c.height * (c.crouch ? 0.62f : 0.93f), 0.f) + fwd * 0.1f;
    }
    if (c.vaultT >= 0.f) {  // pulando janela: o olho passa pelo meio do vão (abaixa junto com o corpo encolhido)
        const float t = c.vaultT, bump = std::sin(3.14159265f * std::clamp(t, 0.f, 1.f));
        eye = body->transform.position + glm::vec3(0.f, glm::mix(1.62f, 0.95f, bump) * scale, 0.f) + c.vaultDir * 0.05f;
        e->fpEye = eye;
        e->fpEyeValid = true;
        return;
    }
    if (e->fpEyeValid) eye.y = glm::mix(e->fpEye.y, eye.y, 1.f - std::exp(-14.f * dt));
    // O olho nunca encosta em parede, teto ou chão: com a tela (plano de perto) dentro da parede dava para ver
    // dentro das casas e por baixo da terra (e achar quem está lá dentro no online). Folga de 12 cm em volta.
    if (body->rigidBody && body->rigidBody->body) {
        const btRigidBody* self = body->rigidBody->body;
        const float m = 0.12f;
        glm::vec3 hit; GameObject* o = nullptr; float fr = 1.f;
        const glm::vec3 axis(body->transform.position.x, eye.y, body->transform.position.z);
        const glm::vec3 d = eye - axis;
        const float L = glm::length(d);
        if (L > 1e-3f && e->physics.raycastIgnoring(axis, axis + d / L * (L + m), self, hit, o, fr))
            eye = axis + d / L * std::max(0.f, fr * (L + m) - m);  // do eixo do corpo até o olho: para antes da parede
        glm::vec3 view = e->editorCam.forward();
        glm::vec3 hv = glm::length(glm::vec2(view.x, view.z)) > 1e-3f ? glm::normalize(glm::vec3(view.x, 0.f, view.z)) : fwd;
        const glm::vec3 side = glm::normalize(glm::cross(hv, glm::vec3(0.f, 1.f, 0.f)));
        for (glm::vec3 dir : {view, hv, -hv, side, -side, glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, -1.f, 0.f)})
            if (e->physics.raycastIgnoring(eye, eye + dir * m, self, hit, o, fr)) eye -= dir * (m - fr * m);
    }
    e->fpEye = eye;
    e->fpEyeValid = true;
}

// Câmera no olho olhando para onde o dedo apontou; a cabeça (com capacete/boné) some para não tapar a vista.
// Mirando, o campo de visão fecha um pouco (sniper: luneta, bem mais).
void applyFirstPerson(Engine* e) {
    // arma escondida enquanto se olha pela luneta (volta ao sair da mira)
    auto showWeapon = [&](CharacterInfo* c, bool on) {
        if (!c) return;
        if (GameObject* w = e->scene.get(c->weapon))
            if (w->active != on) Scene::forEachInSubtree(*w, [&](GameObject& o) { o.active = on; });
    };
    CharacterInfo* pc = e->player >= 0 && e->player < int(e->characters.size()) ? &e->characters[e->player] : nullptr;
    // a arma guardada nas costas/coxa do próprio jogador não aparece na primeira pessoa (os outros veem)
    // o cabelo 3D também (ficaria na frente da câmera)
    if (pc)
        for (Scene::Handle hh : {pc->holster, pc->hair})
            if (GameObject* hw = e->scene.get(hh)) {
                const bool show = !(e->firstPerson && e->playing && e->fpEyeValid);
                if (hw->active != show) Scene::forEachInSubtree(*hw, [&](GameObject& o) { o.active = show; });
            }
    if (!e->firstPerson || !e->playing || !e->fpEyeValid || !pc) {
        e->camera.fovYDeg = e->baseFov;
        if (e->fpScoped) { showWeapon(pc, true); e->fpScoped = false; }
        return;
    }
    CharacterInfo& c = *pc;
    glm::vec3 view = e->editorCam.forward();
    const bool scoped = c.scope && c.adsW > 0.85f;
    if (scoped != e->fpScoped) { showWeapon(&c, !scoped); e->fpScoped = scoped; }
    if (scoped) {  // luneta: a mira balança devagar (respiração), e o tiro vai junto
        const float t = e->renderer.time;
        glm::vec3 rt = glm::normalize(glm::cross(view, glm::vec3(0, 1, 0)));
        glm::vec3 up = glm::cross(rt, view);
        view = glm::normalize(view + rt * (0.0045f * std::sin(t * 0.9f)) + up * (0.0035f * std::sin(t * 1.4f + 0.7f)));
    }
    static const bool outside = std::getenv("MOTOR_FP_DE_FORA") != nullptr;  // testes: vê a pose de fora
    if (outside) {
        e->camera.lookAt(e->fpEye + glm::vec3(1.1f, 0.15f, 0.45f), e->fpEye + view * 0.3f - glm::vec3(0, 0.15f, 0));
        return;
    }
    e->camera.lookAt(e->fpEye, e->fpEye + view);
    e->camera.nearZ = 0.035f;
    // luneta: mirou, um zoom pequeno; os botões + e - aproximam (3 níveis). Sem luneta: o zoom leve da mira
    static const float kScope[3] = {0.6f, 0.42f, 0.28f};
    const float zoom = c.scope ? kScope[std::clamp(e->scopeZoom, 0, 2)] : 0.72f;
    e->camera.fovYDeg = e->baseFov * glm::mix(1.f, zoom, c.adsW);
    e->editorCam.turnSpeed = 0.0042f * e->camera.fovYDeg / e->baseFov;  // com zoom, o dedo gira menos
    // a cabeça (com capacete/boné) some para não tapar a vista; na luneta, os braços também
    if (Animator* a = c.anim; a && a->skeleton) {
        int head = headJointOf(*a);
        if (head < 0) return;
        const Skeleton& S = *a->skeleton;
        int arms[2] = {-1, -1};
        if (scoped) {
            arms[0] = a->retarget ? a->retarget->targetOf[CJ_LUpper] : int(CJ_LUpper);
            arms[1] = a->retarget ? a->retarget->targetOf[CJ_RUpper] : int(CJ_RUpper);
        }
        for (int root : {head, arms[0], arms[1]}) {
            if (root < 0) continue;
            glm::vec3 hp = glm::vec3(a->global[root][3]);
            glm::mat4 M = glm::translate(glm::mat4(1.f), hp) * glm::scale(glm::mat4(1.f), glm::vec3(1e-3f)) * glm::translate(glm::mat4(1.f), -hp);
            bool in[kMaxJoints] = {};
            for (int k = 0; k < S.jointCount; ++k) {
                int i = S.order[k];
                in[i] = i == root || (S.parent[i] >= 0 && in[S.parent[i]]);
                if (in[i]) a->palette[i] = M * a->global[i] * S.inverseBind[i];
            }
        }
        // boneco em bloco (esqueleto do motor): a parte de cima do braço, grossa e colada na câmera, também some;
        // ficam o antebraço, a mão e a arma (como nos jogos de tiro)
        if (!a->retarget && S.jointCount == CJ_Total) {
            auto hide = [&](int j) {
                glm::vec3 hp = glm::vec3(a->global[j][3]);
                glm::mat4 M = glm::translate(glm::mat4(1.f), hp) * glm::scale(glm::mat4(1.f), glm::vec3(1e-3f)) * glm::translate(glm::mat4(1.f), -hp);
                a->palette[j] = M * a->global[j] * S.inverseBind[j];
            };
            for (int up : {int(CJ_LUpper), int(CJ_RUpper)}) hide(up);
            // no ar (pulo, janela) as pernas encolhem e o tronco abaixa: sairiam na frente da câmera
            if (c.airborne || c.vaultT >= 0.f)
                for (int j : {int(CJ_Hips), int(CJ_Spine), int(CJ_Chest), int(CJ_Neck), int(CJ_LThigh), int(CJ_LShin), int(CJ_LFoot),
                              int(CJ_RThigh), int(CJ_RShin), int(CJ_RFoot)}) hide(j);
        }
    }
}

// ---------------- jogador ----------------
// Mira de terceira pessoa: um pouco acima do centro da câmera (a câmera fica inclinada para baixo)
static glm::vec3 aimDirection(Engine* e) {
    glm::vec3 f = glm::normalize(e->camera.rotation * glm::vec3(0, 0, -1));
    if (e->firstPerson) return f;  // primeira pessoa: o tiro vai para o centro da tela
    return glm::normalize(f + glm::vec3(0.f, 0.28f, 0.f));
}

// Tiro com arma da partida: dano da tabela (cabeça 2,5x), queda com a distância, bagos da escopeta, dispersão
// menor mirando (sniper sem mirar é bem imprecisa)
// Doze: depois do tiro a bomba vai para trás e volta (~0,46 s, começando 0,12 s depois do tiro), com o "tec-tec"
constexpr float kBombaTotal = 0.62f, kBombaEspera = 0.12f, kBombaCurso = 0.085f;
void comecarBomba(CharacterInfo& c) {
    if (c.bombaObjs.empty()) return;
    c.bombaT = kBombaTotal;
    c.bombaSom = false;
}
void atualizarBombas(Engine* e, float dt) {
    for (CharacterInfo& c : e->characters) {
        if (c.bombaObjs.empty() || (c.bombaT <= 0.f && c.bombaOff == 0.f)) continue;
        c.bombaT = std::max(0.f, c.bombaT - dt);
        const float passou = kBombaTotal - c.bombaT;
        float off = 0.f;
        if (c.bombaT > 0.f && passou >= kBombaEspera) {
            const float p = std::clamp((passou - kBombaEspera) / (kBombaTotal - kBombaEspera), 0.f, 1.f);
            off = kBombaCurso * std::sin(p * 3.14159265f);
            if (!c.bombaSom) {
                c.bombaSom = true;
                if (GameObject* g = e->scene.get(c.root)) playSound(e, "bomba", g->transform.position, 0.75f);
            }
        }
        c.bombaOff = off;
        for (size_t i = 0; i < c.bombaObjs.size(); ++i)
            if (GameObject* o = e->scene.get(c.bombaObjs[i])) o->transform.setPosition(c.bombaBase[i] - glm::vec3(0.f, 0.f, off));
    }
}

static void fireGun(Engine* e, CharacterInfo& c, GameObject& root, btRigidBody* self) {
    glm::vec3 aim = aimDirection(e), from = e->camera.position;
    if (!e->firstPerson) {
        glm::quat q = glm::angleAxis(std::atan2(aim.x, aim.z), glm::vec3(0, 1, 0));
        btTransform t = self->getWorldTransform();
        t.setRotation({q.x, q.y, q.z, q.w});
        self->setWorldTransform(t);
        root.transform.setRotation(q);
    }
    e->fireCooldown = c.gunInterval;
    c.aimTimer = 1.5f;
    ++c.shots;
    float spread = c.gunSpread * (c.aimHold ? 0.1f : 1.f);  // mirando: a bala vai praticamente na bolinha da mira
    if (c.weaponKind == WK_Sniper && !c.aimHold) spread = 0.06f;
    if (c.gunPellets > 1) spread = c.gunSpread;
    glm::vec3 muzzle = from + aim;
    if (GameObject* w = e->scene.get(c.weapon)) muzzle = glm::vec3(w->transform.world() * glm::vec4(c.muzzle, 1.f));
    if (c.silenciada) playSound(e, "tiro_silenciado", muzzle, 0.9f);  // silenciador: "pff" baixo e sem clarão
    else { playSound(e, c.shotSound.empty() ? "tiro_m4" : c.shotSound.c_str(), muzzle, 1.f); muzzleFx(e, c.weapon, muzzle, aim); }
    for (int k = 0; k < c.gunPellets; ++k) {
        glm::vec3 j(e->fxRng.range(-1.f, 1.f), e->fxRng.range(-1.f, 1.f), e->fxRng.range(-1.f, 1.f));
        glm::vec3 dir = glm::normalize(aim + j * spread);
        glm::vec3 hit; GameObject* obj = nullptr; float frac = 1.f;
        bool any = e->physics.raycastIgnoring(from, from + dir * 300.f, self, hit, obj, frac);
        glm::vec3 end = any ? hit : from + dir * 300.f;
        bulletFx(e, c.weapon, muzzle, end);
        if (!any) continue;
        impactFx(e, c.weapon, hit, dir);
        if (k == 0) playSound(e, "impacto", end, 0.5f);
        if (!obj) continue;
        float dist = glm::length(hit - from);
        float dmg = c.gunDmg * (dist > c.gunFalloff ? std::max(0.3f, 1.f - 0.7f * (dist - c.gunFalloff) / (1.5f * c.gunFalloff)) : 1.f);
        GameObject* r = rootOf(e, obj);
        for (const CharacterInfo& o : e->characters)
            if (r && e->scene.get(o.root) == r && &o != &c) {
                bool head = hit.y > r->transform.position.y + o.height * 0.83f;
                if (head) dmg *= 2.5f;
                e->hitMarker = 0.25f; e->hitHead = head;
            }
        damageAt(e, obj, dmg, from);
    }
    comecarBomba(c);  // Doze: bombeia depois do tiro
}

static void fire(Engine* e, CharacterInfo& c, GameObject& root, btRigidBody* self) {
    int kind = c.weaponKind;
    bool melee = !isGun(kind);
    if (!melee && c.gunDmg > 0.f) { fireGun(e, c, root, self); return; }
    glm::vec3 camFwd = aimDirection(e);
    glm::vec3 from, dir;
    float range, power, cooldown;
    if (melee) {
        glm::vec3 fwd = root.transform.rotation * glm::vec3(0, 0, 1);
        from = root.transform.position + glm::vec3(0, c.height * 0.7f, 0);
        dir = glm::normalize(glm::vec3(fwd.x, 0.f, fwd.z));
        range = 1.9f; power = kind == WK_Bat ? 9.f : 5.f; cooldown = 0.45f;
    } else {
        from = e->camera.position;
        dir = camFwd;
        range = 300.f;
        power = kind == WK_Pistol ? 6.f : kind == WK_Rifle ? 8.f : kind == WK_Shotgun ? 14.f : 22.f;
        cooldown = kind == WK_Pistol ? 0.25f : kind == WK_Rifle ? 0.1f : kind == WK_Shotgun ? 0.75f : 1.1f;
        // vira o personagem para onde a câmera mira
        float yaw = std::atan2(dir.x, dir.z);
        glm::quat q = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
        btTransform t = self->getWorldTransform();
        t.setRotation({q.x, q.y, q.z, q.w});
        self->setWorldTransform(t);
        root.transform.setRotation(q);
    }
    e->fireCooldown = cooldown;
    c.aimTimer = melee ? 0.45f : 1.5f;
    if (melee && c.anim) {  // golpe recomeça a cada ataque
        int base = armedClipBase(kind);
        if (base >= 0 && c.clipMap[base + AP_Aim] >= 0) c.anim->setClip(c.clipMap[base + AP_Aim]);
    }
    glm::vec3 hit; GameObject* obj = nullptr; float frac = 1.f;
    bool any = e->physics.raycastIgnoring(from, from + dir * range, self, hit, obj, frac);
    glm::vec3 end = any ? hit : from + dir * range;
    if (melee) playSound(e, c.shotSound.empty() ? "golpe_faca" : c.shotSound.c_str(), from, 0.8f);
    if (!melee) {
        glm::vec3 muzzle = end;
        if (GameObject* w = e->scene.get(c.weapon)) muzzle = glm::vec3(w->transform.world() * glm::vec4(c.muzzle, 1.f));
        glm::vec3 shotDir = glm::normalize(end - muzzle);
        muzzleFx(e, c.weapon, muzzle, shotDir);
        playSound(e, c.shotSound.empty() ? "tiro_m4" : c.shotSound.c_str(), muzzle, 1.f);
        if (any) playSound(e, "impacto", end, 0.5f);
        int pellets = kind == WK_Shotgun ? 6 : 1;
        for (int k = 0; k < pellets; ++k) {
            glm::vec3 jitter = k ? glm::vec3(e->fxRng.range(-1.f, 1.f), e->fxRng.range(-1.f, 1.f), e->fxRng.range(-1.f, 1.f)) * 0.04f * glm::length(end - muzzle) : glm::vec3(0.f);
            bulletFx(e, c.weapon, muzzle, end + jitter);
        }
        if (any) impactFx(e, c.weapon, hit, dir);
    } else if (any) {
        impactFx(e, c.weapon, hit, dir);
    }
    if (any && obj) {  // dano em inimigos / objetos com vida
        static const float dmg[] = {25.f, 20.f, 60.f, 100.f, 35.f, 30.f};
        damageAt(e, obj, dmg[std::clamp(kind, 0, 5)], from);
    }
    if (any && obj && obj->rigidBody && obj->rigidBody->mass > 0.f) {
        RigidBody* rb = obj->rigidBody;
        glm::vec3 imp = dir * power * rb->mass * 0.4f;
        rb->body->activate(true);
        btVector3 rel = btVector3(hit.x, hit.y, hit.z) - rb->body->getCenterOfMassPosition();
        rb->body->applyImpulse({imp.x, imp.y, imp.z}, rel);
    }
}

// Portas: abrem quando um personagem chega perto (no Play); no editor ficam fechadas
void updateDoors(Engine* e, float dt) {
    for (DoorRT& d : e->doors) {
        GameObject* o = e->scene.get(d.h);
        if (!o) continue;
        float target = 0.f;
        if (e->playing && d.tiroTempo > 0.f) {  // levou tiro: aberta para o lado da bala por um tempo
            d.tiroTempo -= dt;
            target = 1.65f * d.tiroLado;
        } else if (e->playing) {
            // a folha gira para -n (ângulo positivo) ou para +n (negativo); n = frente da porta (eixo Z dela)
            const glm::vec3 n = d.base * glm::vec3(0, 0, 1);
            glm::vec3 c = glm::vec3(o->transform.world() * glm::vec4(0.5f, 0.5f, 0.f, 1.f));
            for (const CharacterInfo& ch : e->characters) {
                GameObject* g = e->scene.get(ch.root);
                if (!g || !g->active) continue;
                glm::vec3 p = g->transform.position;
                if (glm::length(glm::vec2(p.x - c.x, p.z - c.z)) < 1.6f + d.w * 0.5f && std::abs(p.y + ch.height * 0.5f - c.y) < 2.2f) {
                    // já aberta: continua para o mesmo lado; fechada: abre para longe de quem chega
                    const float lado = std::abs(d.angle) > 0.05f ? (d.angle > 0.f ? 1.f : -1.f)
                                                                 : (glm::dot(p - c, n) >= 0.f ? 1.f : -1.f);
                    target = 1.65f * lado;
                    break;
                }
            }
        }
        float step = 3.5f * dt;
        float na = d.angle + std::clamp(target - d.angle, -step, step);
        if (e->playing ? std::abs(na - d.angle) > 1e-5f : d.angle != 0.f) {
            d.angle = e->playing ? na : 0.f;
            o->transform.setRotation(d.base * glm::angleAxis(d.angle, glm::vec3(0, 1, 0)));
        }
    }
}

// Tiro que passa por uma porta (a folha não tem colisão: o teste é contra a caixa dela): a porta abre para o lado
// para onde a bala ia. De fora da casa abre para dentro; de dentro, para fora.
bool tiroNasPortas(Engine* e, glm::vec3 de, glm::vec3 ate) {
    if (!e->playing) return false;
    bool alguma = false;
    for (DoorRT& d : e->doors) {
        GameObject* o = e->scene.get(d.h);
        if (!o) continue;
        const glm::mat4 W = o->transform.world();
        const glm::vec3 centro = glm::vec3(W * glm::vec4(0.5f, 0.5f, 0.f, 1.f));
        // longe da linha do tiro: nem testa (a maioria das portas)
        const glm::vec3 seg = ate - de;
        const float L2 = glm::dot(seg, seg);
        const float t0 = L2 > 1e-6f ? std::clamp(glm::dot(centro - de, seg) / L2, 0.f, 1.f) : 0.f;
        if (glm::length(de + seg * t0 - centro) > 2.5f) continue;
        // raio na caixa da folha (local: [0,1] x [0,1] x [-0,5; 0,5])
        const glm::mat4 inv = glm::inverse(W);
        const glm::vec3 a = glm::vec3(inv * glm::vec4(de, 1.f)), b = glm::vec3(inv * glm::vec4(ate, 1.f)), v = b - a;
        float tmin = 0.f, tmax = 1.f;
        const glm::vec3 lo(0.f, 0.f, -0.5f), hi(1.f, 1.f, 0.5f);
        bool dentro = true;
        for (int k = 0; k < 3 && dentro; ++k) {
            if (std::abs(v[k]) < 1e-8f) { if (a[k] < lo[k] || a[k] > hi[k]) dentro = false; continue; }
            float t1 = (lo[k] - a[k]) / v[k], t2 = (hi[k] - a[k]) / v[k];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
            if (tmin > tmax) dentro = false;
        }
        if (!dentro) continue;
        const glm::vec3 n = d.base * glm::vec3(0, 0, 1);
        d.tiroLado = glm::dot(seg, n) < 0.f ? 1.f : -1.f;  // bala indo para -n: a folha vai para -n (ângulo positivo)
        d.tiroTempo = 8.f;
        alguma = true;
    }
    return alguma;
}

// Elevador: perto de uma ponta aparece o botão que leva à outra
std::string interactLabel(Engine* e, int* which) {
    if (!e->playing || e->player < 0 || e->player >= int(e->characters.size())) return "";
    GameObject* pg = e->scene.get(e->characters[e->player].root);
    if (!pg) return "";
    glm::vec3 p = pg->transform.position;
    if (e->module) if (std::string l = e->module->interactLabel(e); !l.empty()) { if (which) *which = -2; return l; }  // o jogo (ex.: loja)
    for (size_t i = 0; i < e->teles.size(); ++i) {
        const TeleRT& t = e->teles[i];
        GameObject* r = e->scene.get(t.root);
        if (!r) continue;
        glm::mat4 W = r->transform.world();
        for (int end = 0; end < 2; ++end) {
            glm::vec3 q = glm::vec3(W * glm::vec4(end ? t.b : t.a, 1.f));
            if (glm::length(glm::vec2(p.x - q.x, p.z - q.z)) < 1.8f && std::abs(p.y - q.y) < 2.f) {
                if (which) *which = int(i) * 2 + end;
                return end ? t.lb : t.la;
            }
        }
    }
    return "";
}

bool interact(Engine* e) {
    if (e->module && e->module->onInteract(e)) return true;  // o jogo (ex.: loja)
    int which = -1;
    if (interactLabel(e, &which).empty() || which < 0) return false;
    const TeleRT& t = e->teles[which / 2];
    GameObject* r = e->scene.get(t.root);
    CharacterInfo& c = e->characters[e->player];
    GameObject* pg = e->scene.get(c.root);
    if (!r || !pg || !pg->rigidBody) return false;
    glm::vec3 dst = glm::vec3(r->transform.world() * glm::vec4(which % 2 ? t.a : t.b, 1.f));
    pg->transform.setPosition(dst + glm::vec3(0, 0.05f, 0));
    e->physics.syncFromTransform(*pg->rigidBody);
    e->editorCam.target = dst + glm::vec3(0, c.height * 0.85f, 0);
    return true;
}

// Escolhe a animação pela velocidade (parado, andando, correndo, de lado, de costas, agachado, pulando, recarregando)
void animateCharacter(CharacterInfo& c, const glm::quat& rot, glm::vec3 v, bool grounded, bool airborne) {
    if (c.anim && !c.dying) {
        float hs = std::sqrt(v.x * v.x + v.z * v.z);
        int base = armedClipBase(c.weaponKind);
        bool aiming = c.aimTimer > 0.f || c.aimHold;
        int clip = hs < 0.4f ? CC_Idle : hs < 4.f ? CC_Walk : CC_Run;
        if (base >= 0)
            clip = base + (hs < 0.4f ? (aiming ? AP_Aim : AP_Idle) : (aiming ? AP_AimWalk : hs < 4.f ? AP_Walk : AP_Run));
        if (base == CC_Rifle) {
            // mirando com fuzil: anda de costas e de lado olhando para o alvo; parado, o tiro tem coice
            glm::vec3 local = glm::inverse(rot) * glm::vec3(v.x, 0.f, v.z);
            if (aiming && hs >= 0.4f) {
                if (local.z < -0.5f * hs) clip = CC_RifleBack;
                else if (std::abs(local.x) > std::abs(local.z)) clip = local.x > 0.f ? CC_RifleLeft : CC_RifleRight;
            }
            if (aiming && hs < 0.4f && c.fireAnim > 0.f) clip = CC_RifleFire;
            if (c.reloadTimer > 0.f && hs < 4.f && !c.cartucho) clip = CC_Reload;  // (Doze: cartucho a cartucho é só a mão)
            if (c.crouch && c.reloadTimer <= 0.f) clip = CC_CrouchWalk;  // agachado: parado, andando e mirando
        }
        // Pulo (clip de 0,9 s): 0 a 0,1 s impulso, 0,1 a 0,53 s no ar, depois a aterrissagem. No ar a pose segura no fim
        // do trecho do ar; ao tocar o chão toca a aterrissagem rápida (só parado ou andando) e volta misturando.
        constexpr float kJumpAirEnd = 0.52f, kLandFrom = 0.56f, kLandTo = 0.78f;
        const int jumpClip = c.clipMap[CC_Jump];
        if (airborne && jumpClip >= 0) {
            if (!c.wasAir || c.anim->clipIndex != jumpClip) {
                c.anim->setClip(jumpClip, 0.08f);
                c.anim->time = c.jumpHold > 0.f ? 0.08f : 0.3f;  // pulando: desde o impulso; caindo de um degrau: já no ar
            }
            c.anim->loop = false;
            c.anim->speed = 1.25f;
            if (c.anim->time > kJumpAirEnd) c.anim->time = kJumpAirEnd;
            c.wasAir = true;
            c.landing = false;
            return;
        }
        if (c.wasAir && jumpClip >= 0 && c.anim->clipIndex == jumpClip && hs < 4.f) {  // acabou de tocar o chão
            c.landing = true;
            c.anim->time = std::max(c.anim->time, kLandFrom);
        }
        c.wasAir = false;
        if (c.landing && c.anim->clipIndex == jumpClip && c.anim->time < kLandTo && hs < 4.f) {
            c.anim->loop = false;
            c.anim->speed = 2.2f;
            return;
        }
        c.landing = false;
        int mapped = c.clipMap[clip];
        if (mapped < 0 && clip >= CC_Death) mapped = c.clipMap[base >= 0 ? base + AP_Aim : CC_Idle];  // modelo sem essa animação
        if (mapped >= 0 && c.anim->clipIndex != mapped)
            c.anim->setClip(mapped, c.anim->clipIndex == jumpClip ? 0.16f : 0.2f);  // troca misturada (sem estalo)
        c.anim->loop = true;
        c.anim->speed = clip == CC_CrouchWalk ? (hs < 0.3f ? 0.f : std::clamp(hs / 1.6f, 0.6f, 1.4f))
                      : clip == CC_Reload ? 1.4f : clip == CC_Jump || clip == CC_RifleFire ? 1.f
                      : hs < 0.4f ? 1.f : hs < 4.f ? std::clamp(hs / 2.6f, 0.6f, 1.6f) : std::clamp(hs / 6.5f, 0.7f, 1.4f);
    }
}

// Move um personagem (jogador ou inimigo): velocidade horizontal, pulo, virar para o movimento e animação
bool driveCharacter(Engine* e, CharacterInfo& c, glm::vec3 move, float speed, bool jump, float dt, glm::vec3 face) {
    GameObject* go = e->scene.get(c.root);
    if (!go || !go->rigidBody || !go->rigidBody->body) return false;
    btRigidBody* b = go->rigidBody->body;
    float mag = std::min(1.f, glm::length(move));
    if (mag > 1e-3f) move = glm::normalize(move);
    btVector3 vel = b->getLinearVelocity();
    glm::vec3 center = go->transform.position + glm::vec3(0, c.height * 0.5f, 0);
    glm::vec3 hit; GameObject* obj = nullptr; float frac;
    bool grounded = e->physics.raycastIgnoring(center, center - glm::vec3(0, c.height * 0.5f + 0.25f, 0), b, hit, obj, frac);
    btVector3 nv(move.x * speed * mag, vel.y(), move.z * speed * mag);
    if (jump && grounded) { nv.setY(6.f); c.jumpHold = 0.18f; }  // ~85 cm de altura, ~0,52 s no ar (gravidade abaixo)
    c.jumpHold = std::max(0.f, c.jumpHold - dt);
    // Degrau, batente ou quina no caminho: quando o boneco trava, olha o chão um pouco à frente dos pés; se estiver até
    // 45 cm mais alto (e não for parede na altura do joelho), sobe junto. Assim anda liso nas escadarias e becos.
    const float hvAgora = std::sqrt(float(vel.x() * vel.x() + vel.z() * vel.z()));
    if (mag > 0.05f && !jump && (grounded || vel.y() < 1.f) && hvAgora < speed * mag * 0.6f) {  // só quando travou
        const float r = 0.19f * c.height / 1.75f;
        const glm::vec3 feet = go->transform.position;
        const glm::vec3 knee = feet + glm::vec3(0, 0.55f * c.height / 1.75f, 0);
        glm::vec3 h2; GameObject* o2 = nullptr; float f2;
        const bool wall = e->physics.raycastIgnoring(knee, knee + move * (r + 0.2f), b, h2, o2, f2);
        if (!wall) {
            const glm::vec3 ahead = feet + move * (r + 0.14f);
            glm::vec3 h3; GameObject* o3 = nullptr; float f3;
            if (e->physics.raycastIgnoring(ahead + glm::vec3(0, 0.5f, 0), ahead - glm::vec3(0, 0.15f, 0), b, h3, o3, f3)) {
                const float dh = h3.y - feet.y;
                if (dh > 0.03f && dh < 0.46f) nv.setY(std::max(float(nv.y()), std::min(dh / 0.09f, 4.5f)));
            }
        }
    }
    // atrito só parado (segura na rampa); andando, escorrega pelas quinas e paredes em vez de grudar
    b->setFriction(mag > 0.05f ? 0.f : 0.4f);
    // parado no chão (rampa, escada): sem gravidade, senão escorrega para baixo
    const bool anchor = grounded && !jump && mag < 0.05f && vel.y() < 0.5f && frac * (c.height * 0.5f + 0.25f) < c.height * 0.5f + 0.08f;
    // no ar, gravidade forte (com a normal o boneco parecia flutuar); descendo, mais forte ainda: chega rápido no chão
    const float gAr = vel.y() > 0.f ? -21.f : -32.f;
    b->setGravity(anchor ? btVector3(0, 0, 0) : btVector3(0, grounded && c.jumpHold <= 0.f ? -9.81f : gAr, 0));
    if (anchor) nv.setY(0.f);
    b->setLinearVelocity(nv);
    b->activate(true);
    c.aimTimer = std::max(0.f, c.aimTimer - dt);
    glm::vec3 look = glm::length(glm::vec2(face.x, face.z)) > 1e-3f ? face : mag > 0.05f ? move : glm::vec3(0.f);
    if (glm::length(glm::vec2(look.x, look.z)) > 1e-3f) {  // mirando: olha para o alvo mesmo andando de lado
        glm::quat target = glm::angleAxis(std::atan2(look.x, look.z), glm::vec3(0, 1, 0));
        glm::quat q = glm::slerp(go->transform.rotation, target, 1.f - std::exp(-12.f * dt));
        btTransform t = b->getWorldTransform();
        t.setRotation({q.x, q.y, q.z, q.w});
        b->setWorldTransform(t);
        go->transform.setRotation(q);
    }
    c.fireAnim = std::max(0.f, c.fireAnim - dt);
    c.reloadTimer = std::max(0.f, c.reloadTimer - dt);
    // no ar: logo depois de pular (o chão ainda está perto do pé) ou longe do chão; caindo de degrau pequeno não conta
    c.airborne = c.jumpHold > 0.f || (!grounded && (c.wasAir || std::abs(float(vel.y())) > 1.5f));
    animateCharacter(c, go->transform.rotation, glm::vec3(nv.x(), nv.y(), nv.z()), grounded, c.airborne);
    return grounded;
}

void playDeath(CharacterInfo& c) {
    c.dying = true;
    if (!c.anim) return;
    int d = c.clipMap[CC_Death];
    if (d >= 0) { c.anim->setClip(d); c.anim->loop = false; c.anim->speed = 1.f; }
    else c.anim->speed = 0.f;
}

// Pular janela (ou mureta): pulando de frente para uma parede com um vão em cima (peitoril de 45 cm a 1,35 m acima
// do pé, vão livre por cima) e chão do outro lado, o boneco passa encolhido por cima do peitoril e cai do outro lado.
// Durante o pulo a cápsula não bate em nada (a janela é mais baixa que o boneco) e o caminho é conduzido aqui.
// alcance: até onde a parede pode estar; exigeVerga: só vale janela (parede de novo acima do vão), não mureta de laje
static bool tryVault(Engine* e, CharacterInfo& c, GameObject& go, btRigidBody* b, glm::vec3 f, float alcance = 0.85f,
                     bool exigeVerga = false) {
    f.y = 0.f;
    if (glm::length(f) < 1e-3f) return false;
    f = glm::normalize(f);
    const float sc = c.height / 1.75f;
    const glm::vec3 up(0.f, 1.f, 0.f), feet = go.transform.position;
    glm::vec3 hit; GameObject* o = nullptr; float fr = 1.f;
    auto ray = [&](glm::vec3 a, glm::vec3 z) { return e->physics.raycastIgnoring(a, z, b, hit, o, fr); };
    // 1) parede bem na frente, na altura da cintura
    const glm::vec3 w0 = feet + up * (0.5f * sc);
    if (!ray(w0, w0 + f * alcance)) return false;
    const float dWall = alcance * fr;
    // 2) vão livre por cima do peitoril (duas alturas)
    for (float y : {1.3f, 1.6f}) {
        const glm::vec3 p = feet + up * (y * sc);
        if (ray(p, p + f * (dWall + 0.7f))) return false;
    }
    // 3) altura do peitoril (de cima para baixo, logo depois da face da parede)
    const glm::vec3 top = feet + f * (dWall + 0.05f) + up * (1.9f * sc);
    if (!ray(top, top - up * (1.85f * sc))) return false;
    const float sillY = hit.y, rel = sillY - feet.y;
    if (rel < 0.45f * sc || rel > 1.35f * sc) return false;
    if (exigeVerga) {  // janela: em cima do vão a parede continua (a ~2,1 m do chão da casa)
        const glm::vec3 v = glm::vec3(feet.x, sillY + 1.25f, feet.z);
        if (!ray(v, v + f * (dWall + 0.4f))) return false;
    }
    // 4) espessura da parede (raio de volta, do outro lado)
    float thick = 0.15f;
    const glm::vec3 far = feet + f * (dWall + 1.2f) + up * (rel - 0.08f);
    if (ray(far, far - f * 1.2f)) thick = std::clamp(glm::dot(hit - feet, f) - dWall, 0.05f, 0.6f);
    // 5) chão do outro lado (pode ser mais baixo, até 3,5 m)
    const glm::vec3 land = feet + f * (dWall + thick + 0.55f);
    if (!ray(land + up * (rel + 0.4f), land - up * (3.5f - rel))) return false;
    const float floorY = hit.y;
    // 6) espaço para ficar em pé onde cai
    const glm::vec3 chk = glm::vec3(land.x, floorY + 1.f * sc, land.z);
    if (ray(chk - f * 0.3f, chk + f * 0.3f)) return false;
    c.vaultA = feet;
    c.vaultB = glm::vec3(feet + f * (dWall + thick * 0.5f));
    c.vaultB.y = sillY - 0.3f * sc;  // o corpo encolhido passa com os joelhos por cima do peitoril
    c.vaultC = glm::vec3(land.x, floorY + 0.02f, land.z);
    c.vaultDir = f;
    c.vaultDur = 0.55f + 0.08f * std::max(0.f, sillY - floorY - 1.f);  // queda maior do outro lado: um pouco mais
    c.vaultT = 0.f;
    c.jumpHold = 0.18f;
    b->setCollisionFlags(b->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
    b->setGravity(btVector3(0, 0, 0));
    b->setLinearVelocity(btVector3(0, 0, 0));
    return true;
}

// Botão de pular perto de uma janela: pula a janela de frente, de lado ou de costas. Tenta primeiro para onde anda
// (aí vale mureta também), depois para onde olha e por fim em volta (só janela: pulo perto da mureta da laje
// continua sendo pulo normal).
static bool tryVaultAround(Engine* e, CharacterInfo& c, GameObject& go, btRigidBody* b, glm::vec3 move, glm::vec3 fwd) {
    if (glm::length(move) > 0.3f && tryVault(e, c, go, b, move)) return true;
    if (glm::length(move) > 0.3f && tryVault(e, c, go, b, move, 1.1f, true)) return true;
    if (tryVault(e, c, go, b, fwd, 1.1f, true)) return true;
    const float a0 = std::atan2(fwd.x, fwd.z);
    for (int k = 1; k <= 8; ++k)  // em volta, das direções mais perto do olhar para as de trás
        for (int s : {1, -1}) {
            if (k == 8 && s < 0) continue;
            const float a = a0 + s * k * (3.14159265f / 8.f);
            if (tryVault(e, c, go, b, glm::vec3(std::sin(a), 0.f, std::cos(a)), 1.1f, true)) return true;
        }
    return false;
}

static void updateVault(Engine* e, CharacterInfo& c, GameObject& go, btRigidBody* b, float dt) {
    c.vaultT = std::min(1.f, c.vaultT + dt / c.vaultDur);
    const float t = c.vaultT;
    glm::vec3 p;
    if (t < 0.5f) {  // até o vão: sobe e avança
        const float u = t / 0.5f, s = u * u * (3.f - 2.f * u);
        p = glm::mix(c.vaultA, c.vaultB, s);
        p.y = glm::mix(c.vaultA.y, c.vaultB.y, std::sin(u * 1.5707963f));
    } else {  // do vão até o chão do outro lado: um arco curto e desce
        const float u = (t - 0.5f) / 0.5f;
        p = glm::mix(c.vaultB, c.vaultC, u * (2.f - u));
        p.y = glm::mix(c.vaultB.y, c.vaultC.y, u * u) + 0.12f * std::sin(u * 3.14159265f);
    }
    go.transform.setPosition(p);
    if (go.rigidBody) e->physics.syncFromTransform(*go.rigidBody);
    b->setLinearVelocity(btVector3(0, 0, 0));
    c.airborne = true;
    animateCharacter(c, go.transform.rotation, glm::vec3(c.vaultDir.x, 3.f, c.vaultDir.z), false, true);
    if (t >= 1.f) {  // chegou: volta a bater nas coisas, sai andando para a frente
        c.vaultT = -1.f;
        c.jumpHold = 0.f;
        b->setCollisionFlags(b->getCollisionFlags() & ~btCollisionObject::CF_NO_CONTACT_RESPONSE);
        b->setLinearVelocity(btVector3(c.vaultDir.x * 2.f, -1.f, c.vaultDir.z * 2.f));
    }
}

void updatePlayer(Engine* e, float dt) {
    for (auto& t : e->tracers) t.life -= dt;
    e->fireCooldown -= dt;
    if (e->player < 0 || e->player >= int(e->characters.size())) return;
    CharacterInfo& c = e->characters[e->player];
    GameObject* go = e->scene.get(c.root);
    if (!go || !go->rigidBody || !go->rigidBody->body) return;
    btRigidBody* b = go->rigidBody->body;
    bool alive = e->game.state == 0 && !c.dying;  // derrota/vitória (ou abatido na partida de times): o jogador para
    if (e->game.state == 2 && !c.dying) playDeath(c);

    float yaw = e->editorCam.yaw;
    glm::vec3 fwd(-std::sin(yaw), 0.f, -std::cos(yaw)), right(std::cos(yaw), 0.f, -std::sin(yaw));
    glm::vec3 move = alive ? right * e->moveInput.x + fwd * e->moveInput.y : glm::vec3(0.f);
    c.aimHold = alive && e->aimInput && c.weaponKind >= 0;
    const bool fp = e->firstPerson && alive;
    c.firstPerson = e->firstPerson;
    c.viewPitch = fp ? -e->editorCam.pitch : 0.f;
    glm::vec3 face = fp || c.aimHold || c.aimTimer > 0.f ? fwd : glm::vec3(0.f);
    c.crouch = alive && e->crouchInput && c.weaponKind >= 0 && c.weaponKind != WK_Pistol;
    float speed = c.crouch ? 1.6f : e->runInput && !c.aimHold ? 6.5f : 2.6f;  // mirando: anda devagar; agachado mais ainda
    if (fp && c.weaponKind == WK_Pistol) c.aimTimer = std::max(c.aimTimer, 0.1f);  // primeira pessoa: pistola sempre à frente
    if (c.vaultT >= 0.f) updateVault(e, c, *go, b, dt);  // pulando janela: o caminho manda
    // pulo perto de janela: andando na direção dela (vale mureta também) ou, parado/de lado, a janela mais perto
    else if (alive && e->jumpRequest && tryVaultAround(e, c, *go, b, move, fwd)) updateVault(e, c, *go, b, dt);
    else driveCharacter(e, c, move, speed, alive && e->jumpRequest, dt, face);
    e->jumpRequest = false;
    if (fp) {  // primeira pessoa: o corpo vira junto com o olhar, sem atraso (a arma fica no centro da tela)
        glm::quat q = glm::angleAxis(std::atan2(fwd.x, fwd.z), glm::vec3(0, 1, 0));
        btTransform t = b->getWorldTransform();
        t.setRotation({q.x, q.y, q.z, q.w});
        b->setWorldTransform(t);
        go->transform.setRotation(q);
    }

    e->hitMarker = std::max(0.f, e->hitMarker - dt);
    // armas da partida: pente e reserva de cada arma; recarrega no botão ou quando o pente acaba
    auto startReload = [&]() {
        int n = c.reserve < 0 ? c.magSize - c.ammo : std::min(c.reserve, c.magSize - c.ammo);
        if (n <= 0) return;
        if (!c.bombaObjs.empty()) {  // Doze: um cartucho por vez (0,5 s cada); atirar interrompe
            if (c.ammo <= 0) c.precisaBombear = true;
            c.ammo += 1;
            if (c.reserve > 0) c.reserve -= 1;
            c.reloadTimer = 0.5f;
            c.cartucho = true;
            playSound(e, "cartucho", go->transform.position, 0.8f);
            return;
        }
        c.ammo += n;
        if (c.reserve > 0) c.reserve -= n;
        const AnimationClip* rc = c.anim && c.clipMap[CC_Reload] >= 0 ? c.anim->clipAt(c.clipMap[CC_Reload]) : nullptr;
        c.reloadTimer = c.weaponKind == WK_Pistol ? 1.3f : rc ? rc->duration / 1.4f : 2.f;
        e->aimInput = false;  // recarregando sai da mira e não volta sozinho depois
        playSound(e, "recarga", go->transform.position, 0.8f);
        if (c.anim && c.clipMap[CC_Reload] >= 0 && c.weaponKind != WK_Pistol) c.anim->setClip(c.clipMap[CC_Reload]);
    };
    if (c.gunDmg > 0.f) {
        if (c.cartucho && c.reloadTimer <= 0.f) {  // Doze: próximo cartucho, ou acabou (e bombeia se tinha esvaziado)
            const bool mais = c.ammo < c.magSize && c.reserve != 0 && alive;
            if (mais) startReload();
            else { c.cartucho = false; if (c.precisaBombear) { c.precisaBombear = false; comecarBomba(c); } }
        }
        // atirar no meio da recarga da Doze: para de pôr cartucho e atira
        if (alive && e->fireRequest && c.cartucho && c.ammo > 0 && e->fireCooldown <= 0.f) { c.cartucho = false; c.reloadTimer = 0.f; c.precisaBombear = false; }
        if (alive && e->reloadRequest && c.reloadTimer <= 0.f) startReload();
        if (alive && e->fireRequest && e->fireCooldown <= 0.f && c.reloadTimer <= 0.f) {
            if (c.ammo > 0) {
                fire(e, c, *go, b);
                c.fireAnim = 0.35f;
                if (--c.ammo <= 0) startReload();
            } else startReload();
        }
    }
    e->reloadRequest = false;
    // fuzil, escopeta e sniper: pente de 30 tiros; vazio, recarrega sozinho (com a animação)
    bool longGun = c.weaponKind == WK_Rifle || c.weaponKind == WK_Shotgun || c.weaponKind == WK_Sniper;
    if (c.gunDmg <= 0.f && alive && e->fireRequest && e->fireCooldown <= 0.f && c.reloadTimer <= 0.f) {
        fire(e, c, *go, b);
        if (longGun) {
            c.fireAnim = 0.35f;
            if (--c.ammo <= 0) {
                c.ammo = 30;
                const AnimationClip* rc = c.anim && c.clipMap[CC_Reload] >= 0 ? c.anim->clipAt(c.clipMap[CC_Reload]) : nullptr;
                c.reloadTimer = rc ? rc->duration / 1.4f : 2.f;
                e->aimInput = false;
                playSound(e, "recarga", go->transform.position, 0.8f);
                if (c.anim && c.clipMap[CC_Reload] >= 0) c.anim->setClip(c.clipMap[CC_Reload]);
                e->game.message = "Recarregando...";
            }
        }
    }
    e->fireRequest = false;

    // câmera em terceira pessoa segue o jogador (em primeira pessoa, só quando ele cai: vê o corpo de fora)
    if (e->firstPerson) e->editorCam.distance = 3.5f;
    glm::vec3 focus = go->transform.position + glm::vec3(0, c.height * 0.85f, 0);
    e->editorCam.target = glm::mix(e->editorCam.target, focus, 1.f - std::exp(-10.f * dt));
}

void deleteObject(Engine* e, Scene::Handle h) {
    GameObject* sel = e->scene.get(h);
    if (!sel) return;
    // personagem apagado leva junto o equipamento preso nele
    std::vector<Scene::Handle> gear;
    for (const CharacterInfo& c : e->characters)
        if (c.root.index == h.index && c.root.gen == h.gen) gear = c.gear;
    std::vector<const Transform3D*> gone;
    Scene::forEachInSubtree(*sel, [&](GameObject& g) { gone.push_back(&g.transform); });
    auto isGone = [&](const Transform3D* t) { return t && std::find(gone.begin(), gone.end(), t) != gone.end(); };
    // armas presas a personagens apagados (ou a própria arma apagada) saem da lista
    e->attachments.erase(std::remove_if(e->attachments.begin(), e->attachments.end(), [&](const Attachment& a) {
        GameObject* o = e->scene.get(a.obj);
        return !o || isGone(&o->transform) || (a.anim && isGone(a.anim->space));
    }), e->attachments.end());
    std::vector<Animator*> ownAnims;  // animators dos nós apagados saem da lista e voltam ao pool
    Scene::forEachInSubtree(*sel, [&](GameObject& g) { if (g.animator) ownAnims.push_back(g.animator); });
    e->animators.erase(std::remove_if(e->animators.begin(), e->animators.end(), [&](Animator* a) {
        return std::find(ownAnims.begin(), ownAnims.end(), a) != ownAnims.end();
    }), e->animators.end());
    for (Animator* a : e->animators) if (isGone(a->space)) a->space = nullptr;
    for (size_t i = e->characters.size(); i-- > 0;) {
        CharacterInfo& c = e->characters[i];
        GameObject* w = e->scene.get(c.weapon);
        if (w && isGone(&w->transform)) { c.weapon = {}; c.weaponKind = -1; }
        GameObject* r = e->scene.get(c.root);
        if (!r || isGone(&r->transform)) {
            if (e->player == int(i)) e->player = -1;
            else if (e->player > int(i)) --e->player;
            e->characters.erase(e->characters.begin() + i);
        }
    }
    e->doors.erase(std::remove_if(e->doors.begin(), e->doors.end(), [&](const DoorRT& d) {
        GameObject* o = e->scene.get(d.h);
        return !o || isGone(&o->transform);
    }), e->doors.end());
    e->teles.erase(std::remove_if(e->teles.begin(), e->teles.end(), [&](const TeleRT& t) {
        GameObject* o = e->scene.get(t.root);
        return !o || isGone(&o->transform);
    }), e->teles.end());
    e->lightObjects.erase(std::remove_if(e->lightObjects.begin(), e->lightObjects.end(), [&](const LightObject& l) {
        GameObject* o = e->scene.get(l.obj);
        return !o || isGone(&o->transform);
    }), e->lightObjects.end());
    if (e->scene.get(e->selected) && isGone(&e->scene.get(e->selected)->transform)) selectHandle(e, {});
    Scene::forEachInSubtree(*sel, [&](GameObject& g) { releaseComponents(e, g); });
    for (Animator* a : ownAnims) e->animatorPool.release(a);
    e->scene.destroy(h);
    ++e->revision;
    for (Scene::Handle g : gear) deleteObject(e, g);
}

// ---------------- efeitos ----------------
int createEffect(Engine* e, int kind, int element) {
    kind = std::clamp(kind, 0, FX_Count - 1);
    char name[32];
    std::snprintf(name, sizeof(name), "%s (%s)", fxName(kind), elementName(std::clamp(element, 0, EL_Count - 1)));
    Scene::Handle h = e->scene.create(name);
    GameObject* go = e->scene.get(h);
    auto eh = e->emitters.acquire();
    Emitter* em = e->emitters.get(eh);
    if (!go || !em) { e->lastError = "limite de efeitos"; return -1; }
    em->kind = kind;
    em->element = std::clamp(element, 0, EL_Count - 1);
    em->owner = go;
    go->emitter = em;
    glm::vec3 pos = e->editorCam.target;
    pos.y = e->mapKind >= 0 ? mapHeight(e, pos.x, pos.z) : 0.f;
    go->transform.setPosition(pos);
    return registerObject(e, h);
}

void addFlash(Engine* e, const FxLight& l, float life) {
    if (l.intensity <= 0.f) return;
    FxFlash* slot = &e->flashes[0];
    for (auto& f : e->flashes) {
        if (f.life <= 0.f) { slot = &f; break; }
        if (f.life < slot->life) slot = &f;
    }
    *slot = {l, life, life};
}

void spawnEffect(Engine* e, int kind, int element, glm::vec3 pos) {
    FxPalette pal = fxPalette(element);
    if (kind == FX_Explosion) { addFlash(e, fxExplosion(e->particles, e->fxRng, pal, pos, 1.f), 0.35f); return; }
    float acc = 0.f, timer = 0.f;
    for (int i = 0; i < 30; ++i) fxContinuous(e->particles, e->fxRng, kind, pal, pos, 1.f, 1.f / 30.f, acc, timer);
}

static void radialImpulse(Engine* e, glm::vec3 c, float radius, float power, const btCollisionObject* ignore) {
    e->bodies.forEach([&](RigidBody& rb, RigidBodyPool::Handle) {
        if (!rb.body || rb.mass <= 0.f || rb.body == ignore) return;
        btVector3 p = rb.body->getCenterOfMassPosition();
        glm::vec3 d = glm::vec3(p.x(), p.y(), p.z()) - c;
        float dist = glm::length(d);
        if (dist > radius) return;
        glm::vec3 dir = dist > 1e-3f ? d / dist : glm::vec3(0, 1, 0);
        dir = glm::normalize(dir + glm::vec3(0, 0.5f, 0));
        float k = power * (1.f - dist / radius) * rb.mass;
        rb.body->activate(true);
        rb.body->applyCentralImpulse({dir.x * k, dir.y * k, dir.z * k});
    });
}

static void lightningBolt(Engine* e, glm::vec3 a, glm::vec3 b, glm::vec3 color) {
    Bolt* slot = &e->bolts[0];
    for (auto& bl : e->bolts) if (bl.life <= 0.f) { slot = &bl; break; }
    Bolt& bl = *slot;
    bl.n = 14;
    glm::vec3 d = b - a;
    float len = glm::length(d);
    glm::vec3 side = glm::normalize(glm::cross(d, glm::vec3(0, 1, 0)) + glm::vec3(1e-4f));
    glm::vec3 up = glm::normalize(glm::cross(side, d));
    for (int i = 0; i < bl.n; ++i) {
        float t = float(i) / (bl.n - 1);
        float jit = (i == 0 || i == bl.n - 1) ? 0.f : len * 0.05f;
        bl.pts[i] = a + d * t + side * e->fxRng.range(-jit, jit) + up * e->fxRng.range(-jit, jit);
    }
    bl.color = color;
    bl.life = 0.18f;
}

void castSpell(Engine* e, int element) {
    if (e->castCooldown > 0.f || e->player < 0 || e->player >= int(e->characters.size())) return;
    CharacterInfo& c = e->characters[e->player];
    GameObject* go = e->scene.get(c.root);
    if (!go || !go->rigidBody || !go->rigidBody->body) return;
    element = std::clamp(element, 0, EL_Count - 1);
    FxPalette pal = fxPalette(element);
    glm::vec3 dir = aimDirection(e);
    // vira o personagem para a mira e lança da altura do peito, à frente do corpo
    float yaw = std::atan2(dir.x, dir.z);
    glm::quat q = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
    btTransform t = go->rigidBody->body->getWorldTransform();
    t.setRotation({q.x, q.y, q.z, q.w});
    go->rigidBody->body->setWorldTransform(t);
    go->transform.setRotation(q);
    glm::vec3 from = go->transform.position + glm::vec3(0, c.height * 0.72f, 0) + glm::normalize(glm::vec3(dir.x, 0, dir.z)) * 0.6f;
    e->castCooldown = 0.45f;
    if (element == EL_Lightning) {  // raio: instantâneo
        glm::vec3 hit; GameObject* obj = nullptr; float frac;
        glm::vec3 aimFrom = e->camera.position;
        bool any = e->physics.raycastIgnoring(aimFrom, aimFrom + dir * 80.f, go->rigidBody->body, hit, obj, frac);
        glm::vec3 end = any ? hit : aimFrom + dir * 80.f;
        lightningBolt(e, from, end, pal.glow);
        addFlash(e, fxExplosion(e->particles, e->fxRng, pal, end, 0.5f), 0.2f);
        addFlash(e, {from, pal.light, 15.f, 10.f}, 0.18f);
        radialImpulse(e, end, 3.f, 7.f, go->rigidBody->body);
        if (any && obj) damageAt(e, obj, 60.f, from);
        return;
    }
    for (auto& p : e->projectiles) {
        if (p.active) continue;
        p.active = true;
        p.pos = from;
        p.vel = dir * 24.f;
        p.pal = pal;
        p.element = element;
        p.life = 3.f;
        p.ignore = go->rigidBody->body;
        break;
    }
}

// ---------- skin de energia (AK-47 Energia Cósmica): o mesmo efeito do artefato de prévia ----------
// O artefato mostra a arma 2,3 vezes maior (ESC); aqui as medidas dele viram espaço do modelo dividindo por 2,3.
namespace {
constexpr float kEsc = 2.3f;
float suave(float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); }
glm::vec3 hsl(float h, float s, float l) {
    auto f = [&](float n) { const float k = std::fmod(n + h * 12.f, 12.f), a = s * std::min(l, 1.f - l); return l - a * std::max(-1.f, std::min({k - 3.f, 9.f - k, 1.f})); };
    return glm::vec3(f(0.f), f(8.f), f(4.f));
}
glm::vec3 srgbLin(glm::vec3 c) { return glm::pow(c, glm::vec3(2.2f)); }
// um ponto da superfície (pontos tirados do modelo), no mundo, com a normal
bool pontoEnergia(const Emitter& em, Rng& r, const glm::mat4& M, glm::vec3& p, glm::vec3& n) {
    if (!em.pontos || em.pontos->size() < 6) return false;
    const size_t i = size_t(r.f() * float(em.pontos->size() / 6)) % (em.pontos->size() / 6);
    const float* v = em.pontos->data() + i * 6;
    p = glm::vec3(M * glm::vec4(v[0], v[1], v[2], 1.f));
    n = glm::normalize(glm::vec3(M * glm::vec4(v[3], v[4], v[5], 0.f)) + glm::vec3(1e-6f));
    return true;
}
// caminho em zigue-zague (relâmpago) por deslocamento do ponto médio
void zigue(Rng& r, glm::vec3 a, glm::vec3 b, float disp, int prof, std::vector<glm::vec3>& out) {
    std::vector<glm::vec3> pts{a, b};
    for (int d = 0; d < prof; ++d) {
        std::vector<glm::vec3> np{pts[0]};
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            glm::vec3 m = (pts[i] + pts[i + 1]) * 0.5f + glm::vec3(r.range(-0.5f, 0.5f), r.range(-0.5f, 0.5f), r.range(-0.5f, 0.5f)) * disp;
            np.push_back(m);
            np.push_back(pts[i + 1]);
        }
        pts.swap(np);
        disp *= 0.55f;
    }
    out.insert(out.end(), pts.begin(), pts.end());
}
// um traço de raio entre dois pontos: miolo branco fino + brilho colorido (como o shader de raio do artefato)
void tracoRaio(ParticleSystem& ps, glm::vec3 p0, glm::vec3 p1, float largura, float forca, glm::vec3 cor, float vida, bool presa = false) {
    const glm::vec3 dd = p1 - p0;
    const float len = glm::length(dd);
    if (len < 1e-5f) return;
    const glm::vec3 d = dd / len;
    const float v = 0.02f;
    for (int camada = 0; camada < 2; ++camada) {
        ParticleSpawn s;
        const float tam = camada ? largura * 0.5f : largura * 0.12f;
        s.pos = (p0 + p1) * 0.5f; s.vel = d * v;
        s.stretch = std::max(0.f, (len * 0.5f - tam) / v);
        s.life = vida; s.size0 = s.size1 = tam;
        s.color0 = camada ? glm::vec4(cor * 1.6f * forca, 1.f) : glm::vec4(glm::vec3(3.f * forca), 1.f);
        s.color1 = glm::vec4(glm::vec3(s.color0) * 0.6f, 0.f);
        s.shape = camada ? PS_Glow : PS_Spark;
        s.presa = presa;
        ps.emit(s);
    }
}
}  // namespace

static void tiroEnergia(Engine* e, Emitter& em, glm::vec3 boca, glm::vec3 dir) {
    em.atirou = true;  // conta como "fez alguma coisa": a arma parada volta e o relógio dos 2 minutos zera
    em.pulso = 1.f;    // veias e núcleo acendem forte e apagam
    em.calor = std::min(1.f, em.calor + 0.14f);
    const glm::vec3 roxo = srgbLin(em.corEnergia), lilas = srgbLin(glm::vec3(0.86f, 0.74f, 1.f));
    const float k = 1.f + 0.35f * em.calor;  // rajada longa: o clarão cresce um pouco
    // clarão de energia: estrela roxa, miolo branco-lilás e um anel que abre na boca do cano
    ParticleSpawn c;
    c.pos = boca + dir * (0.01f / kEsc); c.life = 0.075f; c.size0 = 0.16f * k; c.size1 = 0.07f;
    c.color0 = glm::vec4(roxo * 3.2f, 1.f); c.color1 = glm::vec4(roxo, 0.f);
    c.shape = PS_Star; c.presa = true;
    e->particles.emit(c);
    ParticleSpawn w = c;
    w.size0 = 0.06f * k; w.size1 = 0.025f; w.color0 = glm::vec4(lilas * 3.5f, 1.f); w.color1 = glm::vec4(lilas, 0.f); w.shape = PS_Glow;
    e->particles.emit(w);
    ParticleSpawn h = c;  // halo largo e fraco (a energia "vaza" em volta)
    h.size0 = 0.12f * k; h.size1 = 0.26f * k; h.life = 0.12f; h.color0 = glm::vec4(roxo * 0.9f, 0.55f); h.color1 = glm::vec4(roxo * 0.4f, 0.f);
    h.shape = PS_Glow;
    e->particles.emit(h);
    ParticleSpawn a = c;
    a.pos = boca + dir * (0.03f / kEsc); a.life = 0.14f; a.size0 = 0.02f; a.size1 = 0.13f * k;
    a.color0 = glm::vec4(lilas * 2.2f, 0.9f); a.color1 = glm::vec4(roxo, 0.f); a.shape = PS_Ring;
    e->particles.emit(a);
    // faíscas: a maioria roxa, algumas lilás quase brancas, saindo em cone para a frente
    for (int i = 0; i < 16; ++i) {
        ParticleSpawn s;
        s.pos = boca;
        s.vel = (dir * e->fxRng.range(1.5f, 5.f) + glm::vec3(e->fxRng.range(-0.9f, 0.9f), e->fxRng.range(-0.9f, 0.9f), e->fxRng.range(-0.9f, 0.9f))) / kEsc;
        s.life = e->fxRng.range(0.18f, 0.42f);
        const glm::vec3 cor = i % 4 ? roxo : lilas;
        s.size0 = e->fxRng.range(8.f, 16.f) * 0.000478f; s.size1 = s.size0 * 0.25f;
        s.color0 = glm::vec4(cor * 2.4f, 1.f); s.color1 = glm::vec4(cor, 0.f);
        s.shape = PS_Star; s.drag = 2.f;
        e->particles.emit(s);
    }
    // a energia sai do núcleo e corre pela arma até a boca do cano (um raio curto a cada tiro)
    if (em.luzPos != glm::vec3(0.f) && glm::distance(em.luzPos, boca) < 1.2f) {
        std::vector<glm::vec3> pts;
        zigue(e->fxRng, em.luzPos, boca, 0.02f / kEsc, 3, pts);
        for (size_t i = 0; i + 1 < pts.size(); ++i) tracoRaio(e->particles, pts[i], pts[i + 1], 0.012f / kEsc, 0.9f, roxo, 0.06f);
    }
    addFlash(e, {boca, srgbLin(glm::vec3(0.72f, 0.5f, 1.f)), 13.f + 4.f * em.calor, 2.8f}, 0.16f);
}
// feixe roxo do tiro: um traço largo da boca para a frente (3,9 m, o do artefato) com 3 filamentos elétricos
static void feixeEnergia(Engine* e, glm::vec3 from, glm::vec3 to) {
    const glm::vec3 d = to - from;
    const float dist = glm::length(d);
    if (dist < 0.05f) return;
    const glm::vec3 u = d / dist;
    const glm::vec3 fim = from + u * std::min(dist, 9.f / kEsc);
    const glm::vec3 cor = srgbLin(glm::vec3(0.70f, 0.40f, 1.f));
    tracoRaio(e->particles, from, fim, 0.05f / kEsc * 2.f, 1.f, cor, 0.16f);
    tracoRaio(e->particles, from, fim, 0.014f / kEsc * 2.f, 1.4f, srgbLin(glm::vec3(0.9f, 0.82f, 1.f)), 0.1f);  // fio claro no meio
    std::vector<glm::vec3> pts;
    for (int j = 0; j < 3; ++j) {
        pts.clear();
        zigue(e->fxRng, from, fim, 0.12f / kEsc * 2.f, 4, pts);
        for (size_t i = 0; i + 1 < pts.size(); ++i) tracoRaio(e->particles, pts[i], pts[i + 1], 0.016f / kEsc * 2.f, 0.55f, cor, 0.16f);
    }
}
// estouro roxo onde a bala da skin de energia acerta
static void impactoEnergia(Engine* e, Emitter& em, glm::vec3 hit, glm::vec3 dir) {
    const glm::vec3 roxo = srgbLin(em.corEnergia), lilas = srgbLin(glm::vec3(0.86f, 0.74f, 1.f));
    const glm::vec3 p = hit - dir * (0.02f / kEsc);
    ParticleSpawn g;
    g.pos = p; g.life = 0.12f; g.size0 = 0.08f; g.size1 = 0.18f;
    g.color0 = glm::vec4(roxo * 2.4f, 1.f); g.color1 = glm::vec4(roxo, 0.f); g.shape = PS_Glow;
    e->particles.emit(g);
    ParticleSpawn r = g;
    r.life = 0.2f; r.size0 = 0.03f; r.size1 = 0.32f; r.color0 = glm::vec4(lilas * 1.8f, 0.8f); r.color1 = glm::vec4(roxo, 0.f); r.shape = PS_Ring;
    e->particles.emit(r);
    for (int i = 0; i < 10; ++i) {
        ParticleSpawn s;
        s.pos = p;
        s.vel = (-dir * e->fxRng.range(0.5f, 2.f) + glm::vec3(e->fxRng.range(-1.5f, 1.5f), e->fxRng.range(-0.5f, 2.f), e->fxRng.range(-1.5f, 1.5f))) / kEsc;
        s.life = e->fxRng.range(0.25f, 0.5f);
        s.size0 = e->fxRng.range(7.f, 13.f) * 0.000478f; s.size1 = s.size0 * 0.3f;
        const glm::vec3 cor = i % 3 ? roxo : lilas;
        s.color0 = glm::vec4(cor * 2.2f, 1.f); s.color1 = glm::vec4(cor, 0.f);
        s.shape = PS_Star; s.drag = 1.8f; s.gravity = 2.5f / kEsc;  // caem
        e->particles.emit(s);
    }
    addFlash(e, {p, roxo, 6.f, 1.6f}, 0.1f);
}

void atualizarEnergia(Engine* e, float dt) {
    if (dt <= 0.f) return;
    const glm::vec3 cam = e->camera.position;
    e->emitters.forEach([&](Emitter& em, EmitterPool::Handle) {
        GameObject* o = em.owner;
        if (!em.energia || !o) return;
        em.luzEnergia = 0.f;
        if (!o->active) return;
        // a arma guardada no corpo (costas, coxa) fica quieta: só a pintura
        const Scene::Handle eu = Scene::handleOf(*o);
        for (const CharacterInfo& c : e->characters)
            if (c.holster.index == eu.index && c.holster.gen == eu.gen) {
                Scene::forEachInSubtree(*o, [&](GameObject& p) {
                    if (p.meshRenderer) e->renderer.setEnergia(p.meshRenderer->material, glm::vec4(0.35f, 0.f, 1.f, 0.f));
                });
                return;
            }
        em.timer += dt;
        const float t = em.timer;
        // 2 minutos parada na mão (o dono não anda e ninguém atira com ela): a arma se estilhaça
        {
            glm::vec3 pos(0.f);
            bool temDono = false;
            const Scene::Handle eu = Scene::handleOf(*o);
            for (const CharacterInfo& c : e->characters)
                if (c.weapon.index == eu.index && c.weapon.gen == eu.gen)
                    if (GameObject* r = e->scene.get(c.root)) { pos = r->transform.worldPosition(); temDono = true; break; }
            const bool andou = temDono && glm::distance(pos, em.posParado) > 0.15f;
            if (andou) em.posParado = pos;
            if (andou || em.atirou) {
                em.parado = 0.f;
                if (em.ativT >= 0.f && em.ativT < 2.7f) {  // no meio do estilhaço: volta rápido
                    const float T = em.ativT;
                    em.fragVolta = suave(0.55f, 1.15f, T) * (1.f - suave(1.9f, 2.7f, T));
                    em.flareVolta = suave(0.7f, 1.f, T) * (1.f - suave(1.2f, 2.2f, T));
                    em.volta = 0.25f;
                    em.ativT = -1.f;
                    em.ondaT = -1.f;
                    em.ativo = true;
                }
            } else if (temDono && em.ativT < 0.f && em.volta <= 0.f) {
                em.parado += dt;
                if (em.parado >= 120.f) { em.parado = 0.f; em.ativT = 0.f; em.nivel = 0.35f; }
            }
            em.atirou = false;
        }
        // ativação: sobe a energia, a arma se solta em pedaços (libera o núcleo), a onda passa e tudo volta
        float frag = 0.f, flare = 0.f;
        if (em.volta > 0.f) {  // voltando rápido ao normal (atirou ou andou no meio)
            em.volta = std::max(0.f, em.volta - dt);
            const float k = suave(0.f, 0.25f, em.volta);
            frag = em.fragVolta * k;
            flare = em.flareVolta * k;
            em.nivel = 1.f;
        } else if (em.ativT >= 0.f) {
            em.ativT += dt;
            const float T = em.ativT;
            em.nivel = 0.35f + 0.65f * suave(0.f, 0.8f, T);
            frag = suave(0.55f, 1.15f, T) * (1.f - suave(1.9f, 2.7f, T));
            flare = suave(0.7f, 1.f, T) * (1.f - suave(1.2f, 2.2f, T));
            if (T > 0.85f && T - dt <= 0.85f) em.ondaT = 0.f;
            if (T > 2.9f) { em.ativT = -1.f; em.ativo = true; }
        } else {
            em.nivel += ((em.ativo ? 1.f : 0.35f) - em.nivel) * std::min(1.f, dt * 2.f);
        }
        // tiro: as veias acendem (pulso) e, com a rajada, correm mais quentes (calor); os dois apagam sozinhos
        em.pulso = std::max(0.f, em.pulso - dt * 9.f);
        em.calor = std::max(0.f, em.calor - dt * 0.45f);
        const float veia = em.nivel * (0.85f + 0.15f * std::sin(t * (2.3f + 6.f * em.calor))) + 0.5f * em.pulso + 0.45f * em.calor;
        Scene::forEachInSubtree(*o, [&](GameObject& p) {
            if (!p.meshRenderer) return;
            e->renderer.setEnergia(p.meshRenderer->material, glm::vec4(veia, frag, 1.f, 0.f));
            e->renderer.setEmissive(p.meshRenderer->material, glm::vec3(em.emiBase * (0.5f + 0.5f * veia)));
        });
        const glm::mat4 M = o->transform.world() * em.rel;
        const float ws = glm::length(glm::vec3(M[0]));
        const glm::vec3 centroArma = glm::vec3(M * glm::vec4(0.f, em.nucleo.y, em.nucleo.z, 1.f));
        if (glm::distance(cam, centroArma) > 30.f) return;  // longe: só a pintura
        ParticleSystem& ps = e->particles;
        Rng& r = e->fxRng;
        const glm::vec3 cor = srgbLin(em.corEnergia);
        const float vida1 = dt * 1.05f;  // partículas de um quadro (núcleo, cristais): redesenhadas a cada quadro

        // núcleo, dos dois lados do disco (como um adesivo virado para fora: some quando visto de lado)
        const float tamNuc = (0.55f + 0.45f * em.nivel + 0.9f * flare + 0.35f * em.pulso + 0.2f * em.calor) * (1.f + 0.05f * std::sin(t * 5.f));
        const float forcaNuc = 0.45f + 0.45f * em.nivel + 0.6f * flare + 0.4f * em.pulso;
        for (int lado = -1; lado <= 1; lado += 2) {
            const glm::vec3 pos = glm::vec3(M * glm::vec4(lado * (em.nucleo.x + 0.004f), em.nucleo.y, em.nucleo.z, 1.f));
            const glm::vec3 nrm = glm::normalize(glm::vec3(M * glm::vec4(float(lado), 0.f, 0.f, 0.f)));
            const float frente = glm::dot(nrm, glm::normalize(cam - pos));
            const float vis = suave(0.f, 0.35f, frente);
            if (vis <= 0.f) continue;
            ParticleSpawn s;
            s.pos = pos; s.life = vida1; s.size0 = s.size1 = 0.1f / kEsc * tamNuc * ws;
            s.color0 = s.color1 = glm::vec4(srgbLin(glm::vec3(0.69f, 0.42f, 1.f)), forcaNuc * vis);
            s.shape = PS_Nucleo; s.seed = std::fmod(t, 600.f);
            ps.emit(s);
        }
        em.luzPos = centroArma;
        em.luzEnergia = (0.8f + 1.6f * em.nivel + 5.f * flare + 2.5f * em.pulso + 1.2f * em.calor) * (0.85f + 0.15f * std::sin(t * 7.f));

        // onda de choque saindo do núcleo
        if (em.ondaT >= 0.f) {
            em.ondaT += dt;
            const float k = em.ondaT / 0.9f;
            if (k >= 1.f) em.ondaT = -1.f;
            else {
                const float raio = 0.5f * (0.4f + 4.2f * k) * (0.5f + 0.35f * k) / kEsc * ws;
                ParticleSpawn s;
                s.pos = centroArma; s.life = vida1; s.size0 = s.size1 = raio / 0.72f;
                s.color0 = s.color1 = glm::vec4(srgbLin(glm::vec3(0.75f, 0.55f, 1.f)) * 1.6f, 0.6f * (1.f - k));
                s.shape = PS_Ring;
                ps.emit(s);
            }
            if (em.ondaT >= 0.f && em.ondaT <= dt * 1.01f) {  // chuva de faíscas no começo da onda
                glm::vec3 p, n;
                for (int i = 0; i < 90; ++i)
                    if (pontoEnergia(em, r, M, p, n)) {
                        ParticleSpawn s;
                        s.pos = p; s.vel = n * r.range(1.2f, 3.4f) / kEsc * ws;
                        s.life = r.range(0.6f, 1.2f);
                        const glm::vec3 c = i % 4 ? srgbLin(glm::vec3(0.72f, 0.48f, 1.f)) : glm::vec3(1.f);
                        s.size0 = r.range(6.f, 15.f) * 0.000478f * ws; s.size1 = s.size0 * 0.3f;
                        s.color0 = glm::vec4(c * 1.6f, 1.f); s.color1 = glm::vec4(c, 0.f);
                        s.shape = PS_Star; s.drag = 1.6f; s.gravity = -0.052f;
                        ps.emit(s);
                    }
            }
        }

        // cristais orbitando (abrem na ativação)
        {
            const float abre = 0.6f + 0.4f * em.nivel + 0.9f * frag;
            const float brilho = 0.25f + 0.6f * em.nivel + 1.2f * flare;
            for (int i = 0; i < 28; ++i) {
                auto h = [&](int k) { uint32_t x = uint32_t(i * 7919 + k * 104729 + 17); x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15; return (x & 0xffffff) / float(0x1000000); };
                const float a0 = h(0) * 6.283f, rf = 0.7f + h(1) * 0.6f, hf = (h(2) - 0.5f) * 1.6f;
                const float sp = (0.25f + h(3) * 0.6f) * (h(4) < 0.5f ? 1.f : -1.f), ph = h(5) * 6.283f, sz = 0.6f + h(6) * 0.9f, tw = 0.4f + h(7) * 1.6f;
                const float a = a0 + t * sp * (0.6f + 0.8f * em.nivel);
                const glm::vec3 lp(std::cos(a) * 0.32f * rf * abre / kEsc, (hf * 0.36f * abre + std::sin(t * 1.3f + ph) * 0.04f) / kEsc,
                                   0.1f + std::sin(a) * 1.25f * rf * abre / kEsc);
                ParticleSpawn s;
                s.pos = glm::vec3(M * glm::vec4(lp, 1.f)); s.life = vida1;
                s.size0 = s.size1 = 0.045f * sz * (0.6f + 0.4f * em.nivel) / kEsc * ws;
                s.rot = t * tw + ph;
                // cristal claro (no artefato a luz do ambiente reflete nas faces); acende roxo no clarão da ativação
                const glm::vec3 c = glm::mix(glm::vec3(0.86f, 0.80f, 1.f), srgbLin(glm::vec3(0.62f, 0.30f, 1.f)) * 1.4f, std::min(1.f, flare * 0.7f));
                s.color0 = s.color1 = glm::vec4(c, 0.95f);
                s.shape = PS_Shard; s.additive = false;
                ps.emit(s);
                ParticleSpawn g = s;  // brilho roxo do cristal
                g.size0 = g.size1 = s.size0 * 1.6f; g.color0 = g.color1 = glm::vec4(cor * 0.25f * brilho, 1.f); g.shape = PS_Glow; g.additive = true;
                ps.emit(g);
            }
        }

        // raios na peça: trocam 24 vezes por segundo; mais e maiores com a energia alta
        em.accArcos += dt;
        if (em.accArcos > 1.f / 24.f) {
            em.accArcos = 0.f;
            const int n = int(std::round(1.f + 6.f * em.nivel + 14.f * frag));
            glm::vec3 A, B, nA, nB;
            std::vector<glm::vec3> pts;
            for (int k = 0; k < n; ++k) {
                if (!pontoEnergia(em, r, M, A, nA)) break;
                int tent = 0;
                do { pontoEnergia(em, r, M, B, nB); ++tent; } while (glm::distance(A, B) > 0.55f / kEsc * ws && tent < 10);
                const float len = glm::distance(A, B);
                if (len < 0.02f / kEsc * ws) continue;
                const glm::vec3 meio = (A + B) * 0.5f + nA * len * 0.3f;
                pts.clear();
                zigue(r, A, meio, len * 0.18f, 2, pts);
                std::vector<glm::vec3> p2;
                zigue(r, meio, B, len * 0.18f, 2, p2);
                pts.insert(pts.end(), p2.begin() + 1, p2.end());
                const float I = 0.45f + 0.55f * r.f();
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                    tracoRaio(ps, pts[i], pts[i + 1], 0.014f / kEsc * ws * 2.f, I, srgbLin(glm::vec3(0.79f, 0.63f, 1.f)), 1.f / 24.f);
            }
        }

        // partículas de energia saindo da peça
        em.accEnergia += dt * (8.f + 22.f * em.nivel);
        glm::vec3 p, n;
        while (em.accEnergia > 1.f) {
            em.accEnergia -= 1.f;
            if (!pontoEnergia(em, r, M, p, n)) break;
            ParticleSpawn s;
            s.pos = p;
            s.vel = (n * r.range(0.12f, 0.37f) + glm::vec3(0.f, 0.15f, 0.f)) / kEsc * ws;
            s.life = r.range(1.f, 1.9f);
            const glm::vec3 c = srgbLin(hsl(0.76f + r.f() * 0.05f, 1.f, 0.55f + r.f() * 0.2f));
            s.size0 = r.range(4.f, 11.f) * 0.000478f * ws; s.size1 = s.size0 * 0.4f;
            s.color0 = glm::vec4(c * 1.4f, 1.f); s.color1 = glm::vec4(c * 0.8f, 0.f);
            s.shape = PS_Star; s.drag = 1.6f; s.gravity = -0.052f * ws;
            ps.emit(s);
        }
    });
}

static Emitter* skinShot(Engine* e, Scene::Handle weapon) {
    GameObject* w = e->scene.get(weapon);
    return w && w->emitter && w->emitter->disparo ? w->emitter : nullptr;
}
// Âncora na boca do cano: guarda o ponto no espaço da arma; a cada quadro, depois que a arma foi posicionada
// (mão, primeira pessoa, corrida, pulo), as partículas presas andam o mesmo que a boca andou. Assim o fogo
// sai sempre da boca da arma, parado ou em movimento (antes ia pela velocidade do corpo e passava da frente).
static int ancoraDaArma(Engine* e, Scene::Handle weapon, glm::vec3 muzzle) {
    GameObject* w = e->scene.get(weapon);
    if (!w) return -1;
    const glm::mat4 mundo = w->transform.world();
    int slot = -1, livre = -1, velha = 0;
    for (int i = 0; i < ParticleSystem::kAncoras; ++i) {
        const Engine::BocaAncora& a = e->ancoras[i];
        if (a.vida > 0.f && a.arma.index == weapon.index && a.arma.gen == weapon.gen) { slot = i; break; }
        if (a.vida <= 0.f && livre < 0) livre = i;
        if (a.vida < e->ancoras[velha].vida) velha = i;
    }
    if (slot >= 0) {  // a mesma arma atirando de novo: o fogo do tiro anterior acompanha até aqui antes de trocar o ponto
        Engine::BocaAncora& a = e->ancoras[slot];
        e->particles.moverAncora(slot, glm::vec3(mundo * glm::vec4(a.local, 1.f)) - a.ultimo);
    } else {
        slot = livre >= 0 ? livre : velha;
        e->particles.soltarAncora(slot);
    }
    Engine::BocaAncora& a = e->ancoras[slot];
    a.arma = weapon;
    a.local = glm::vec3(glm::inverse(mundo) * glm::vec4(muzzle, 1.f));
    a.ultimo = muzzle;
    a.vida = 0.5f;  // mais que a vida do fogo
    return slot;
}
void atualizarAncoras(Engine* e, float dt) {
    for (int i = 0; i < ParticleSystem::kAncoras; ++i) {
        Engine::BocaAncora& a = e->ancoras[i];
        if (a.vida <= 0.f) continue;
        GameObject* w = e->scene.get(a.arma);
        if (!w || (a.vida -= dt) <= 0.f) { a.vida = 0.f; e->particles.soltarAncora(i); continue; }
        const glm::vec3 agora = glm::vec3(w->transform.world() * glm::vec4(a.local, 1.f));
        e->particles.moverAncora(i, agora - a.ultimo);
        a.ultimo = agora;
    }
}
void muzzleFx(Engine* e, Scene::Handle weapon, glm::vec3 muzzle, glm::vec3 dir) {
    e->particles.ancoraAtual = ancoraDaArma(e, weapon, muzzle);
    if (Emitter* em = skinShot(e, weapon); em && em->energia) {
        tiroEnergia(e, *em, muzzle, dir);
    } else if (em) {
        addFlash(e, fxDisparoSkin(e->particles, e->fxRng, muzzle, dir, em->corDisparo, em->corDisparo2), 0.08f);
        em->pulso = 1.f;  // a pintura acende forte e apaga (updateEffects)
    } else {
        addFlash(e, fxMuzzleFlash(e->particles, e->fxRng, muzzle, dir), 0.06f);
    }
    e->particles.ancoraAtual = -1;
}
void impactFx(Engine* e, Scene::Handle weapon, glm::vec3 hit, glm::vec3 dir) {
    fxImpact(e->particles, e->fxRng, hit, dir);
    if (Emitter* em = skinShot(e, weapon); em && em->energia) impactoEnergia(e, *em, hit, dir);
}
void bulletFx(Engine* e, Scene::Handle weapon, glm::vec3 from, glm::vec3 to) {
    tiroNasPortas(e, from, to);  // todo tiro (meu, dos robôs e de quem joga online) passa por aqui
    Emitter* em = skinShot(e, weapon);
    if (em && em->energia) feixeEnergia(e, from, to);
    fxBullet(e->particles, from, to, em ? &em->corDisparo : nullptr);
}

void updateEffects(Engine* e, float dt) {
    e->castCooldown -= dt;
    // emissores (rodam também no editor, para ver o efeito enquanto monta a cena)
    e->emitters.forEach([&](Emitter& em, EmitterPool::Handle) {
        GameObject* o = em.owner;
        if (!o || !o->active) return;
        if (em.disparo && em.pulso > 0.f && !em.energia) {  // pulso do tiro: a pintura e a borda acendem e apagam
            em.pulso = std::max(0.f, em.pulso - dt * 7.f);
            const float k = em.pulso * em.pulso;
            Scene::forEachInSubtree(*o, [&](GameObject& p) {
                if (!p.meshRenderer) return;
                e->renderer.setEmissive(p.meshRenderer->material, glm::vec3(em.emiBase * (1.f + 2.5f * k)));
                e->renderer.setRim(p.meshRenderer->material, glm::vec4(glm::vec3(em.rimBase), em.rimBase.w * (1.f + 2.f * k)));
            });
        }
        if (em.kind == FX_Raios && em.estilo < 0) return;  // só o tiro (sem efeito contínuo)
        if (em.kind == FX_Raios) {
            em.timer += dt;
            const glm::vec3 agora = o->transform.worldPosition();
            glm::vec3 vel = em.temUltimo && dt > 1e-4f ? (agora - em.ultimo) / dt : glm::vec3(0.f);
            if (glm::length(vel) > 15.f) vel = glm::vec3(0.f);  // renasceu/teleportou: não é movimento
            em.ultimo = agora;
            em.temUltimo = true;
            if (em.estilo >= 1)
                fxAuraArma(e->particles, e->fxRng, o->transform.world(), em.a, em.b, em.raio, em.color, dt, em.accum, em.accum2, em.accum3, em.accum4, em.accum5, vel, em.estilo == 2, em.timer);
            else
                fxRaios(e->particles, e->fxRng, o->transform.world(), em.a, em.b, em.raio, em.color, dt, em.accum, em.accum2, em.accum3, vel);
            return;
        }
        glm::vec3 pos = o->transform.worldPosition();
        float s = std::max(0.1f, o->transform.scale.x);
        FxLight l = fxContinuous(e->particles, e->fxRng, em.kind, em.palette(), pos, s, dt, em.accum, em.timer);
        if (em.kind == FX_Explosion && l.intensity > 0.f) addFlash(e, l, 0.35f);
    });
    // projéteis de magia
    for (auto& p : e->projectiles) {
        if (!p.active) continue;
        if (!e->playing) { p.active = false; continue; }
        glm::vec3 next = p.pos + p.vel * dt;
        glm::vec3 hit; GameObject* obj = nullptr; float frac;
        bool any = e->physics.raycastIgnoring(p.pos, next, p.ignore, hit, obj, frac);
        p.life -= dt;
        if (any || p.life <= 0.f) {
            glm::vec3 at = any ? hit : next;
            addFlash(e, fxExplosion(e->particles, e->fxRng, p.pal, at, any ? 0.8f : 0.4f), 0.4f);
            if (any) {
                radialImpulse(e, at, 4.5f, 9.f, p.ignore);
                damageRadius(e, at, 4.5f, 45.f);
            }
            p.active = false;
            continue;
        }
        p.pos = next;
        fxSpellTrail(e->particles, e->fxRng, p.pal, p.pos, p.vel, dt);
    }
    for (auto& f : e->flashes) f.life -= dt;
    for (auto& b : e->bolts) b.life -= dt;
    e->particles.update(dt);
}

int gatherLights(Engine* e, PointLight* out, int max) {
    int n = std::min(e->pointCount, max);
    for (int i = 0; i < n; ++i) out[i] = e->points[i];
    struct Cand { FxLight l; float score; };
    Cand c[64];
    int nc = 0;
    glm::vec3 cam = e->camera.position;
    auto push = [&](const FxLight& l) {
        if (l.intensity <= 0.f || nc >= 64) return;
        glm::vec3 d = l.pos - cam;
        c[nc++] = {l, glm::dot(d, d) / (l.intensity + 1e-3f)};
    };
    e->emitters.forEach([&](Emitter& em, EmitterPool::Handle) {
        GameObject* o = em.owner;
        if (!o || !o->active || em.kind == FX_Smoke || em.kind == FX_Explosion) return;
        if (em.energia && em.luzEnergia > 0.f) {  // núcleo da skin de energia: luz roxa
            push({em.luzPos, glm::vec3(0.63f, 0.38f, 1.f), em.luzEnergia, 1.75f});
            return;
        }
        if (em.kind == FX_Raios && em.estilo < 0) return;
        if (em.kind == FX_Raios) {  // luz azul fraca no meio da arma, piscando como eletricidade
            const float pisca = em.estilo == 2 ? (std::sin(em.timer * 47.f) * std::sin(em.timer * 13.1f) > -0.2f ? 1.f : 0.35f)
                                               : 0.65f + 0.35f * std::sin(em.timer * 31.f) * std::sin(em.timer * 11.7f);
            push({glm::vec3(o->transform.world() * glm::vec4((em.a + em.b) * 0.5f, 1.f)), em.color, (em.estilo >= 1 ? 2.2f : 1.4f) * pisca, 2.8f});
            return;
        }
        float s = std::max(0.1f, o->transform.scale.x);
        float flick = 0.85f + 0.3f * std::sin(em.timer * 23.f) * std::sin(em.timer * 7.3f);
        float inten = (em.kind == FX_Torch ? 2.5f : em.kind == FX_Sparks ? 3.f : 6.f) * s * flick;
        push({o->transform.worldPosition() + glm::vec3(0, 0.8f * s, 0), em.palette().light, inten, 7.f * s});
    });
    for (auto& p : e->projectiles) if (p.active) push({p.pos, p.pal.light, 8.f, 8.f});
    for (const LightObject& lo : e->lightObjects)
        if (GameObject* o = e->scene.get(lo.obj); o && o->active) push({o->transform.worldPosition(), lo.color, lo.intensity, lo.radius});
    for (auto& f : e->flashes)
        if (f.life > 0.f) { FxLight l = f.light; l.intensity *= f.life / f.maxLife; push(l); }
    for (auto& b : e->bolts) if (b.life > 0.f) push({b.pts[b.n / 2], b.color, 20.f, 14.f});
    // postes acesos à noite: só os mais perto da câmera (o shader tem poucas luzes)
    if (e->night > 0.05f && !e->mapLamps.empty()) {
        std::pair<float, int> near[24];
        int nn = 0;
        for (int i = 0; i < int(e->mapLamps.size()); ++i) {
            glm::vec3 d = e->mapLamps[i] - cam;
            float d2 = glm::dot(d, d);
            if (d2 > 90.f * 90.f) continue;
            if (nn < 24) near[nn++] = {d2, i};
            else {
                int worst = 0;
                for (int k = 1; k < 24; ++k) if (near[k].first > near[worst].first) worst = k;
                if (d2 < near[worst].first) near[worst] = {d2, i};
            }
        }
        for (int k = 0; k < nn; ++k) push({e->mapLamps[near[k].second], glm::vec3(1.f, 0.78f, 0.5f), 110.f * e->night, 20.f});
    }
    std::sort(c, c + nc, [](const Cand& a, const Cand& b) { return a.score < b.score; });
    for (int i = 0; i < nc && n < max; ++i) out[n++] = {c[i].l.pos, c[i].l.radius, c[i].l.color, c[i].l.intensity};
    return n;
}

void drawEffectsOverlay(Engine* e, const glm::mat4& vp, bool editor) {
    for (auto& b : e->bolts) {
        if (b.life <= 0.f) continue;
        float a = std::min(1.f, b.life * 10.f);
        for (int i = 0; i + 1 < b.n; ++i) {
            e->renderer.line(vp, b.pts[i], b.pts[i + 1], glm::vec4(b.color, 0.35f * a), 14.f, true);
            e->renderer.line(vp, b.pts[i], b.pts[i + 1], glm::vec4(glm::mix(b.color, glm::vec3(1.f), 0.7f), a), 3.5f, true);
        }
    }
    if (!editor) return;
    // marcador dos emissores (para achar e selecionar no editor)
    e->emitters.forEach([&](Emitter& em, EmitterPool::Handle) {
        if (!em.owner) return;
        glm::vec3 p = em.owner->transform.worldPosition();
        glm::vec4 col(em.palette().glow, 0.9f);
        float s = 0.35f;
        e->renderer.line(vp, p + glm::vec3(-s, 0, 0), p + glm::vec3(s, 0, 0), col, 2.f, false);
        e->renderer.line(vp, p + glm::vec3(0, 0, -s), p + glm::vec3(0, 0, s), col, 2.f, false);
        e->renderer.line(vp, p, p + glm::vec3(0, 2.f * s, 0), col, 2.f, false);
    });
}

// ---------------- modelo importado -> personagem ----------------
// procura nas animações do próprio modelo (com retarget, elas ficam depois das do motor)
static int findClip(const Animator* a, std::initializer_list<const char*> keys) {
    int from = a->retarget ? a->refCount : 0;
    for (int i = from; i < a->clipCount; ++i) {
        const AnimationClip* c = a->clipAt(i);
        if (!c) continue;
        std::string n = c->name;
        for (auto& ch : n) ch = char(std::tolower((unsigned char)ch));
        if (n.find("arma") != std::string::npos) continue;  // variantes armadas do motor
        for (const char* k : keys) if (n.find(k) != std::string::npos) return i;
    }
    return -1;
}

}  // namespace eng

namespace eng {
// Aberturas: caixa de cada uma (no mundo) para o shader e para a física
void updateCuts(Engine* e) {
    e->renderer.cutCount = 0;
    e->physics.cuts.clear();
    for (size_t i = 0; i < e->openings.size();) {
        const Opening& o = e->openings[i];
        GameObject* g = e->scene.get(o.root);
        if (!g) { e->openings.erase(e->openings.begin() + long(i)); continue; }  // apagada: a parede fecha de novo
        ++i;
        if (!g->active || e->renderer.cutCount >= Renderer::kMaxCuts) continue;
        glm::mat4 box = g->transform.world() * glm::translate(glm::mat4(1.f), o.center) * glm::scale(glm::mat4(1.f), o.size);
        Renderer::CutBox& c = e->renderer.cuts[e->renderer.cutCount++];
        c.worldToBox = glm::inverse(box);
        c.bounds = {glm::vec3(1e30f), glm::vec3(-1e30f)};
        for (int k = 0; k < 8; ++k) {
            glm::vec3 p = glm::vec3(box * glm::vec4(k & 1 ? 0.5f : -0.5f, k & 2 ? 0.5f : -0.5f, k & 4 ? 0.5f : -0.5f, 1.f));
            c.bounds.min = glm::min(c.bounds.min, p);
            c.bounds.max = glm::max(c.bounds.max, p);
        }
        e->physics.cuts.push_back(c.worldToBox);
    }
}
}  // namespace eng

namespace eng {
void updateLamps(Engine* e) {
    // luz quente de vapor de sódio; de dia o vidro fica apagado (um pouco claro)
    e->renderer.setEmissive(e->mats[int(Mat::Lamp)], glm::vec3(1.f, 0.8f, 0.55f) * (0.05f + 7.f * e->night));
}
}  // namespace eng
