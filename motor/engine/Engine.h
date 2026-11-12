#pragma once
#include <unordered_map>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "assets/GltfLoader.h"
#include "scene/EditorCamera.h"
#include "physics/Physics.h"
#include "procgen/Buildings.h"
#include "procgen/Character.h"
#include "procgen/TextureGen.h"
#include "render/Effects.h"
#include "render/Particles.h"
#include "render/Renderer.h"
#include "scene/Animator.h"
#include "scene/Scene.h"

namespace eng {

// Colisão de cenário gerado (mapas): corpo estático com forma composta, sem GameObject
struct StaticCollision {
    btCollisionShape* shape = nullptr;
    std::vector<btCollisionShape*> children;
    btRigidBody* body = nullptr;
};

// Objeto que segue um osso (arma na mão)
struct Attachment {
    Scene::Handle obj;
    Animator* anim = nullptr;
    int joint = 0;
    glm::mat4 offset{1.f};
};

struct CharacterInfo {
    Scene::Handle root;        // GameObject raiz (origem nos pés)
    Scene::Handle holster;     // a arma guardada no corpo (fuzil nas costas, pistola na coxa)
    Scene::Handle hair;        // cabelo 3D preso na cabeça (some na primeira pessoa do próprio jogador)
    Animator* anim = nullptr;
    float height = 1.75f;
    int clipMap[CC_Count];     // animação do motor (CharClip) -> índice no Animator
    int handJoint = CJ_RHand;  // osso da mão direita (arma)
    glm::mat4 handFix{1.f};    // corrige o referencial da mão de modelos importados
    Scene::Handle weapon;      // arma equipada (opcional)
    std::vector<Scene::Handle> gear;  // equipamento preso aos ossos (colete, cinto, boné, corrente...)
    int weaponKind = -1;
    std::string shotSound;     // som do tiro/golpe da arma equipada (ex.: "tiro_ak47")
    glm::vec3 muzzle{0.f};     // ponta do cano no espaço da arma
    float aimTimer = 0.f;      // > 0: mirando/atacando (arma apontada para frente)
    bool aimHold = false;      // botão "Mirar" segurado (jogador)
    bool crouch = false;       // agachado (anda devagar, mira agachado)
    float fireAnim = 0.f;      // > 0: acabou de atirar (animação de tiro, com coice)
    int ammo = 30;             // balas no pente (fuzil/escopeta/sniper do jogador)
    float reloadTimer = 0.f;   // > 0: recarregando (não atira)
    bool dying = false;        // tocando a animação de morte
    // mão esquerda na arma (armas longas): ponto do guarda-mão no espaço da arma e peso do IK (0..1)
    bool hasSupport = false;
    glm::vec3 support{0.f};
    float supportW = 0.f;
    // mira com fuzil: alça de mira no espaço da arma; a arma vai para a frente do olho direito e as mãos vão até ela
    glm::vec3 sight{0.f, 0.08f, 0.1f};
    glm::vec3 butt{0.f, 0.03f, -0.25f};  // meio da soleira da coronha (vai no bolso do ombro)
    glm::vec3 gripPt{0.f, -0.04f, 0.f}, guardPt{0.f, 0.01f, 0.25f};  // centro do cabo e do guarda-mão (espaço da arma)
    // Doze (escopeta de bomba): a peça "bomba" desliza para trás e volta depois de cada tiro (a mão de apoio junto) e a
    // recarga é cartucho por cartucho (a mão vai até a janela de baixo a cada um; atirar interrompe)
    std::vector<Scene::Handle> bombaObjs;
    std::vector<glm::vec3> bombaBase;
    float bombaT = 0.f, bombaOff = 0.f;  // tempo restante do movimento / quanto está para trás agora (m)
    bool bombaSom = false;               // o som da bomba já tocou neste movimento
    bool cartucho = false;               // recarregando cartucho por cartucho
    bool silenciada = false;             // arma com silenciador: tiro baixo e sem clarão
    bool precisaBombear = false;         // esvaziou: depois do último cartucho, bombeia
    float adsW = 0.f;
    // primeira pessoa (só o jogador): inclinação do olhar (radianos, + para cima) e se a câmera está no olho
    float viewPitch = 0.f;
    bool firstPerson = false;
    bool scope = false;        // arma com luneta (mirando: a tela da luneta, com mais zoom)
    float raiseW = 0.f;        // arma apontada para a frente (atirando sem mirar); adsW: no olho (mirando)
    // arma da partida de times: dano por tiro/bago, intervalo, dispersão, bagos, queda do dano com a distância,
    // pente e balas de reserva (-1 = sem fim). gunDmg 0 = armas fora da partida (tabela antiga por tipo)
    float gunDmg = 0.f, gunInterval = 0.1f, gunSpread = 0.f, gunFalloff = 1e9f;
    int gunPellets = 1, magSize = 30, reserve = -1;
    int shots = 0;             // tiros dados (online: os outros celulares mostram o tiro quando o número sobe)
    // pular janela (ou mureta): caminho do pé em 3 pontos, de A (antes) por B (no vão) até C (do outro lado)
    float vaultT = -1.f, vaultDur = 0.6f;   // < 0: não está pulando janela; 0..1 progresso
    glm::vec3 vaultA{0.f}, vaultB{0.f}, vaultC{0.f}, vaultDir{0.f, 0.f, 1.f};
    bool wasAir = false, landing = false;  // pulo: estava no ar no quadro anterior / tocando a aterrissagem
    float jumpHold = 0.f;      // > 0: acabou de pular (conta como no ar mesmo com o chão ainda perto do pé)
    bool airborne = false;     // no ar (pulando/caindo): online, os outros veem o pulo
};
// Animação de morte (não repete); sem ela, congela parado
void playDeath(CharacterInfo& c);

// Modelo reaproveitado várias vezes (árvores, casas do mundo infinito): instancing automático
struct ProtoPart { MeshId mesh; MaterialId mat; float lodNear = 0.f, lodFar = 1e30f; glm::mat4 local{1.f}; bool bomba = false; };  // bomba: peça que desliza (Doze)
struct Prototype {
    std::string name;
    std::vector<ProtoPart> parts;
    std::vector<BoxCollider> boxes;   // colliders locais (casas com paredes); vazio = usa colliderHalf
    glm::vec3 colliderHalf{0.f};
    float colliderY = 0.f;     // centro do collider acima da base
};

struct TerrainChunk {
    int cx = 0, cz = 0;
    bool used = false;
    MeshId mesh = kInvalidId;
    Scene::Handle ground;
    std::vector<float> heights;  // (res+1)^2, para o heightfield do Bullet (precisa continuar vivo)
    btCollisionShape* hfShape = nullptr;
    btRigidBody* hfBody = nullptr;
    StaticCollision props;
    std::vector<Scene::Handle> objects;
    std::vector<MeshId> geoMeshes;   // malhas de construções/ruas deste chunk (voltam para freeGeoMeshes)
};

// ---- Lógica de jogo (Game.cpp)
enum BehaviorType { BH_Enemy, BH_Health, BH_Collect, BH_Goal, BH_Spawner, BH_DamageZone, BH_Rotate, BH_Float, BH_Platform,
                    BH_Explosive, BH_Count };
struct Behavior {
    int type = BH_Health;
    Scene::Handle obj;
    // parâmetros (o significado depende do tipo)
    float vida = 100.f, velocidade = 3.f, dano = 10.f, alcance = 25.f, raio = 2.f, intervalo = 5.f, tempo = 3.f;
    int pontos = 10, maximo = 6, total = 0, element = 0;
    float cura = 0.f;
    bool ranged = false;
    std::string armaInimigo;     // spawner: arma dos inimigos criados
    glm::vec3 destino{0.f};
    // estado em tempo de jogo
    float timer = 0.f, hp = 100.f, deadTime = 0.f, accum = 0.f, fxTimer = 0.f;
    int spawnedCount = 0, alive = 0;
    bool dead = false, aggro = false, done = false;
    glm::vec3 start{0.f};
    glm::quat startRot{1, 0, 0, 0};
    int parent = -1;             // inimigo criado por um spawner (índice do spawner)
    bool kinematic = false;
};
struct GameRules {
    std::string objetivo = "livre";  // livre, sobreviver, coletar, chegar, eliminar, pontos
    int quantidade = 0;
    float tempo = 0.f;               // limite de tempo (s); 0 = sem limite
    float vidaJogador = 100.f;
    std::string titulo, descricao;
};
struct GameState {
    int state = 0;                   // 0 jogando, 1 vitória, 2 derrota
    float time = 0.f;
    int score = 0, collected = 0, killed = 0, totalCollect = 0, totalEnemies = 0;
    float playerHp = 100.f, playerMax = 100.f, hurtTimer = 0.f;
    std::vector<Behavior> behaviors;
    std::vector<Scene::Handle> spawned;           // criados durante o Play (apagados no Stop)
    std::vector<Scene::Handle> hidden;            // escondidos durante o Play (voltam no Stop)
    std::vector<RigidBody*> disabledBodies;
    std::string message;                          // aviso curto (ex.: "Vitória!")
};
struct Engine;
// Jogo rodando por cima do motor (como os scripts de um jogo): o motor só avisa os momentos e o jogo decide.
// O motor não conhece as regras do jogo; o jogo instala o seu módulo em Engine::module.
struct GameModule {
    virtual ~GameModule() = default;
    virtual void onPlayStart(Engine*) {}                       // Play: monta a partida
    virtual void onPlayStop(Engine*) {}                        // Stop: antes de o motor desfazer o Play
    virtual void onPlayEnded(Engine*) {}                       // Stop: depois (objetos criados no Play já saíram)
    virtual bool onUpdate(Engine*, float) { return false; }    // true = o jogo cuidou das regras neste quadro
    virtual bool onDamage(Engine*, GameObject*, float) { return false; }  // true = o jogo cuidou do dano
    virtual bool onInteract(Engine*) { return false; }         // botão de interagir (true = usado)
    virtual void onProjectLoaded(Engine*) {}                   // mapa/projeto aberto (pré-carregar o que precisar)
    virtual std::string statusText(Engine*) { return ""; }     // texto do objetivo ("" = o do motor)
    virtual std::string interactLabel(Engine*) { return ""; }  // botão de interagir do jogo ("" = nenhum)
};

// Porta em jogo: abre sozinha quando alguém chega perto
// porta: abre para longe de quem chega; levando tiro, abre para o lado para onde a bala ia e fica aberta um tempo
struct DoorRT { Scene::Handle h; glm::quat base{1, 0, 0, 0}; float angle = 0.f, w = 1.f, tiroLado = 0.f, tiroTempo = 0.f; };
// Abertura cortada numa parede/muro (porta, janela, vão): caixa local (centro e tamanho) em relação à raiz
struct Opening { Scene::Handle root; glm::vec3 center, size; };
// Elevador/passagem: pontos em coordenadas locais do objeto `root` (acompanha se a construção for movida)
struct TeleRT { Scene::Handle root; glm::vec3 a, b; std::string la, lb; };
struct LightObject { Scene::Handle obj; glm::vec3 color{1.f}; float intensity = 8.f, radius = 10.f; };

enum MapKind { MAP_None = -1 };  // mapKind >= 0: índice do mapa no cadastro (engine/MapKit.h)


struct Tracer { glm::vec3 a, b; float life = 0.f; };

// Efeitos em andamento
struct FxFlash { FxLight light{}; float life = 0.f, maxLife = 0.f; };
struct Projectile {
    bool active = false;
    glm::vec3 pos{0.f}, vel{0.f};
    FxPalette pal{};
    int element = 0;
    float life = 0.f;
    const btCollisionObject* ignore = nullptr;
};
struct Bolt { glm::vec3 pts[14]; int n = 0; glm::vec3 color{1.f}; float life = 0.f; };
using EmitterPool = ObjectPool<Emitter, 256>;

struct Engine {
    Renderer renderer;
    Scene scene;
    Physics physics;
    Camera camera;
    EditorCamera editorCam;
    DirectionalLight sun;
    Environment env;
    PointLight points[kMaxPointLights];
    int pointCount = 0;

