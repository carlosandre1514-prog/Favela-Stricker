#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "assets/GltfLoader.h"
#include "procgen/MeshBuilder.h"

namespace eng {
struct Geo;

// Ossos do humanoide procedural (pais sempre antes dos filhos)
enum CharJoint { CJ_Hips, CJ_Spine, CJ_Chest, CJ_Head, CJ_LUpper, CJ_LFore, CJ_LHand, CJ_RUpper, CJ_RFore, CJ_RHand,
                 CJ_LThigh, CJ_LShin, CJ_LFoot, CJ_RThigh, CJ_RShin, CJ_RFoot, CJ_Count };

// Ossos extras (depois dos 16 humanoides principais): pescoço e dedos (2 articulações cada)
enum CharJointExtra { CJ_Neck = CJ_Count,
                      CJ_LThumb1, CJ_LThumb2, CJ_LIndex1, CJ_LIndex2, CJ_LMiddle1, CJ_LMiddle2, CJ_LRing1, CJ_LRing2, CJ_LPinky1, CJ_LPinky2,
                      CJ_RThumb1, CJ_RThumb2, CJ_RIndex1, CJ_RIndex2, CJ_RMiddle1, CJ_RMiddle2, CJ_RRing1, CJ_RRing2, CJ_RPinky1, CJ_RPinky2,
                      CJ_Total };

// Animações geradas para todo personagem. Com arma: 5 por tipo (parado, andando, correndo, mirando/atacando,
// andando mirando), na ordem de AP_*; a base de cada tipo é CC_Rifle, CC_Pistol ou CC_Melee.
// Depois delas: morte, recarregar, mirando com fuzil andar de costas e de lado (esquerda/direita), atirar e pular.
enum CharClip { CC_Idle, CC_Walk, CC_Run, CC_Dance, CC_Wave,
                CC_Rifle, CC_Pistol = CC_Rifle + 5, CC_Melee = CC_Pistol + 5,
                CC_Death = CC_Melee + 5, CC_Reload, CC_RifleBack, CC_RifleLeft, CC_RifleRight, CC_RifleFire, CC_Jump, CC_CrouchWalk, CC_Count };
// Papel de cada animação (para a biblioteca de captura de movimento: shaders/anim/biblioteca.txt)
const char* clipSlot(int charClip);
enum ArmedPose { AP_Idle, AP_Walk, AP_Run, AP_Aim, AP_AimWalk };
// tipo de pose da arma (WeaponKind abaixo): fuzil/escopeta/sniper, pistola, faca/taco
int armedClipBase(int weaponKind);

struct CharacterParams {
    float height = 1.75f;     // metros
    int build = 1;            // 0 magro, 1 normal, 2 forte
    glm::vec4 skin, shirt, pants, shoes, hair;
    int hairStyle = 1;        // 0 careca, 1 curto, 2 comprido, 3 boné
    bool longSleeves = false;
    // Estilo bloco (parecido com Roblox): cabeça quadrada com rostinho, tronco, braços e pernas em blocos.
    // Os ossos e as animações são os mesmos; só muda a malha (bem mais leve).
    bool bloco = false;
    int chapeu = 0;           // 0 nada, 1 boné, 2 capacete, 3 touca ninja, 4 capacete com viseira
    glm::vec4 chapeuCor{0.f};
    bool colete = false;
    glm::vec4 coleteCor{0.f};
    int estampa = 0;          // camisa: 0 lisa, 1 listras deitadas, 2 listras em pé, 3 faixa diagonal, 4 três cores em pé, 5 gola e punhos
    glm::vec4 cor2{1.f}, cor3{1.f};
    bool bermuda = false;     // perna de fora do joelho para baixo
    glm::vec4 luvas{0.f};     // alfa 0 = sem luva (mão de pele)
    // Skin pintada (imagem no molde de tools/gerar_molde_personagem.py): só os blocos do corpo, com a imagem
    // por cima. molde[parte][face]: (u0, v0, u1, v1) na imagem; partes na ordem de SkinPart, faces na ordem
    // frente, costas, lado esquerdo (+X), lado direito (-X), topo, base.
    std::string cabeloModelo;  // cabelo de modelo 3D (shaders/kit/personagens/cabelos/<nome>.bin); vazio = cabelo3d
    int cabelo3d = 0;         // cabelo 3D de blocos por cima da cabeça: 0 nenhum, 1 cacheado, 2 black, 3 curto, 4 dreads
    bool texturizado = false;
    glm::vec4 molde[8][6];
};
enum SkinPart { SP_Tronco, SP_Cabeca, SP_BracoEsq, SP_BracoDir, SP_PernaEsq, SP_PernaDir, SP_PeEsq, SP_PeDir };

CharacterParams defaultCharacter();
CharacterParams randomCharacter(uint32_t seed);

// Gera esqueleto + animações em `out` (skeletons[0], clips) e a malha com skinning em `mesh`
void buildCharacter(const CharacterParams& p, LoadedModel& out, MeshBuilder& mesh);
// Colete do boneco em blocos como peça separada (malha no espaço do personagem; vai preso no osso do peito)
void buildBlockVest(const CharacterParams& p, MeshBuilder& mesh);
// Cabelo 3D (p.cabelo3d): touca arredondada + espirais (cacheado/curto), bolinhas (black) ou mechas (dreads), no espaço do
// personagem; vai preso no osso da cabeça. Com skin pintada, usa o desenho do cabelo do TOPO da cabeça; sem, a cor p.hair.
void buildBlockHair(const CharacterParams& p, Geo& g, uint32_t seed);
// Boné separado (p.chapeu == 1 com skin pintada), na cor p.chapeuCor, no espaço do personagem (preso na cabeça)
void buildBlockCap(const CharacterParams& p, MeshBuilder& b);
// Colete de modelo 3D (colete.bin do tools/encaixar_colete.py, em fração da altura), na cor p.coleteCor. false = arquivo inválido
bool buildModelVest(const CharacterParams& p, Geo& g, const std::vector<uint8_t>& data);
// Cabelo de modelo 3D (arquivo .bin feito por tools/converter_cabelo.py): a touca arredondada na cor do modelo (sempre)
// e as peças do modelo (cachos, twists, mechas) na camada de detalhe. false = arquivo inválido.
bool buildModelHair(const CharacterParams& p, Geo& g, const std::vector<uint8_t>& data);  // a base fica sempre; cachos/mechas só de perto

// Armas: origem no ponto de empunhadura, cano para +Z, topo para +Y
enum WeaponKind { WK_Pistol, WK_Rifle, WK_Shotgun, WK_Sniper, WK_Knife, WK_Bat, WK_Count };
const char* weaponName(int kind);

// Encaixe da arma na mão direita (espaço do osso da mão): empunhadura descendo do pulso, cano seguindo os dedos
glm::mat4 weaponGripOffset(int kind, float height);

}  // namespace eng
