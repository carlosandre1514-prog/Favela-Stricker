#pragma once
// Favela Stricker — a partida Polícia x Bandidos: estado, módulo de jogo e o que o app/site chamam.
#include <map>
#include <string>
#include <vector>
#include "engine/Engine.h"

namespace eng {

// Partida de times (Polícia x Bandidos): cada lutador é um personagem criado no Play (cópia do que está no projeto)
struct Fighter {
    Scene::Handle root;            // personagem em jogo
    Scene::Handle original;        // o do projeto (escondido durante o Play)
    std::string name;
    int team = 0;                  // time: 0 polícia, 1 bandido (Ressurgência: 0..9, o time de 4)
    int lado = 0;                  // de que lado é a roupa (0 polícia, 1 bandido): na Ressurgência cada um escolhe
    bool fora = false;             // Ressurgência: caiu sem companheiro vivo (ou na zona final): está fora
    bool bot = true, dead = false;
    float hp = 100.f, deadTime = 0.f;
    int kills = 0, deaths = 0, money = 800;
    int skin = 0;                  // roupa (índice na lista do time)
    std::string weapon = "pistola";
    std::vector<int> owned;        // armas/roupas compradas (ids da loja)
    // jogador: arma principal e secundária (troca rápida), balas de cada uma e granadas
    std::string primary, secondary;
    int slot = 0, mag[2] = {0, 0}, reserve[2] = {0, 0}, grenades = 2;
    // robô
    std::vector<int> path;
    size_t pathPos = 0;
    int target = -1;
    float think = 0.f, fireTimer = 0.f, stuckTimer = 0.f, strafe = 1.f, strafeTimer = 0.f;
    int burst = 0;
    float shotAgo = 99.f;          // segundos desde o último tiro (aparece no minimapa do inimigo)
    float hurtAgo = 99.f;          // segundos desde o último dano (robô: a vida volta fora do combate)
    // online: lutador comandado por outro celular (jogador da sala, ou robô de quem criou a sala)
    bool remote = false, left = false;
    std::string netId;             // id na sala (jogador: id do Firebase; robô: "b<time>_<n>")
    glm::vec3 netPos{0.f};
    float netYaw = 0.f, netPitch = 0.f;
    bool netHas = false, netAlive = true, netCrouch = false, netAim = false, netAir = false, netReload = false;
    glm::vec3 netVel{0.f};         // velocidade (prevê a posição entre um pacote e outro)
    float netAge = 0.f;
    // Ressurgência online: "fora" vem do celular da própria pessoa (cada celular decidia sozinho, e com o atraso da
    // rede um dava a pessoa como fora enquanto ela renascia no dela); netMorto: há quanto tempo o estado chega "caído"
    // sem o evento de abate (evento perdido: cai assim mesmo, senão o time dela ficava de pé para sempre)
    bool netFora = false;
    float netMorto = 0.f;
    int netShots = 0;
    std::string netWeapon;
    std::map<std::string, std::string> gunSkins;  // online: skins das armas de outra pessoa (arma -> skin)
    glm::vec3 lastPos{0.f}, goal{0.f};
};
struct ShopRT { Scene::Handle obj; int kind = 0; int team = 0; };  // kind 0 armas, 1 roupas
struct TeamMatch {
    bool active = false;
    int modo = 0;                         // 0 Polícia x Bandidos (5 x 5), 1 Battle Royale Ressurgência
    bool piloto = false;                  // vídeos/testes: o jogador anda, procura e atira sozinho (como os robôs)
    // Ressurgência: a zona (círculo que fecha em fases), quantos times e em que lugar cada time ficou (0 = vivo)
    struct Zona { glm::vec2 c{0.f}, de{0.f}, para{0.f}; float r = 0.f, rDe = 0.f, rPara = 0.f, t = 0.f, tick = 0.f;
                  int fase = 0; bool fechando = false; };
    Zona zona;
    int times = 2;
    int tamanhoTime = 4;                  // Ressurgência: 1 solo, 2 duo, 3 trio, 4 squad
    std::vector<int> colocacao;
    Rng zonaRng{1};                       // Ressurgência: sorteios da zona e da queda (online: mesma semente em todos)
    std::vector<Fighter> f;
    int score[2] = {0, 0};
    glm::vec3 base[2]{};                  // centro dos pontos de renascimento de cada time
    std::vector<glm::vec3> spawns[2];     // cada "base" do time é um ponto de renascimento
    std::vector<glm::vec3> nav;                // pontos de caminho (escadarias, rua de baixo, patamares)
    std::vector<std::vector<int>> navAdj;
    std::vector<ShopRT> shops;
    int playerTeam = -1;                       // escolhido no app antes do Play (-1: o do personagem)
    int openShop = -1;                         // loja aberta pelo jogador
    int attacker = -1;                         // quem está atirando agora (crédito do abate)
    float feedTimer = 0.f;
    // escolhas do jogador antes do Play (app do jogo): roupa (índice na lista do time) e arma principal
    int playerSkin = -1;
    std::string playerPrimary, playerSecondary;
    std::map<std::string, std::string> gunSkins;   // skin escolhida de cada arma do jogador
    struct Nade { Scene::Handle h; glm::vec3 pos{0.f}, vel{0.f}; float t = 3.f, giro = 0.f; int owner = -1; bool visual = false; };
    std::vector<Nade> nades;                   // granadas no ar/no chão (explodem em 3 s)
    struct Feed { std::string killer, victim, weapon, skin; int kt = 0, vt = 0; bool you = false; float age = 0.f; };
    std::vector<Feed> feed;                    // últimos abates (quadro do HUD)
    float lastDamage = 0.f;                    // tempo desde o último dano no jogador
    // munição que o inimigo abatido derruba (offline): caixinha girando no chão; passando por cima, pega
    struct Drop { Scene::Handle h; glm::vec3 pos{0.f}; float t = 0.f; int pentes = 1; };  // pentes: a munição toda de quem caiu
    std::vector<Drop> drops;
    int playerShots = -1;                      // tiros do jogador (para saber se está trocando tiro)
    float playerShotAgo = 99.f;
    // partida online (sala do Firebase): quem é o jogador local, se ele criou a sala (comanda os robôs),
    // os jogadores da sala e os eventos a enviar (dano, abate, granada) em JSON
    bool online = false, host = false;
    std::string localId, roster;
    std::vector<std::string> netOut;
};

// O jogo instalado no motor: guarda a partida e responde aos avisos do motor
struct Partida : GameModule {
    TeamMatch M;
    void onPlayStart(Engine* e) override;
    void onPlayStop(Engine* e) override;
    void onPlayEnded(Engine* e) override;
    bool onUpdate(Engine* e, float dt) override;
    bool onDamage(Engine* e, GameObject* root, float dmg) override;
    bool onInteract(Engine* e) override;
    void onProjectLoaded(Engine* e) override;
    std::string statusText(Engine* e) override;
    std::string interactLabel(Engine* e) override;
};
void jogoInstalar(Engine* e);   // liga o Favela Stricker no motor (depois de criar o motor)
TeamMatch& matchOf(Engine* e);  // a partida do jogo instalado

// Partida de times: escolha do lado, lojas (o app mostra a lista e compra), caminhos dos robôs
void teamChoose(Engine* e, int team);
void teamMode(Engine* e, int modo);   // antes do Play: 0 Polícia x Bandidos, 1 Ressurgência
// Antes do Play: lado, roupa (0..4 na lista do lado) e arma principal ("m4a1", "ak47"...; "" = a do projeto)
void teamLoadout(Engine* e, int team, int skin, const std::string& primary, const std::string& secondary = "");
bool teamSwapWeapon(Engine* e);     // troca rápida principal <-> segunda arma
std::string teamSkinLook(int team, int skin);  // aparência do boneco da roupa (JSON)
void teamGunSkin(Engine* e, const std::string& gun, const std::string& skin);  // skin da arma do jogador ("" = padrão)
void gunSkinFinish(const std::string& gun, const std::string& skin, float& metal, float& rough);  // acabamento da skin de arma
// modelo da arma com a skin: "kit:armas/<arma>_<skin>" se a skin tem modelo próprio (armas da Meshy), senão o da arma
// alta: a versão detalhada (<arma>_<skin>_alta, metade ou mais dos triângulos do original), só para a arma na mão
// do jogador deste aparelho, fora da qualidade Leve
std::string gunModelFile(Engine* e, const std::string& gun, const std::string& skin, bool alta = false);
bool teamThrowGrenade(Engine* e);   // joga granada para onde a câmera olha
std::string hudJson(Engine* e);     // estado da partida para o HUD do jogo (JSON)
// Online (antes do Play): sala com jogadores [{"id","nome","time","skin","arma"}]; "" sai do modo online
void netRoster(Engine* e, bool host, const std::string& localId, const std::string& rosterJson);
std::string netCollect(Engine* e);                     // estado dos meus lutadores: {"id":{p,y,pi,c,a,w,v,n}}
void netApply(Engine* e, const std::string& states);   // estado dos lutadores dos outros celulares
std::string netTakeEvents(Engine* e);                  // eventos a enviar (array JSON) e limpa a fila
void netEvent(Engine* e, const std::string& ev);       // evento recebido (dano, abate, granada, saiu)
std::string shopLabel(Engine* e);            // botão perto de uma loja ("" = nenhuma)
bool shopInteract(Engine* e);
std::string shopJson(Engine* e);             // loja aberta: itens, preços, dinheiro ("" = nenhuma)
std::string shopBuy(Engine* e, int item);
void shopClose(Engine* e);
int playerFighter(Engine* e);
void preloadMatchWeapons(Engine* e);  // todas as armas da partida (no carregamento do mapa)

}  // namespace eng