    // Componentes (pools de capacidade fixa)
    MeshRendererPool meshRenderers;
    AnimatorPool animatorPool;
    RigidBodyPool bodies;
    EmitterPool emitters;

    // Efeitos
    ParticleSystem particles;
    Rng fxRng{7};
    FxFlash flashes[16];
    // Boca do cano de cada arma que atirou agora há pouco: o fogo do disparo anda preso a ela (ver muzzleFx)
    struct BocaAncora { Scene::Handle arma{}; glm::vec3 local{0.f}, ultimo{0.f}; float vida = 0.f; };
    BocaAncora ancoras[ParticleSystem::kAncoras];
    Projectile projectiles[32];
    Bolt bolts[8];
    float castCooldown = 0.f;

    std::vector<std::unique_ptr<LoadedModel>> models;  // glTF e personagens procedurais
    std::unordered_map<std::string, std::pair<std::vector<ProtoPart>, AABB>> weaponParts;  // peças de cada arma já carregada
    std::vector<Scene::Handle> objects;                // id público -> handle
    std::vector<Animator*> animators;
    std::vector<CharacterInfo> characters;
    std::unique_ptr<LoadedModel> humanoidRef;            // esqueleto + animações de referência (retarget)
    std::vector<std::unique_ptr<Retarget>> retargets;
    std::vector<Attachment> attachments;

