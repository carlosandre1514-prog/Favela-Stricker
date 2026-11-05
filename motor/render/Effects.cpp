#include "render/Effects.h"
#include <algorithm>
#include <cmath>

namespace eng {

const char* elementName(int e) {
    static const char* n[] = {"Fogo", "Gelo", "Trevas", "Luz", "Veneno", "Raio", "Arcano"};
    return e >= 0 && e < EL_Count ? n[e] : "Personalizado";
}

const char* fxName(int k) {
    static const char* n[] = {"Fogueira", "Tocha", "Fumaca", "Faiscas", "Aura magica", "Portal", "Explosao", "Orbe magico", "Coluna de luz", "Raios"};
    return k >= 0 && k < FX_Count ? n[k] : "Efeito";
}

FxPalette fxPalette(int e) {
    switch (e) {
        case EL_Ice:       return {{0.85f, 0.97f, 1.f}, {0.25f, 0.65f, 1.f}, {0.75f, 0.88f, 1.f}, false, PS_Shard, {0.4f, 0.7f, 1.f}};
        case EL_Dark:      return {{0.75f, 0.35f, 1.f}, {0.4f, 0.05f, 0.65f}, {0.04f, 0.f, 0.07f}, true, PS_Glow, {0.5f, 0.1f, 0.9f}};
        case EL_Light:     return {{1.f, 1.f, 0.92f}, {1.f, 0.8f, 0.3f}, {1.f, 0.95f, 0.75f}, false, PS_Star, {1.f, 0.9f, 0.6f}};
        case EL_Poison:    return {{0.75f, 1.f, 0.45f}, {0.25f, 0.85f, 0.1f}, {0.18f, 0.4f, 0.06f}, false, PS_Glow, {0.4f, 1.f, 0.2f}};
        case EL_Lightning: return {{0.92f, 0.96f, 1.f}, {0.45f, 0.6f, 1.f}, {0.5f, 0.55f, 0.7f}, false, PS_Spark, {0.6f, 0.75f, 1.f}};
        case EL_Arcane:    return {{1.f, 0.75f, 1.f}, {0.9f, 0.2f, 0.85f}, {0.35f, 0.08f, 0.4f}, false, PS_Star, {1.f, 0.4f, 0.9f}};
        default:           return {{1.f, 0.9f, 0.55f}, {1.f, 0.42f, 0.08f}, {0.22f, 0.2f, 0.19f}, false, PS_Spark, {1.f, 0.55f, 0.2f}};
    }
}

FxPalette fxPaletteFromColor(glm::vec3 c) {
    glm::vec3 core = glm::mix(c, glm::vec3(1.f), 0.6f);
    float lum = glm::dot(c, glm::vec3(0.3f, 0.59f, 0.11f));
    return {core, c, c * 0.3f, lum < 0.15f, PS_Glow, glm::max(c, glm::vec3(0.1f))};
}

namespace {
glm::vec3 randDir(Rng& r) {
    float z = r.range(-1.f, 1.f), a = r.range(0.f, 6.2831853f), s = std::sqrt(std::max(0.f, 1.f - z * z));
    return {s * std::cos(a), z, s * std::sin(a)};
}
glm::vec3 randDisk(Rng& r, float radius) {
    float a = r.range(0.f, 6.2831853f), d = std::sqrt(r.f()) * radius;
    return {std::cos(a) * d, 0.f, std::sin(a) * d};
}
int count(float& accum, float rate, float dt) {
    accum += rate * dt;
    int n = int(accum);
    accum -= float(n);
    return std::min(n, 64);
}

void smokePuff(ParticleSystem& ps, Rng& r, const FxPalette& pal, glm::vec3 pos, float s, glm::vec3 vel, float life, float opacity) {
    ParticleSpawn p;
    p.pos = pos; p.vel = vel;
    p.life = life * r.range(0.8f, 1.2f);
    p.size0 = 0.35f * s; p.size1 = 1.4f * s;
    p.color0 = glm::vec4(pal.smoke, opacity * (pal.darkSmoke ? 1.6f : 1.f));
    p.color1 = glm::vec4(pal.smoke * 0.8f, 0.f);
    p.rot = r.range(0.f, 6.28f); p.rotVel = r.range(-0.6f, 0.6f);
    p.drag = 0.6f; p.gravity = -0.25f;
    p.shape = PS_Smoke; p.additive = false;
    ps.emit(p);
}
}  // namespace

FxLight fxContinuous(ParticleSystem& ps, Rng& r, int kind, const FxPalette& pal, glm::vec3 pos, float s, float dt, float& accum,
                     float& timer) {
    timer += dt;
    float flicker = 0.85f + 0.3f * std::sin(timer * 23.f) * std::sin(timer * 7.3f);
    switch (kind) {
        case FX_Fire:
        case FX_Torch: {
            float k = kind == FX_Torch ? 0.35f : 1.f;
            float sc = s * k;
            for (int n = count(accum, 70.f * std::max(sc, 0.3f), dt); n > 0; --n) {
                ParticleSpawn p;
                p.pos = pos + randDisk(r, 0.28f * sc) + glm::vec3(0, 0.05f * sc, 0);
                p.vel = glm::vec3(r.range(-0.15f, 0.15f), r.range(1.1f, 2.f), r.range(-0.15f, 0.15f)) * std::sqrt(sc);
                p.life = r.range(0.45f, 0.85f);
                p.size0 = r.range(0.28f, 0.42f) * sc; p.size1 = 0.06f * sc;
                p.color0 = glm::vec4(glm::mix(pal.glow, pal.core, r.range(0.15f, 0.55f)), 1.f);
                p.color1 = glm::vec4(pal.glow * 0.7f, 0.f);
                p.shape = PS_Flame; p.drag = 0.5f; p.occlusion = 0.45f;
                ps.emit(p);
                if (r.chance(0.12f)) {  // brasas
                    ParticleSpawn b;
                    b.pos = p.pos; b.vel = glm::vec3(r.range(-0.6f, 0.6f), r.range(1.8f, 3.2f), r.range(-0.6f, 0.6f)) * std::sqrt(sc);
                    b.life = r.range(0.8f, 1.6f); b.size0 = b.size1 = 0.025f * std::max(sc, 0.5f);
                    b.color0 = glm::vec4(pal.core * 2.f, 1.f); b.color1 = glm::vec4(pal.glow, 0.f);
                    b.shape = pal.bitShape; b.stretch = 0.04f; b.drag = 0.8f;
                    ps.emit(b);
                }
                if (r.chance(0.12f))
                    smokePuff(ps, r, pal, pos + glm::vec3(0, 0.9f * sc, 0) + randDisk(r, 0.2f * sc), sc,
                              glm::vec3(r.range(-0.2f, 0.2f), r.range(0.6f, 1.f), r.range(-0.2f, 0.2f)), 2.2f, 0.28f);
            }
            return {pos + glm::vec3(0, 0.6f * sc, 0), pal.light, 7.f * sc * flicker, 7.f * std::max(sc, 0.4f)};
        }
        case FX_Smoke:
            for (int n = count(accum, 14.f * s, dt); n > 0; --n)
                smokePuff(ps, r, pal, pos + randDisk(r, 0.3f * s), s * 1.3f, glm::vec3(r.range(-0.2f, 0.2f), r.range(0.8f, 1.4f), r.range(-0.2f, 0.2f)), 3.5f, 0.45f);
            return {pos, glm::vec3(0.f), 0.f, 0.f};
        case FX_Sparks:
            for (int n = count(accum, 45.f * s, dt); n > 0; --n) {
                ParticleSpawn p;
                p.pos = pos;
                p.vel = glm::vec3(r.range(-1.2f, 1.2f), r.range(4.f, 7.f), r.range(-1.2f, 1.2f)) * std::sqrt(s);
                p.life = r.range(0.7f, 1.3f); p.size0 = p.size1 = 0.03f * s;
                p.color0 = glm::vec4(pal.core * 2.f, 1.f); p.color1 = glm::vec4(pal.glow, 0.f);
                p.shape = pal.bitShape == PS_Shard ? PS_Shard : PS_Spark; p.stretch = 0.035f; p.gravity = 9.8f;
                if (p.shape == PS_Shard) { p.stretch = 0.f; p.size0 = p.size1 = 0.06f * s; p.rotVel = r.range(-8.f, 8.f); }
                ps.emit(p);
            }
            return {pos + glm::vec3(0, 0.3f, 0), pal.light, 3.f * s * flicker, 4.f * s};
        case FX_Aura:
            for (int n = count(accum, 40.f * s, dt); n > 0; --n) {
                float a = r.range(0.f, 6.2831853f);
                float rad = 0.8f * s;
                ParticleSpawn p;
                p.pos = pos + glm::vec3(std::cos(a) * rad, r.range(0.f, 0.3f), std::sin(a) * rad);
                p.vel = glm::vec3(-std::sin(a), 0.f, std::cos(a)) * 0.6f * s + glm::vec3(0, r.range(0.6f, 1.4f) * s, 0);
                p.life = r.range(1.f, 1.8f); p.size0 = 0.09f * s; p.size1 = 0.02f * s;
                p.color0 = glm::vec4(glm::mix(pal.glow, pal.core, 0.3f), 1.f); p.color1 = glm::vec4(pal.glow, 0.f);
                p.shape = pal.bitShape == PS_Spark ? PS_Glow : pal.bitShape; p.rotVel = r.range(-3.f, 3.f); p.occlusion = 0.4f;
                ps.emit(p);
                if (pal.darkSmoke && r.chance(0.25f))
                    smokePuff(ps, r, pal, p.pos, 0.6f * s, glm::vec3(0, 0.5f, 0), 1.6f, 0.5f);
            }
            if (std::fmod(timer, 0.5f) < dt) {  // pulso de brilho no chão
                ParticleSpawn g;
                g.pos = pos + glm::vec3(0, 0.15f, 0); g.life = 0.6f; g.size0 = 0.6f * s; g.size1 = 1.3f * s;
                g.color0 = glm::vec4(pal.glow, 0.7f); g.color1 = glm::vec4(pal.glow, 0.f); g.shape = PS_Ring;
                ps.emit(g);
            }
            return {pos + glm::vec3(0, 0.8f * s, 0), pal.light, 4.f * s * flicker, 5.f * s};
        case FX_Portal: {
            const float R = 1.3f * s;
            glm::vec3 c = pos + glm::vec3(0, R + 0.2f, 0);
            for (int n = count(accum, 120.f * s, dt); n > 0; --n) {
                float a = r.range(0.f, 6.2831853f);
                glm::vec3 radial(std::cos(a), std::sin(a), 0.f), tang(-std::sin(a), std::cos(a), 0.f);
                ParticleSpawn p;
                p.pos = c + radial * R * r.range(0.92f, 1.05f);
                p.vel = tang * 2.2f * s - radial * 0.4f * s;
                p.life = r.range(0.5f, 0.9f); p.size0 = 0.12f * s; p.size1 = 0.03f * s;
                p.color0 = glm::vec4(glm::mix(pal.glow, pal.core, r.f() * 0.5f), 1.f); p.color1 = glm::vec4(pal.glow, 0.f);
                p.shape = PS_Glow; p.occlusion = 0.4f;
                ps.emit(p);
                if (r.chance(0.3f)) {  // redemoinho no centro
                    ParticleSpawn q;
                    float b = r.range(0.f, 6.28f), d = r.range(0.2f, 0.9f) * R;
                    q.pos = c + glm::vec3(std::cos(b) * d, std::sin(b) * d, 0.f);
                    q.vel = glm::vec3(-std::sin(b), std::cos(b), 0.f) * 1.4f * s - glm::vec3(std::cos(b), std::sin(b), 0.f) * d * 0.8f;
                    q.life = 0.8f; q.size0 = 0.35f * s; q.size1 = 0.05f * s;
                    q.color0 = pal.darkSmoke ? glm::vec4(pal.smoke, 0.8f) : glm::vec4(pal.glow * 0.5f, 0.6f);
                    q.color1 = glm::vec4(q.color0.r, q.color0.g, q.color0.b, 0.f);
                    q.shape = PS_Smoke; q.additive = !pal.darkSmoke;
                    ps.emit(q);
                }
            }
            return {c, pal.light, 6.f * s * flicker, 7.f * s};
        }
        case FX_Explosion: {
            FxLight l{pos, pal.light, 0.f, 0.f};
            if (timer > 2.6f || timer <= dt) { timer = dt; l = fxExplosion(ps, r, pal, pos + glm::vec3(0, 0.6f * s, 0), s); }
            return l;
        }
        case FX_Orb: {
            glm::vec3 c = pos + glm::vec3(0, 1.2f * s + 0.12f * std::sin(timer * 2.f), 0);
            for (int n = count(accum, 50.f * s, dt); n > 0; --n) {
                ParticleSpawn p;
                p.pos = c + randDir(r) * 0.08f * s; p.vel = randDir(r) * 0.15f;
                p.life = 0.25f; p.size0 = 0.45f * s; p.size1 = 0.3f * s;
                p.color0 = glm::vec4(glm::mix(pal.glow, pal.core, 0.35f), 0.9f); p.color1 = glm::vec4(pal.glow, 0.f);
                p.shape = PS_Glow; p.occlusion = 0.5f;
                ps.emit(p);
                if (r.chance(0.4f)) {  // partículas orbitando
                    float a = timer * 4.f + r.range(0.f, 6.28f);
                    ParticleSpawn q;
                    q.pos = c + glm::vec3(std::cos(a), r.range(-0.4f, 0.4f), std::sin(a)) * 0.55f * s;
                    q.vel = glm::vec3(-std::sin(a), 0.f, std::cos(a)) * 1.2f * s;
                    q.life = 0.7f; q.size0 = 0.06f * s; q.size1 = 0.01f;
                    q.color0 = glm::vec4(pal.core * 1.6f, 1.f); q.color1 = glm::vec4(pal.glow, 0.f);
                    q.shape = pal.bitShape; q.rotVel = 6.f;
                    ps.emit(q);
                }
            }
            return {c, pal.light, 5.f * s, 6.f * s};
        }
        default: {  // coluna de luz
            for (int n = count(accum, 70.f * s, dt); n > 0; --n) {
                ParticleSpawn p;
                p.pos = pos + randDisk(r, 0.35f * s);
                p.vel = glm::vec3(0, r.range(5.f, 9.f) * s, 0);
                p.life = r.range(0.6f, 1.1f); p.size0 = 0.12f * s; p.size1 = 0.04f * s;
                p.color0 = glm::vec4(glm::mix(pal.glow, pal.core, 0.4f), 0.9f); p.color1 = glm::vec4(pal.glow, 0.f);
                p.shape = PS_Spark; p.stretch = 0.12f; p.occlusion = 0.3f;
                ps.emit(p);
            }
            if (std::fmod(timer, 0.15f) < dt) {
                ParticleSpawn g;
                g.pos = pos + glm::vec3(0, 0.1f, 0); g.life = 0.4f; g.size0 = 0.5f * s; g.size1 = 0.9f * s;
                g.color0 = glm::vec4(pal.glow, 0.6f); g.color1 = glm::vec4(pal.glow, 0.f); g.shape = PS_Glow;
                ps.emit(g);
            }
            return {pos + glm::vec3(0, 1.f, 0), pal.light, 5.f * s * flicker, 6.f * s};
        }
    }
}

FxLight fxExplosion(ParticleSystem& ps, Rng& r, const FxPalette& pal, glm::vec3 pos, float s) {
    // bola de fogo
    for (int i = 0; i < 28; ++i) {
        ParticleSpawn p;
        glm::vec3 d = randDir(r);
        p.pos = pos + d * 0.2f * s; p.vel = d * r.range(2.5f, 6.f) * s;
        p.life = r.range(0.45f, 0.85f); p.size0 = r.range(0.6f, 0.9f) * s; p.size1 = r.range(1.4f, 2.2f) * s;
        p.color0 = glm::vec4(glm::mix(pal.glow, pal.core, r.range(0.2f, 0.6f)) * 1.15f, 1.f);
        p.color1 = glm::vec4(pal.glow * 0.5f, 0.f);
        p.shape = r.chance(0.5f) ? PS_Flame : PS_Glow; p.drag = 3.5f; p.rot = r.range(0.f, 6.28f); p.occlusion = 0.5f;
        ps.emit(p);
    }
    // fumaça
    for (int i = 0; i < 16; ++i)
        smokePuff(ps, r, pal, pos + randDir(r) * 0.5f * s, 1.6f * s, randDir(r) * r.range(1.f, 2.5f) * s + glm::vec3(0, 1.f, 0), 2.6f, 0.55f);
    // estilhaços / faíscas / cristais
    for (int i = 0; i < 36; ++i) {
        ParticleSpawn p;
        glm::vec3 d = randDir(r);
        d.y = std::abs(d.y) * 0.8f + 0.2f;
        p.pos = pos; p.vel = glm::normalize(d) * r.range(7.f, 15.f) * std::sqrt(s);
        p.life = r.range(0.6f, 1.3f); p.size0 = p.size1 = 0.04f * s;
        p.color0 = glm::vec4(pal.core * 2.f, 1.f); p.color1 = glm::vec4(pal.glow, 0.f);
        p.shape = pal.bitShape; p.stretch = pal.bitShape == PS_Spark ? 0.03f : 0.f; p.gravity = 9.8f; p.drag = 0.6f;
        if (pal.bitShape != PS_Spark) { p.size0 = p.size1 = 0.09f * s; p.rotVel = r.range(-10.f, 10.f); }
        ps.emit(p);
    }
    // onda de choque e clarão
    ParticleSpawn ring;
    ring.pos = pos; ring.life = 0.35f; ring.size0 = 0.4f * s; ring.size1 = 6.f * s;
    ring.color0 = glm::vec4(pal.core, 0.8f); ring.color1 = glm::vec4(pal.glow, 0.f); ring.shape = PS_Ring;
    ps.emit(ring);
    ParticleSpawn flash;
    flash.pos = pos; flash.life = 0.15f; flash.size0 = 3.f * s; flash.size1 = 4.f * s;
    flash.color0 = glm::vec4(pal.core * 1.5f, 1.f); flash.color1 = glm::vec4(pal.glow, 0.f); flash.shape = PS_Star;
    ps.emit(flash);
    return {pos, pal.light, 40.f * s, 14.f * s};
}

FxLight fxRaios(ParticleSystem& ps, Rng& r, const glm::mat4& world, glm::vec3 a, glm::vec3 b, float raio, glm::vec3 cor, float dt,
                float& accFaisca, float& accBrilho, float& accArco, glm::vec3 velArma) {
    auto mundo = [&](glm::vec3 p) { return glm::vec3(world * glm::vec4(p, 1.f)); };
    auto pontoNaArma = [&]() {
        glm::vec3 p = glm::mix(a, b, r.f());
        return p + glm::vec3(r.range(-1.f, 1.f), r.range(-1.f, 1.f), 0.f) * raio;
    };
    const glm::vec3 branco = glm::mix(cor, glm::vec3(1.f), 0.4f);
    // faíscas pequenas que pulam da arma
    for (int n = count(accFaisca, 70.f, dt); n > 0; --n) {
        ParticleSpawn p;
        p.pos = mundo(pontoNaArma());
        p.vel = randDir(r) * r.range(0.3f, 0.9f) + velArma;
        p.life = r.range(0.15f, 0.35f); p.size0 = 0.011f; p.size1 = 0.005f;
        p.color0 = glm::vec4(branco * 3.f, 1.f); p.color1 = glm::vec4(cor * 2.f, 0.f);
        p.shape = PS_Spark; p.stretch = 0.035f; p.drag = 1.2f;
        ps.emit(p);
    }
    // brilho azul em volta (névoa de luz)
    for (int n = count(accBrilho, 26.f, dt); n > 0; --n) {
        ParticleSpawn p;
        p.pos = mundo(pontoNaArma());
        p.vel = randDir(r) * 0.08f + velArma;
        p.life = r.range(0.2f, 0.4f); p.size0 = r.range(0.04f, 0.07f); p.size1 = 0.015f;
        p.color0 = glm::vec4(cor * 1.3f, 0.7f); p.color1 = glm::vec4(cor * 0.8f, 0.f);
        p.shape = PS_Glow;
        ps.emit(p);
    }
    // arcos: zigue-zague curto saindo da superfície (cada pedaço é uma faísca esticada, quase parada)
    for (int n = count(accArco, 14.f, dt); n > 0; --n) {
        glm::vec3 p0 = mundo(pontoNaArma());
        glm::vec3 dir = randDir(r);
        const int segs = 5 + int(r.f() * 4.f);
        const float vida = r.range(0.1f, 0.18f);
        for (int k = 0; k < segs; ++k) {
            glm::vec3 d = glm::normalize(dir + randDir(r) * 0.9f);
            float len = r.range(0.025f, 0.05f);
            glm::vec3 p1 = p0 + d * len;
            ParticleSpawn s;
            const float v = 0.02f, tam = 0.006f;
            s.pos = (p0 + p1) * 0.5f; s.vel = d * v;
            s.stretch = std::max(0.f, (len * 0.5f - tam) / v);
            s.life = vida; s.size0 = s.size1 = tam;
            s.color0 = glm::vec4(branco * 4.f, 1.f); s.color1 = glm::vec4(cor * 2.5f, 0.f);
            s.shape = PS_Spark;
            ps.emit(s);
            p0 = p1;
        }
    }
    float flick = 0.7f + 0.3f * std::sin(accArco * 40.f);
    return {mundo((a + b) * 0.5f), cor, 1.6f * flick, 2.5f};
}

FxLight fxAuraArma(ParticleSystem& ps, Rng& r, const glm::mat4& world, glm::vec3 a, glm::vec3 b, float raio, glm::vec3 cor, float dt,
                   float& accFumaca, float& accBrasa, float& accEstalo, float& accHalo, float& accNevoa, glm::vec3 velArma, bool eletrico, float tempo) {
    auto mundo = [&](glm::vec3 p) { return glm::vec3(world * glm::vec4(p, 1.f)); };
    // direções da arma no mundo: comprimento (para trás: a aura escorre para a coronha) e "para fora"
    const glm::vec3 atras = glm::normalize(glm::vec3(world * glm::vec4(a - b, 0.f)) + glm::vec3(1e-5f));
    auto pontoNaArma = [&](float f = 1.f) {
        glm::vec3 p = glm::mix(a, b, r.f());
        return p + glm::vec3(r.range(-1.f, 1.f), r.range(-1.f, 1.f), 0.f) * raio * f;
    };
    const glm::vec3 quente = glm::mix(cor, glm::vec3(1.f, 0.75f, 0.35f), 0.45f);  // brasa: mais amarelada
    const glm::vec3 nucleo = glm::mix(cor, glm::vec3(1.f), 0.45f);                 // centro: quase branco
    // núcleo: luz intensa colada na arma (o centro do efeito brilha forte; as camadas de fora são mais fracas)
    for (int n = count(accHalo, 110.f, dt); n > 0; --n) {
        ParticleSpawn p;
        p.pos = mundo(pontoNaArma(0.35f));
        p.vel = randDir(r) * 0.02f + velArma;
        p.life = r.range(0.15f, 0.26f); p.size0 = r.range(0.018f, 0.028f); p.size1 = 0.014f;
        p.color0 = glm::vec4(nucleo * 2.2f, 0.8f); p.color1 = glm::vec4(cor * 1.4f, 0.f);
        p.shape = PS_Glow; p.occlusion = 0.25f;
        ps.emit(p);
    }
    // um pedaço de raio entre dois pontos (traço fino bem claro + brilho largo em volta)
    auto traco = [&](glm::vec3 p0, glm::vec3 p1, float vida, float forca) {
        const glm::vec3 dd = p1 - p0;
        const float len = glm::length(dd);
        if (len < 1e-5f) return;
        const glm::vec3 d = dd / len;
        const float v = 0.02f;
        for (int camada = 0; camada < 2; ++camada) {
            ParticleSpawn s;
            const float tam = camada ? 0.011f : 0.0032f;
            s.pos = (p0 + p1) * 0.5f; s.vel = d * v;
            s.stretch = (len * 0.62f) / v;
            s.life = vida; s.size0 = s.size1 = tam;
            if (camada) { s.color0 = glm::vec4(cor * 1.4f * forca, 0.4f); s.color1 = glm::vec4(cor, 0.f); s.shape = PS_Glow; }
            else { s.color0 = glm::vec4(nucleo * 4.f * forca, 1.f); s.color1 = glm::vec4(cor * 2.5f, 0.f); s.shape = PS_Spark; }
            ps.emit(s);
        }
    };
    if (eletrico) {
        // raios em espiral girando em volta da arma: cada raio dá voltas ao redor do eixo (cano -> coronha) num pedaço
        // da arma; o ângulo de partida gira com o tempo (a espiral roda), o raio ondula e treme como eletricidade e
        // cada um vive um instante (pisca). Duas "fitas" opostas formam a espiral.
        const glm::vec3 eixo = b - a;
        const float ry = raio * 1.3f, rx = raio * 0.85f;  // elipse em volta da arma (ela é mais alta que larga)
        for (int n = count(accFumaca, 60.f, dt); n > 0; --n) {
            const int fita = r.chance(0.5f) ? 0 : 1;
            const float t0 = r.range(-0.25f, 0.95f), comp = r.range(0.18f, 0.35f);
            const float voltas = r.range(0.9f, 1.6f);
            const float ang0 = tempo * 4.5f + fita * 3.14159f + t0 * 9.f + r.range(-0.35f, 0.35f);
            const float vida = r.range(0.06f, 0.13f);
            const float fase = r.range(0.f, 6.28f);
            const int segs = 16 + int(r.f() * 8.f);
            glm::vec3 ant(0.f);
            for (int k = 0; k <= segs; ++k) {
                const float u = float(k) / float(segs);
                const float ang = ang0 + u * voltas * 6.2831853f;
                // ondulação (onda que corre) + tremida aleatória: o raio não é uma curva lisa
                const float onda = 1.f + 0.18f * std::sin(u * 17.f + fase + tempo * 25.f);
                glm::vec3 p = a + eixo * glm::clamp(t0 + u * comp, 0.f, 1.f);
                p += glm::vec3(std::cos(ang) * rx * onda, std::sin(ang) * ry * onda, 0.f);
                p += glm::vec3(r.range(-1.f, 1.f), r.range(-1.f, 1.f), r.range(-1.f, 1.f)) * 0.007f;
                const glm::vec3 w = mundo(p);
                if (k > 0) {
                    traco(ant, w, vida, 1.f);
                    // galhinho de vez em quando, saindo para fora da espiral
                    if (r.chance(0.07f)) {
                        glm::vec3 g0 = w, dir = glm::normalize(w - mundo(a + eixo * glm::clamp(t0 + u * comp, 0.f, 1.f)) + randDir(r) * 0.6f);
                        for (int j = 0; j < 3; ++j) {
                            glm::vec3 g1 = g0 + glm::normalize(dir + randDir(r) * 0.7f) * r.range(0.012f, 0.022f);
                            traco(g0, g1, vida * 0.8f, 0.8f);
                            g0 = g1;
                        }
                    }
                }
                ant = w;
            }
        }
    }
    // fumaça de energia: nuvens que crescem, sobem e escorrem para trás (saturada mesmo de dia: escurece o fundo)
    for (int n = eletrico ? 0 : count(accFumaca, 110.f, dt); n > 0; --n) {
        ParticleSpawn p;
        p.pos = mundo(pontoNaArma());
        glm::vec3 fora = randDir(r);
        p.vel = fora * r.range(0.04f, 0.14f) + atras * r.range(0.12f, 0.32f) + velArma;
        p.life = r.range(0.4f, 0.7f); p.size0 = r.range(0.018f, 0.03f); p.size1 = r.range(0.05f, 0.08f);
        p.color0 = glm::vec4(glm::mix(cor, nucleo, 0.35f) * 2.f, 0.7f); p.color1 = glm::vec4(cor * 0.3f, 0.f);
        p.shape = r.chance(0.6f) ? PS_Flame : PS_Glow; p.rot = r.range(0.f, 6.28f); p.rotVel = r.range(-2.5f, 2.5f);
        p.gravity = -0.25f; p.drag = 1.2f; p.occlusion = 0.45f;
        if (p.shape == PS_Flame) p.stretch = 0.12f;  // labaredas esticadas, escorrendo para trás
        ps.emit(p);
    }
    // névoa de fora: maior e mais fraca (a borda do efeito)
    for (int n = count(accNevoa, 45.f, dt); n > 0; --n) {
        ParticleSpawn p;
        p.pos = mundo(pontoNaArma(1.4f));
        p.vel = randDir(r) * 0.05f + velArma;
        p.life = r.range(0.3f, 0.5f); p.size0 = r.range(0.06f, 0.09f); p.size1 = 0.1f;
        p.color0 = glm::vec4(cor * 0.9f, 0.3f); p.color1 = glm::vec4(cor * 0.4f, 0.f);
        p.shape = PS_Glow; p.occlusion = 0.3f;
        ps.emit(p);
    }
    // brasas que sobem
    for (int n = count(accBrasa, 28.f, dt); n > 0; --n) {
        ParticleSpawn p;
        p.pos = mundo(pontoNaArma());
        p.vel = randDir(r) * r.range(0.2f, 0.55f) + atras * 0.15f + velArma;
        p.life = r.range(0.4f, 0.85f); p.size0 = 0.008f; p.size1 = 0.003f;
        p.color0 = glm::vec4((eletrico ? nucleo : quente) * 3.f, 1.f); p.color1 = glm::vec4(cor * 1.5f, 0.f);
        p.shape = PS_Spark; p.stretch = 0.03f; p.gravity = eletrico ? 0.f : -0.6f; p.drag = 1.f;
        ps.emit(p);
    }
    // estalos de energia (zigue-zague curto) de vez em quando
    for (int n = eletrico ? 0 : count(accEstalo, 5.f, dt); n > 0; --n) {
        glm::vec3 p0 = mundo(pontoNaArma());
        glm::vec3 dir = randDir(r);
        const int segs = 4 + int(r.f() * 3.f);
        const float vida = r.range(0.08f, 0.14f);
        for (int k = 0; k < segs; ++k) {
            glm::vec3 d = glm::normalize(dir + randDir(r) * 0.9f);
            float len = r.range(0.02f, 0.04f);
            glm::vec3 p1 = p0 + d * len;
            ParticleSpawn s;
            const float v = 0.02f, tam = 0.005f;
            s.pos = (p0 + p1) * 0.5f; s.vel = d * v;
            s.stretch = std::max(0.f, (len * 0.5f - tam) / v);
            s.life = vida; s.size0 = s.size1 = tam;
            s.color0 = glm::vec4(quente * 3.5f, 1.f); s.color1 = glm::vec4(cor * 2.f, 0.f);
            s.shape = PS_Spark;
            ps.emit(s);
            p0 = p1;
        }
    }
    return {mundo((a + b) * 0.5f), cor, 2.2f, 2.8f};
}

FxLight fxMuzzleFlash(ParticleSystem& ps, Rng& r, glm::vec3 pos, glm::vec3 dir) {
    ParticleSpawn f;
    f.pos = pos + dir * 0.05f; f.life = 0.05f; f.size0 = 0.22f; f.size1 = 0.12f;
    f.color0 = glm::vec4(1.f, 0.85f, 0.5f, 1.f) * 2.f; f.color1 = glm::vec4(1.f, 0.5f, 0.1f, 0.f);
    f.shape = PS_Star; f.rot = r.range(0.f, 6.28f);
    ps.emit(f);
    for (int i = 0; i < 6; ++i) {
        ParticleSpawn p;
        p.pos = pos; p.vel = glm::normalize(dir + randDir(r) * 0.3f) * r.range(6.f, 12.f);
        p.life = r.range(0.06f, 0.14f); p.size0 = p.size1 = 0.02f;
        p.color0 = glm::vec4(1.f, 0.85f, 0.4f, 1.f) * 2.f; p.color1 = glm::vec4(1.f, 0.4f, 0.1f, 0.f);
        p.shape = PS_Spark; p.stretch = 0.02f;
        ps.emit(p);
    }
    ParticleSpawn s;
    s.pos = pos + dir * 0.1f; s.vel = dir * 0.6f + glm::vec3(0, 0.3f, 0); s.life = 0.7f; s.size0 = 0.08f; s.size1 = 0.35f;
    s.color0 = glm::vec4(0.6f, 0.6f, 0.6f, 0.25f); s.color1 = glm::vec4(0.6f, 0.6f, 0.6f, 0.f); s.shape = PS_Smoke; s.additive = false; s.drag = 1.f;
    ps.emit(s);
    return {pos, {1.f, 0.7f, 0.35f}, 14.f, 6.f};
}

void fxBullet(ParticleSystem& ps, glm::vec3 from, glm::vec3 to, const glm::vec3* cor) {
    glm::vec3 d = to - from;
    float dist = glm::length(d);
    if (dist < 0.5f) return;
    const float speed = 160.f;
    ParticleSpawn b;
    b.pos = from; b.vel = d / dist * speed; b.life = std::min(dist / speed, 1.5f);
    b.size0 = b.size1 = 0.025f; b.stretch = 0.025f;
    b.color0 = glm::vec4(1.f, 0.9f, 0.6f, 1.f) * 2.f; b.color1 = glm::vec4(1.f, 0.8f, 0.4f, 0.6f);
    if (cor) {  // bala da skin: miolo quase branco puxado para a cor, rastro mais longo
        const glm::vec3 c = glm::mix(*cor, glm::vec3(1.f), 0.5f);
        b.size0 = b.size1 = 0.03f; b.stretch = 0.035f;
        b.color0 = glm::vec4(c * 2.6f, 1.f); b.color1 = glm::vec4(*cor * 1.8f, 0.7f);
    }
    b.shape = PS_Spark;
    ps.emit(b);
}

FxLight fxDisparoSkin(ParticleSystem& ps, Rng& r, glm::vec3 pos, glm::vec3 dir, glm::vec3 cor, glm::vec3 cor2) {
    // jato de chamas na boca do cano: miolo quente (cor2, quase branco) que vira a cor da skin e apaga; as labaredas
    // saem esticadas na direção do tiro, freiam rápido, abrem e sobem um pouco, como fogo de verdade
    const glm::vec3 quente = glm::mix(cor2, glm::vec3(1.f), 0.25f);
    ParticleSpawn g;  // clarão do miolo
    g.pos = pos + dir * 0.03f; g.life = 0.05f; g.size0 = 0.1f; g.size1 = 0.05f;
    g.color0 = glm::vec4(glm::mix(quente, cor, 0.3f) * 2.f, 1.f); g.color1 = glm::vec4(cor * 1.5f, 0.f); g.shape = PS_Glow;
    ps.emit(g);
    for (int i = 0; i < 22; ++i) {  // jato principal
        ParticleSpawn p;
        p.pos = pos + dir * r.range(0.f, 0.05f);
        p.vel = glm::normalize(dir + randDir(r) * 0.18f) * r.range(1.6f, 3.8f);
        p.life = r.range(0.1f, 0.2f); p.size0 = r.range(0.035f, 0.05f); p.size1 = r.range(0.1f, 0.15f);
        p.color0 = glm::vec4(glm::mix(quente, cor, r.range(0.6f, 0.95f)) * 1.5f, 0.9f); p.color1 = glm::vec4(cor * 0.6f, 0.f);
        p.shape = PS_Flame; p.stretch = 0.035f; p.drag = 9.f; p.gravity = -1.f; p.occlusion = 0.25f;
        ps.emit(p);
    }
    for (int i = 0; i < 12; ++i) {  // labaredas de fora: mais abertas, lentas e escuras
        ParticleSpawn p;
        p.pos = pos + dir * r.range(0.02f, 0.08f);
        p.vel = glm::normalize(dir + randDir(r) * 0.4f) * r.range(0.8f, 2.f);
        p.life = r.range(0.12f, 0.24f); p.size0 = 0.04f; p.size1 = r.range(0.11f, 0.15f);
        p.color0 = glm::vec4(cor * 1.4f, 0.8f); p.color1 = glm::vec4(cor * 0.5f, 0.f);
        p.shape = PS_Flame; p.stretch = 0.02f; p.drag = 6.f; p.gravity = -1.5f; p.rot = r.range(0.f, 6.28f); p.occlusion = 0.2f;
        ps.emit(p);
    }
    for (int i = 0; i < 8; ++i) {  // fagulhas azuis
        ParticleSpawn p;
        p.pos = pos;
        p.vel = glm::normalize(dir + randDir(r) * 0.5f) * r.range(2.f, 6.f);
        p.life = r.range(0.15f, 0.3f); p.size0 = p.size1 = 0.008f;
        p.color0 = glm::vec4(quente * 3.f, 1.f); p.color1 = glm::vec4(cor * 1.5f, 0.f);
        p.shape = PS_Spark; p.stretch = 0.02f; p.drag = 2.f; p.gravity = -0.5f;
        ps.emit(p);
    }
    return {pos + dir * 0.1f, cor, 18.f, 7.f};
}

void fxImpact(ParticleSystem& ps, Rng& r, glm::vec3 pos, glm::vec3 dir) {
    for (int i = 0; i < 9; ++i) {
        ParticleSpawn p;
        p.pos = pos; p.vel = glm::normalize(-dir + randDir(r) * 0.9f) * r.range(2.f, 6.f);
        p.life = r.range(0.2f, 0.5f); p.size0 = p.size1 = 0.02f;
        p.color0 = glm::vec4(1.f, 0.85f, 0.5f, 1.f) * 2.f; p.color1 = glm::vec4(1.f, 0.4f, 0.1f, 0.f);
        p.shape = PS_Spark; p.stretch = 0.03f; p.gravity = 9.8f;
        ps.emit(p);
    }
    for (int i = 0; i < 3; ++i) {
        ParticleSpawn s;
        s.pos = pos - dir * 0.05f; s.vel = (-dir + randDir(r) * 0.4f) * 0.8f; s.life = r.range(0.6f, 1.f);
        s.size0 = 0.08f; s.size1 = 0.45f;
        s.color0 = glm::vec4(0.55f, 0.52f, 0.48f, 0.4f); s.color1 = glm::vec4(0.55f, 0.52f, 0.48f, 0.f);
        s.shape = PS_Smoke; s.additive = false; s.drag = 2.f; s.rot = r.range(0.f, 6.28f);
        ps.emit(s);
    }
}

void fxSpellTrail(ParticleSystem& ps, Rng& r, const FxPalette& pal, glm::vec3 pos, glm::vec3 vel, float dt) {
    glm::vec3 dir = glm::length(vel) > 1e-3f ? glm::normalize(vel) : glm::vec3(0, 0, 1);
    // núcleo
    ParticleSpawn c;
    c.pos = pos; c.life = 0.08f; c.size0 = 0.4f; c.size1 = 0.3f;
    c.color0 = glm::vec4(glm::mix(pal.glow, pal.core, 0.5f) * 1.2f, 1.f); c.color1 = glm::vec4(pal.glow, 0.f);
    c.shape = PS_Glow; c.occlusion = 0.3f;
    ps.emit(c);
    int n = std::max(1, int(140.f * dt));
    for (int i = 0; i < n; ++i) {
        ParticleSpawn p;
        p.pos = pos - vel * dt * r.f() + randDir(r) * 0.08f;
        p.vel = randDir(r) * 0.6f - dir * 1.5f;
        p.life = r.range(0.25f, 0.5f); p.size0 = r.range(0.15f, 0.25f); p.size1 = 0.03f;
        p.color0 = glm::vec4(glm::mix(pal.glow, pal.core, r.f() * 0.4f), 1.f); p.color1 = glm::vec4(pal.glow, 0.f);
        p.shape = pal.bitShape == PS_Spark ? PS_Flame : pal.bitShape; p.rot = r.range(0.f, 6.28f); p.rotVel = r.range(-6.f, 6.f);
        p.occlusion = 0.45f;
        ps.emit(p);
        if (pal.darkSmoke || r.chance(0.2f))
            smokePuff(ps, r, pal, p.pos, 0.5f, p.vel * 0.3f, 1.f, pal.darkSmoke ? 0.6f : 0.25f);
    }
}

}  // namespace eng
