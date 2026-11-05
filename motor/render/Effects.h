#pragma once
#include <glm/glm.hpp>
#include "procgen/Noise.h"
#include "render/Particles.h"

namespace eng {

struct GameObject;

// Elementos (cores e comportamento das magias/efeitos)
enum Element { EL_Fire, EL_Ice, EL_Dark, EL_Light, EL_Poison, EL_Lightning, EL_Arcane, EL_Count };
const char* elementName(int e);

struct FxPalette {
    glm::vec3 core;      // centro (quase branco)
    glm::vec3 glow;      // cor principal (aditiva)
    glm::vec3 smoke;     // fumaça
    bool darkSmoke;      // fumaça opaca escura (trevas) em vez de translúcida
    uint8_t bitShape;    // formato das partículas pequenas (faísca, cristal, estrela...)
    glm::vec3 light;     // cor da luz dinâmica
};
FxPalette fxPalette(int element);
FxPalette fxPaletteFromColor(glm::vec3 color);  // cor livre escolhida pelo usuário

// Efeitos contínuos que podem virar objeto na cena
enum FxKind { FX_Fire, FX_Torch, FX_Smoke, FX_Sparks, FX_Aura, FX_Portal, FX_Explosion, FX_Orb, FX_Pillar, FX_Raios, FX_Count };
const char* fxName(int k);

// Componente: emissor preso a um GameObject
struct Emitter {
    int kind = FX_Fire;
    int element = EL_Fire;
    bool customColor = false;
    glm::vec3 color{1.f};
    float scale = 1.f;
    float accum = 0.f, timer = 0.f;
    float accum2 = 0.f, accum3 = 0.f, accum4 = 0.f, accum5 = 0.f;
    // FX_Raios (skin de arma): raios e faíscas ao longo do segmento a -> b (espaço do objeto), a até "raio" dele
    glm::vec3 a{0.f}, b{0.f};
    float raio = 0.03f;
    int estilo = 0;          // FX_Raios: 0 faíscas, 1 aura de fumaça brilhante com brasas, 2 aura de raios (arma toda)
    // tiro personalizado da skin ("disparo" no _efeito.json): sopro de dragão na boca do cano, bala colorida e a
    // pintura acendendo forte a cada tiro (pulso)
    bool disparo = false;
    glm::vec3 corDisparo{0.2f, 0.55f, 1.f}, corDisparo2{1.f, 0.75f, 0.3f};
    float pulso = 0.f, emiBase = 1.f;
    glm::vec4 rimBase{0.f};
    glm::vec3 ultimo{0.f};   // posição no frame anterior (as partículas acompanham a arma quando ela anda)
    bool temUltimo = false;
    GameObject* owner = nullptr;
    FxPalette palette() const { return customColor ? fxPaletteFromColor(color) : fxPalette(element); }
};

// Luz dinâmica pedida por um efeito neste frame
struct FxLight { glm::vec3 pos, color; float intensity, radius; };

// Emite as partículas do efeito contínuo durante dt. Retorna a luz do efeito (intensity 0 = sem luz).
FxLight fxContinuous(ParticleSystem& ps, Rng& r, int kind, const FxPalette& pal, glm::vec3 pos, float scale, float dt,
                     float& accum, float& timer);

// Efeitos instantâneos
FxLight fxExplosion(ParticleSystem& ps, Rng& r, const FxPalette& pal, glm::vec3 pos, float scale);
// Raios elétricos saindo de um objeto (skin de arma): faíscas, brilhos e arcos em zigue-zague ao longo de a -> b
FxLight fxRaios(ParticleSystem& ps, Rng& r, const glm::mat4& world, glm::vec3 a, glm::vec3 b, float raio, glm::vec3 cor, float dt,
                float& accFaisca, float& accBrilho, float& accArco, glm::vec3 velArma = glm::vec3(0.f));
// Aura de energia envolvendo um objeto (skin de arma): fumaça brilhante subindo, brasas e estalos ao longo de a -> b
FxLight fxAuraArma(ParticleSystem& ps, Rng& r, const glm::mat4& world, glm::vec3 a, glm::vec3 b, float raio, glm::vec3 cor, float dt,
                   float& accFumaca, float& accBrasa, float& accEstalo, float& accHalo, float& accNevoa, glm::vec3 velArma = glm::vec3(0.f),
                   bool eletrico = false, float tempo = 0.f);  // eletrico: raios em zigue-zague no lugar das labaredas
FxLight fxMuzzleFlash(ParticleSystem& ps, Rng& r, glm::vec3 pos, glm::vec3 dir);
void fxBullet(ParticleSystem& ps, glm::vec3 from, glm::vec3 to, const glm::vec3* cor = nullptr);  // cor: bala colorida (skin)
// Tiro de skin com efeito próprio: sopro de fogo azul (cor) com faíscas douradas (cor2), anel de choque e clarão
FxLight fxDisparoSkin(ParticleSystem& ps, Rng& r, glm::vec3 pos, glm::vec3 dir, glm::vec3 cor, glm::vec3 cor2);
void fxImpact(ParticleSystem& ps, Rng& r, glm::vec3 pos, glm::vec3 dir);
void fxSpellTrail(ParticleSystem& ps, Rng& r, const FxPalette& pal, glm::vec3 pos, glm::vec3 vel, float dt);

}  // namespace eng