    // Malhas e materiais básicos (recriados a cada cena nova)
    MeshId cube = kInvalidId, sphere = kInvalidId, capsule = kInvalidId, rock = kInvalidId;
    std::vector<glm::vec3> rockHull;
    MaterialId matFloor = kInvalidId;
    MaterialId matPalette[6] = {};
    MaterialId mats[int(Mat::Count)] = {};
    MaterialId matPonto = kInvalidId;  // o ponto vermelho da mira holográfica (aceso)
    std::map<uint64_t, MaterialId> tintCache;

    // Mapa atual
    int mapKind = MAP_None;
    uint32_t mapSeed = 1;
    bool hasBounds = false;
    glm::vec2 boundsMin{0.f}, boundsMax{0.f};
    std::vector<StaticCollision> statics;
    std::vector<TerrainChunk> chunks;
    std::vector<MeshId> freeGeoMeshes;   // malhas de cidade de chunks descarregados (reaproveitadas)
    std::vector<MeshId> freeChunkMeshes;
    std::map<std::string, Prototype> veg;  // plantas do kit carregadas nesta cena (vegPrototype)
    Scene::Handle water;

    // Jogador
    int player = -1;                 // índice em characters
    glm::vec2 moveInput{0.f};
    bool runInput = false, jumpRequest = false, fireRequest = false, aimInput = false, crouchInput = false, reloadRequest = false;
    int scopeZoom = 0;                   // luneta: 0 zoom pequeno, 1 médio, 2 grande (botões + e -)
    float fireCooldown = 0.f;
    Tracer tracers[16];
    struct CamState { glm::vec3 target; float yaw, pitch, distance; } savedCam{};

    // Projeto
    std::vector<std::string> recipes;   // JSON de criação de cada objeto raiz (GameObject::recipe)
    uint64_t revision = 0;              // muda a cada edição (salvamento automático)
    std::string projectName = "Projeto";
    float timeOfDay = 13.f;
    float night = 0.f;                   // 0 dia .. 1 noite (postes acesos)
    // dia/noite e chuva (Weather.cpp): valem no Play
    float dayCycleMin = 0.f;             // minutos de um dia inteiro (0 = a hora fica parada)
    int rainMode = 0;                    // 0 nunca, 1 automática (em algum momento do ciclo), 2 sempre
    float rain = 0.f, rainTarget = 0.f, rainClock = 0.f, cycleAccum = 0.f;
    float lightning = 0.f, lightningNext = 0.f, thunderIn = -1.f;
    float windClock = 0.f;               // rajadas de vento na chuva
    static constexpr int kRainGrid = 25;
    std::vector<float> rainCover;
    glm::vec2 rainCoverOrigin{0.f}, rainCoverCenter{1e9f};
    float rainCoverTimer = 0.f, playStartHour = 13.f;
    // sons pedidos pelo jogo (o app lê e toca: tiros, recarga, trovão...)
    struct SoundEvent { std::string name; glm::vec3 pos; float gain; bool positional; };
    std::vector<SoundEvent> sounds;
    std::string soundText;
    std::vector<glm::vec3> mapLamps;     // postes do mapa
    std::vector<LightObject> lightObjects;
    std::vector<DoorRT> doors;
    std::vector<Opening> openings;  // máx. Renderer::kMaxCuts ativas
    std::vector<TeleRT> teles;
    MeshId doorMesh = kInvalidId;
    GameRules rules;
    GameState game;
    std::unique_ptr<GameModule> module;   // o jogo (regras próprias), instalado por ele
    float nearOverride = 0.f;  // > 0: plano de corte próximo fixo (primeira pessoa: não desenha a própria cabeça)
    // Primeira pessoa (jogo): câmera no olho do jogador, cabeça escondida, arma e braços à vista
    bool firstPerson = false;
    glm::vec3 fpEye{0.f};
    bool fpEyeValid = false;
    bool fpScoped = false;      // olhando pela luneta (arma e braços escondidos)
    float baseFov = 60.f;
    float hitMarker = 0.f;      // > 0: acertou alguém agora (marcador no centro da tela)
    bool hitHead = false;
    std::string toolResult;             // texto devolvido pela API C (fica vivo até a próxima chamada)

    bool playing = false;
    bool previewAnim = true;          // animações rodando também no editor
    int width = 1, height = 1;
    Scene::Handle selected;
    std::string lastError;

    // Snapshot para o Stop restaurar o estado do editor
    struct Snap { Scene::Handle h; glm::vec3 p, s; glm::quat r; };
    std::vector<Snap> snapshot;
    struct AnimSnap { float time; int clip; };
    std::vector<AnimSnap> animSnap;

    // Toque
    bool tapCandidate = false, gizmoGrabbed = false;
    glm::vec2 tapStart{0.f};

    // Estatísticas
    float statTime = 0.f, fps = 0.f, frameMs = 0.f, cpuMs = 0.f, cpuAccum = 0.f;
    // perfil do último quadro (ms): mundo (carregar pedaços), montar a lista de desenho, render (CPU do GL)
    float profWorld = 0.f, profSubmit = 0.f, profRender = 0.f, profGame = 0.f;  // profGame: jogo, física e animações
    int quality = 2;  // 0 desempenho (sem sombra, vê menos longe), 1 equilibrado, 2 alta
    int nivel = 2;    // nível gráfico do jogo: 0 Leve, 1 Padrão, 2 Intermediária, 3 Ultra (applyQualityLevel)
    bool lowTextures = false;  // celular fraco (qualidade.txt = 0 ao abrir): texturas na metade e terreno simples
    int statFrames = 0;
};

// ---- cena (Engine.cpp)
GameObject* objectById(Engine* e, int id);
int registerObject(Engine* e, Scene::Handle h);
glm::mat4 renderMatrix(const Engine* e, const GameObject& go, const glm::mat4** palette);
bool subtreeBounds(Engine* e, GameObject& root, AABB& out);
void applyQualityLevel(Engine* e, int nivel);  // 0 Leve · 1 Padrão · 2 Intermediária · 3 Ultra
void warmWeaponModel(Engine* e, const std::string& file, int kind);  // carrega as peças de uma arma (sem pôr na cena)
void releaseComponents(Engine* e, GameObject& g);
void ensureBasics(Engine* e);
void clearScene(Engine* e);
void addFloor(Engine* e, float size = 40.f);
void selectHandle(Engine* e, Scene::Handle h);
Scene::Handle addMeshObject(Engine* e, const char* name, MeshId mesh, MaterialId mat, glm::vec3 pos, glm::vec3 scale,
                            glm::vec3 eulerDeg = glm::vec3(0.f), bool reg = true);
void addBody(Engine* e, Scene::Handle h, ColliderType type, float mass);
// Transforma Geo em GameObjects. local=true: Geo montado na origem; vira objeto editável (raiz registrada,
// collider composto que acompanha o objeto). local=false: cenário de mapa em coordenadas de mundo.
Scene::Handle spawnGeo(Engine* e, Geo& g, const char* name, bool local, glm::vec3 pos = glm::vec3(0.f), float yaw = 0.f,
                       float detailFar = 160.f);
// Distâncias de LOD (metros)
constexpr float kDetailFar = 160.f;   // carros, postes, caixas d'água...
constexpr float kInteriorFar = 40.f;  // móveis e paredes internas
constexpr float kTreeLod = 70.f;      // árvore completa -> simplificada
constexpr float kTreeFar = 420.f;     // árvore some (neblina)
void addStaticBoxes(Engine* e, const std::vector<BoxCollider>& boxes, StaticCollision* into = nullptr);
void freeStatic(Engine* e, StaticCollision& s);
MaterialId tintedMaterial(Engine* e, MaterialId base, glm::vec3 color);

// ---- personagens e armas (Engine.cpp)
int createCharacter(Engine* e, const CharacterParams& p, glm::vec3 pos, float yaw, bool controllable);
// Aparência da arma: modelo de arquivo (vazio = a do motor) e acessórios encaixados
enum WeaponAccessory { ACC_RedDot = 1, ACC_Stock = 2, ACC_Foregrip = 4,
                       // acessórios do jogo (montagem.json): boca, mira, pente e coronha
                       ACC_Silenciador = 8, ACC_Freio = 16, ACC_Holo = 32, ACC_Luneta = 64, ACC_Pente = 128, ACC_Coronha = 256 };
// o que os acessórios montados mudaram na arma (espaço da arma)
struct AcessorioInfo { bool temBoca = false, temMira = false, luneta = false, temPente = false, temCoronha = false; glm::vec3 muzzle{0.f}, sight{0.f}; };
bool montarAcessorios(Engine* e, GameObject& weapon, const std::string& file, uint32_t acc, const glm::mat4& fix, AcessorioInfo& info);
struct WeaponLook {
    std::string file;        // .glb/.gltf/.fbx/.obj: deitado com o cano para a frente e no tamanho do tipo da arma
    bool flip = false;       // o arquivo veio com o cano para trás: vira 180°
    float size = 1.f;        // multiplica o tamanho padrão do tipo
    uint32_t accessories = 0;
};
int createWeapon(Engine* e, int kind, int character, const WeaponLook& look = {});
void comecarBomba(CharacterInfo& c);         // Doze: bombeia (depois do tiro)
void atualizarBombas(Engine* e, float dt);   // a bomba de cada Doze deslizando
int holsterWeapon(Engine* e, int ch, int kind, const WeaponLook& look);  // guarda a arma no corpo (-1 tira)
int attachGearMesh(Engine* e, int ch, int charJoint, Geo& g, const char* name);  // peça montada no espaço do personagem, presa ao osso
std::string accessoriesList(uint32_t accessories);  // ["mira","coronha",...] em JSON   // character < 0: solto no chão
void setPlayer(Engine* e, int character);
void updatePlayer(Engine* e, float dt);
void updateAttachments(Engine* e);
// Mão esquerda segura o guarda-mão das armas longas (IK de dois ossos depois da animação)
void updateSupportHands(Engine* e, float dt);
// Primeira pessoa: olho do jogador (antes do IK) e câmera/cabeça (depois do IK)
void updateFirstPersonEye(Engine* e, float dt);
void applyFirstPerson(Engine* e);
void updateDoors(Engine* e, float dt);                 // abre/fecha portas perto de personagens
std::string interactLabel(Engine* e, int* which = nullptr);  // botão de interação do jogador ("" = nenhum)
bool interact(Engine* e);

// ---- efeitos (Effects no Engine.cpp)
int createEffect(Engine* e, int kind, int element);              // objeto com emissor contínuo
void spawnEffect(Engine* e, int kind, int element, glm::vec3 pos); // efeito instantâneo (explosão...)
void castSpell(Engine* e, int element);                          // magia do jogador
void addFlash(Engine* e, const FxLight& l, float life);
void updateEffects(Engine* e, float dt);
int gatherLights(Engine* e, PointLight* out, int max);           // luzes da cena + luzes dos efeitos
void drawEffectsOverlay(Engine* e, const glm::mat4& vp, bool editor);

// ---- personagens: movimento compartilhado por jogador e inimigos (Engine.cpp)
bool driveCharacter(Engine* e, CharacterInfo& c, glm::vec3 move, float speed, bool jump, float dt, glm::vec3 face = glm::vec3(0.f));
void animateCharacter(CharacterInfo& c, const glm::quat& rot, glm::vec3 vel, bool grounded, bool airborne);  // animação pela velocidade
void deleteObject(Engine* e, Scene::Handle h);

// Atualiza as caixas das aberturas no renderizador e na física (antes de desenhar)
void updateCuts(Engine* e);
// Lâmpadas dos postes: brilho conforme a noite
void updateLamps(Engine* e);
// Tempo e chuva (Weather.cpp)
void weatherStart(Engine* e);
void weatherUpdate(Engine* e, float dt);
void weatherLook(const Engine* e, Environment& env, DirectionalLight& sun, float& fog);
void playSound(Engine* e, const char* name, glm::vec3 pos, float gain = 1.f, bool positional = true);

// ---- jogo (Game.cpp)
// ajudas de jogabilidade (o jogo também usa)
CharacterInfo* charOf(Engine* e, GameObject* g);     // personagem cuja raiz é g
CharacterInfo* playerChar(Engine* e);                // personagem do jogador (nullptr = nenhum)
void setSubtreeActive(GameObject& g, bool on);
void hideObject(Engine* e, GameObject& g);           // esconde até o Stop
void hurtPlayer(Engine* e, float dmg);
void gameStart(Engine* e);
void gameStop(Engine* e);
void gameUpdate(Engine* e, float dt);
void damageAt(Engine* e, GameObject* hit, float dmg, glm::vec3 from);
void damageRadius(Engine* e, glm::vec3 c, float radius, float dmg, bool hurtPlayer = false);
int behaviorTypeFromName(const std::string& n);

// ---- projeto (Project.cpp): abre o projeto (cena) em JSON e cria objetos a partir de receitas
int createFromSpecJson(Engine* e, const std::string& specJson, std::string* err);
bool importProject(Engine* e, const std::string& json, std::string* err);
void setTimeOfDay(Engine* e, float hour);
int recordRecipe(Engine* e, GameObject& root, const std::string& specJson);
// Muda um campo da receita do objeto raiz (ex.: cor/material editados no inspetor). valueJson: JSON do valor.
void recipeSet(Engine* e, GameObject& root, const char* key, const std::string& valueJson);
void recipePartSet(Engine* e, GameObject& root, int partIndex, const char* key, const std::string& valueJson);
std::string jsonString(const std::string& s);   // "texto" com escapes JSON
GameObject* rootOf(Engine* e, GameObject* g);   // raiz editável (com id) acima de g
// Peça do kit da cidade (shaders/kit/<nome>.gltf) como protótipo: centrada, base no chão, collider na caixa
bool kitPrototype(Engine* e, const char* name, Prototype& out, float lodFar);
// Planta do kit de vegetação (shaders/kit/vegetacao): 3 níveis de detalhe pela distância. nullptr se não houver.
const Prototype* vegPrototype(Engine* e, const std::string& name);
// Malhas fixas de um arquivo 3D (só os nós cujo nome, ou de um pai, contém `only`; nulo = todos), cada uma com a
// matriz no espaço do arquivo, e a caixa que envolve todas. O modelo carregado é apagado depois.
bool modelParts(Engine* e, const std::string& path, const char* only, std::vector<ProtoPart>& parts, AABB& box,
                std::vector<std::string>* names = nullptr);  // names: nome do nó (e dos pais), normalizado, de cada peça
void spawnPrototype(Engine* e, const Prototype& p, glm::vec3 pos, float yaw, float scale, std::vector<Scene::Handle>* out,
                std::vector<BoxCollider>* colliders);
int loadModelFile(Engine* e, const char* path); // .glb/.gltf/.fbx -> id da raiz (-1 = erro em lastError)
// Equipamento do personagem (shaders/kit/personagens/equip/<nome>.glb, osso em equipamento.txt): preso ao osso
bool attachGear(Engine* e, int character, const std::string& name);
std::string gameStatusText(Engine* e);
// Efeitos do tiro de uma arma: o da skin dela (se tiver "disparo") ou o padrão. Chame no lugar de fxMuzzleFlash/fxBullet.
void muzzleFx(Engine* e, Scene::Handle weapon, glm::vec3 muzzle, glm::vec3 dir);
void atualizarAncoras(Engine* e, float dt);
void atualizarEnergia(Engine* e, float dt);  // skin de energia (depois da arma posicionada: núcleo e cristais colados nela)
bool tiroNasPortas(Engine* e, glm::vec3 de, glm::vec3 ate);  // bala que passa por uma porta: ela abre para o lado do tiro  // depois de posicionar as armas, antes de desenhar
void bulletFx(Engine* e, Scene::Handle weapon, glm::vec3 from, glm::vec3 to);
// impacto da bala onde acertou (o de sempre; skin de energia: estouro roxo com anel e faíscas)
void impactFx(Engine* e, Scene::Handle weapon, glm::vec3 hit, glm::vec3 dir);
void applyKitTexture(Engine* e, GameObject& g, const std::string& kitName, float metal = -1.f, float rough = -1.f);  // textura de cor do kit no objeto
std::string kitPath(Engine* e, const std::string& name);  // caminho de um arquivo do kit ("armas/ak47" -> .../kit/armas/ak47.glb)

// ---- mundo (World.cpp)
void loadMap(Engine* e, int kind, uint32_t seed);   // kind: índice no cadastro de mapas (engine/MapKit.h)
void addBoxCollider(Engine* e, Scene::Handle root, glm::vec3 centerLocal, glm::vec3 half);  // collider estático do objeto
int matIndexByName(const std::string& name);
void clearWorld(Engine* e);
float mapHeight(Engine* e, float x, float z);
Scene::Handle createBuilding(Engine* e, int kind, uint32_t seed, glm::vec3 pos, int floors = 0);  // floors 0 = sorteado
void mapOverview(Engine* e);           // câmera do alto enquadrando o mapa
// Cria portas e elevadores do Geo como filhos de `root` (usado por spawnGeo)
void spawnGeoExtras(Engine* e, Geo& g, Scene::Handle root, bool local, float detailFar);

}  // namespace eng
