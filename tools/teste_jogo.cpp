// Testes do Favela Stricker no PC/CI: contexto GLES 3.2 sem janela (EGL surfaceless, Mesa), o motor com o jogo
// ligado nele (jogo_criar), desenha num FBO e salva PNGs. Uso:
//   favela_testes <pasta_shaders> <pasta_saida>
// Sem variável: partida, online e escada. Ou um só: SO_JOGO, SO_ONLINE, SO_ESCADA_EXTERNA, SO_MEMORIA=<projeto.json>,
// SO_RETRATOS_BLOCO (fotos das roupas), SO_PREVIAS="arma:skin,..." (fotos das armas)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <functional>
#include <glm/glm.hpp>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <sys/stat.h>
#include "json.hpp"
#include "platform/engine_c_api.h"
#include "jogo/jogo_api.h"
#include "engine/Engine.h"
#include "jogo/Partida.h"
#include "jogo/MapaFavela.h"

static const int W = std::getenv("LARGURA") ? std::atoi(std::getenv("LARGURA")) : 960, H = std::getenv("ALTURA") ? std::atoi(std::getenv("ALTURA")) : 540;  // (fotos maiores: LARGURA=1920 ALTURA=1080)
static GLuint g_fbo = 0;
static int g_fail = 0;

static void check(const char* what) {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) { std::printf("  [ERRO] glGetError 0x%x em %s\n", e, what); ++g_fail; }
}

static void frames(Engine* e, int n, float dt = 1.f / 60.f) {
    for (int i = 0; i < n; ++i) engine_frame(e, dt);
    glFinish();
    check("frame");
}

static void save(const std::string& dir, const char* name) {
    std::vector<unsigned char> px(W * H * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    stbi_flip_vertically_on_write(1);
    std::string path = dir + "/" + name;
    stbi_write_png(path.c_str(), W, H, 4, px.data(), W * 4);
    std::printf("  salvo %s\n", path.c_str());
}

static void stats(Engine* e, const char* label) {
    float s[6];
    engine_get_stats(e, s);
    std::printf("  %-22s draws=%3.0f tris=%7.0f objetos=%4.0f cpu=%.2fms\n", label, s[3], s[4], s[5], s[2]);
}


int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);  // mensagens na hora (testes longos)
    if (argc < 3) { std::printf("uso: %s <shaders> <saida>\n", argv[0]); return 2; }
    std::string out = argv[2];

    auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = getPlatformDisplay ? getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                                        : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, nullptr, nullptr)) { std::printf("eglInitialize falhou\n"); return 1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    EGLConfig cfg; EGLint ncfg = 0;
    eglChooseConfig(dpy, cfgAttr, &cfg, 1, &ncfg);
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, ncfg ? cfg : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctxAttr);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { std::printf("contexto GLES 3.2 falhou\n"); return 1; }
    std::printf("GL: %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    // FBO no lugar da tela
    GLuint color, depth;
    glGenRenderbuffers(1, &color); glBindRenderbuffer(GL_RENDERBUFFER, color); glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
    glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, W, H);
    glGenFramebuffers(1, &g_fbo); glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);

    auto tCreate = std::chrono::steady_clock::now();
    Engine* e = jogo_criar(argv[1]);  // motor + o jogo ligado nele
    std::printf("motor iniciado em %.2f s\n", std::chrono::duration<float>(std::chrono::steady_clock::now() - tCreate).count());
    if (!e) { std::printf("engine_create falhou (shaders?)\n"); return 1; }
    engine_set_target_fbo(e, g_fbo);
    engine_resize(e, W, H);
    check("init");

    // O projeto da partida (o mesmo do app e do site); as barracas de roupa ficam de fora (roupa é no mercado)
    const char* projPath = std::getenv("PROJETO") ? std::getenv("PROJETO") : "app/src/main/assets/exemplos/5_policia_x_bandidos.json";
    auto lerProjeto = [&](const char* path) {
        std::string txt;
        if (FILE* f = std::fopen(path, "rb")) { char b[65536]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) txt.append(b, n); std::fclose(f); }
        auto j = nlohmann::json::parse(txt.empty() ? "{}" : txt);
        nlohmann::json keep = nlohmann::json::array();
        for (auto& o : j["objetos"]) if (!(o.value("tipo", "") == "loja" && o.value("loja", "") == "roupas")) keep.push_back(o);
        j["objetos"] = keep;
        return j;
    };
    auto abrirPartida = [&]() {
        if (!engine_import_project(e, lerProjeto(projPath).dump().c_str())) { std::printf("  [ERRO] o projeto da partida não abriu: %s\n", engine_last_error(e)); ++g_fail; }
    };

    // Battle Royale Ressurgência (SO_BR=1): 10 times de 4, renascer perto do time, time eliminado, zona fechando e
    // fim da partida (o jogador fica de pé o tempo todo para ver até o último time). PERFIL=n: tempo por quadro.
    if (std::getenv("SO_BR")) {
        eng::Engine* E = reinterpret_cast<eng::Engine*>(e);
        abrirPartida();
        jogo_modo(e, 1);
        jogo_escolhas(e, 1, 2, "ak47", "tt33");
        engine_set_first_person(e, 1);
        engine_set_play(e, 1);
        eng::TeamMatch& M = eng::matchOf(E);
        // o jogador não cai durante o teste (o time dele fica no jogo até o fim): vida cheia a cada quadro
        auto quadros = [&](int n, float dt) { for (int q = 0; q < n; ++q) { E->game.playerHp = E->game.playerMax; engine_frame(e, dt); } glFinish(); };
        int times[10] = {0};
        for (const eng::Fighter& f : M.f) if (f.team >= 0 && f.team < 10) ++times[f.team];
        int certos = 0;
        for (int t = 0; t < 10; ++t) certos += times[t] == 4;
        const int pf = eng::playerFighter(E);
        std::printf("  lutadores: %zu · times com 4: %d · jogador no time %d (roupa de %s) · nós de caminho: %zu\n", M.f.size(), certos,
                    pf >= 0 ? M.f[pf].team + 1 : -1, pf >= 0 && M.f[pf].lado ? "bandido" : "polícia", M.nav.size());
        if (M.f.size() != 40 || certos != 10 || pf < 0) { std::printf("  [ERRO] a partida não tem 10 times de 4\n"); ++g_fail; }
        if (std::getenv("BR_LOG")) {  // os primeiros segundos: quem cai e por quê
            size_t vistos = 0;
            for (int k = 0; k < 30 * 20; ++k) {
                frames(e, 1, 1.f / 30.f);
                if (M.feed.size() != vistos || (k % 30 == 0)) {
                    int mortos = 0;
                    for (const eng::Fighter& f : M.f) mortos += f.dead;
                    if (k % 30 == 0) std::printf("  %4.1f s: no chão %d\n", k / 30.f, mortos);
                    for (size_t q = 0; q < M.feed.size(); ++q)
                        if (M.feed[q].age < 0.05f) std::printf("     abate: %s -> %s (%s)\n", M.feed[q].killer.c_str(), M.feed[q].victim.c_str(), M.feed[q].weapon.c_str());
                    vistos = M.feed.size();
                }
            }
            engine_destroy(e);
            return 0;
        }
        frames(e, 4);
        save(out, "br_caindo.png");
        quadros(90, 1.f / 30.f);
        save(out, "br_inicio.png");
        {  // todos pousaram em cima do chão (ninguém atravessou o mapa caindo do céu)
            int abaixo = 0;
            for (const eng::Fighter& f : M.f)
                if (eng::GameObject* g = E->scene.get(f.root)) {
                    const glm::vec3 p = g->transform.position;
                    if (p.y < eng::mapHeight(E, p.x, p.z) - 1.5f) { ++abaixo; std::printf("     abaixo do chão: %s em (%.1f, %.1f, %.1f), chão %.1f\n", f.name.c_str(), p.x, p.y, p.z, eng::mapHeight(E, p.x, p.z)); }
                }
            std::printf("  pouso: %d de %zu abaixo do chão\n", abaixo, M.f.size());
            if (abaixo) { std::printf("  [ERRO] alguém atravessou o chão caindo\n"); ++g_fail; }
        }
        if (std::getenv("PERFIL")) {
            double w = 0, sb = 0, rd = 0;
            const int nq = std::max(1, std::atoi(std::getenv("PERFIL")));
            for (int k = 0; k < nq; ++k) {
                engine_player_input(e, std::sin(k * 0.02f), 1.f, 1);
                frames(e, 1, 1.f / 30.f);
                float pr[3]; engine_get_profile(e, pr);
                w += pr[0]; sb += pr[1]; rd += pr[2];
            }
            engine_player_input(e, 0.f, 0.f, 0);
            std::printf("  CPU por quadro (40 lutadores): mundo %.2f ms · preparar desenho %.2f ms · desenhar %.2f ms\n", w / nq, sb / nq, rd / nq);
        }
        auto matar = [&](int i) { if (eng::GameObject* g = E->scene.get(M.f[i].root)) E->module->onDamage(E, g, 500.f); };
        // renascer: um robô do time 4 cai com os companheiros de pé -> volta em 10 s
        int alvo = -1;
        for (size_t i = 0; i < M.f.size(); ++i) if (M.f[i].team == 3 && !M.f[i].dead) { alvo = int(i); break; }
        if (alvo >= 0) {
            matar(alvo);
            const bool caiu = M.f[alvo].dead;
            quadros(30 * 9, 1.f / 30.f);
            const bool aindaNoChao = M.f[alvo].dead;
            quadros(30 * 2, 1.f / 30.f);
            std::printf("  renascer: caiu=%d, aos 9 s no chão=%d, aos 11 s de pé=%d, fora=%d\n", caiu, aindaNoChao, !M.f[alvo].dead, M.f[alvo].fora);
            if (!caiu || !aindaNoChao || M.f[alvo].dead || M.f[alvo].fora) { std::printf("  [ERRO] não renasceu em 10 s\n"); ++g_fail; }
        }
        // time inteiro no chão ao mesmo tempo: eliminado
        for (size_t i = 0; i < M.f.size(); ++i) if (M.f[i].team == 6) matar(int(i));
        quadros(2, 1.f / 30.f);
        std::printf("  time 7 todo no chão: colocação %d (0 = vivo)\n", M.colocacao[6]);
        if (!M.colocacao[6]) { std::printf("  [ERRO] o time eliminado continua no jogo\n"); ++g_fail; }
        // a zona fechando até o fim (o jogador não cai: vê até o último time)
        float t = 0.f, raio0 = M.zona.r;
        int ultimaFase = -1;
        while (E->game.state == 0 && t < 700.f) {
            E->game.playerHp = E->game.playerMax;
            engine_frame(e, 0.1f);
            t += 0.1f;
            if (M.zona.fase != ultimaFase) {
                ultimaFase = M.zona.fase;
                int tv = 0, jv = 0;
                for (int k = 0; k < 10; ++k) tv += M.colocacao[k] == 0;
                for (const eng::Fighter& f : M.f) jv += !f.fora;
                std::printf("  %4.0f s: fase %d da zona, raio %5.1f m, times vivos %d, jogadores %d\n", t, M.zona.fase + 1, M.zona.r, tv, jv);
                if (M.zona.fase == 2) save(out, "br_fase3.png");
                if (M.zona.fase == 1 && std::getenv("PAREDE")) {  // foto da parede da zona: perto da borda, olhando para fora
                    const int pj = eng::playerFighter(E);
                    if (eng::GameObject* g = E->scene.get(M.f[pj].root)) {
                        const glm::vec2 dir = glm::normalize(glm::vec2(1.f, 0.4f));
                        const glm::vec2 q = M.zona.c + dir * (M.zona.r - 9.f);
                        g->transform.setPosition(glm::vec3(q.x, eng::mapHeight(E, q.x, q.y) + 12.f, q.y));
                        if (g->rigidBody) E->physics.syncFromTransform(*g->rigidBody);
                        E->editorCam.yaw = std::atan2(-dir.x, -dir.y);
                        E->editorCam.pitch = 0.05f;
                        for (int k = 0; k < 60; ++k) { E->game.playerHp = E->game.playerMax; engine_frame(e, 1.f / 30.f); }
                        glFinish();
                        save(out, "br_parede.png");
                    }
                }
            }
        }
        glFinish();
        save(out, "br_fim.png");
        std::printf("  fim aos %.0f s: estado %d (%s) · raio da zona %.1f m (começou com %.0f)\n", t, E->game.state, E->game.message.c_str(), M.zona.r, raio0);
        if (E->game.state != 1) { std::printf("  [ERRO] a partida não terminou com o último time de pé\n"); ++g_fail; }
        std::string hud = jogo_hud(e);
        if (hud.find("\"br\"") == std::string::npos) { std::printf("  [ERRO] HUD sem os dados da Ressurgência\n"); ++g_fail; }
        engine_set_play(e, 0);
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Arma do jogador com skin na partida (SO_SKIN="ak47:dragao_azul", TIME=0/1): foto em primeira pessoa
    if (const char* sk = std::getenv("SO_SKIN")) {
        std::string a = sk, arma = a.substr(0, a.find(':')), skin = a.find(':') == std::string::npos ? "" : a.substr(a.find(':') + 1);
        const int time = std::getenv("TIME") ? std::atoi(std::getenv("TIME")) : 1;
        abrirPartida();
        jogo_skin_arma(e, arma.c_str(), skin.c_str());
        jogo_escolhas(e, time, 0, arma.c_str(), "tt33");
        engine_set_first_person(e, 1);
        engine_set_play(e, 1);
        frames(e, 60);
        std::printf("  hora no começo da partida: %.2f h\n", engine_get_time_of_day(e));
        save(out, ("skin_fp_" + arma + ".png").c_str());
        if (std::getenv("PERFIL")) {  // tempo de CPU por quadro na partida (média de 300 quadros, correndo)
            double w = 0, sb = 0, rd = 0;
            const int nq = std::max(1, std::atoi(std::getenv("PERFIL")));
            for (int k = 0; k < nq; ++k) {
                engine_player_input(e, std::sin(k * 0.02f), 1.f, 1);
                frames(e, 1, 1.f / 30.f);
                float pr[3]; engine_get_profile(e, pr);
                w += pr[0]; sb += pr[1]; rd += pr[2];
            }
            std::printf("  CPU por quadro: mundo %.2f ms · preparar desenho %.2f ms · desenhar %.2f ms\n", w / nq, sb / nq, rd / nq);
        }
        if (std::getenv("HORAS")) {  // o dia passando: um quadro de 0,5 s por vez
            for (int min = 1; min <= (std::getenv("HORAS") ? std::atoi(std::getenv("HORAS")) : 1); ++min) {
                for (int k = 0; k < 120; ++k) engine_frame(e, 0.5f);
                std::printf("  %2d min de partida: %.1f h\n", min, engine_get_time_of_day(e));
            }
        }
        if (std::getenv("CORRER")) {  // correndo e atirando: o clarão tem que ficar na boca do cano
            for (int k = 0; k < 40; ++k) { engine_player_input(e, 0.f, 1.f, 1); frames(e, 1); }
            for (int k = 0; k < 3; ++k) { engine_player_input(e, 0.f, 1.f, 1); engine_player_fire(e); frames(e, 1); }
            save(out, ("correndo_" + arma + ".png").c_str());
        }
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }

    auto steer = [&](int ch, int maxFrames, const std::function<glm::vec2(glm::vec3)>& dirOf, int run = 0) {
        float p[3], r[3], sc[3];
        for (int k = 0; k < maxFrames; ++k) {
            engine_get_transform(e, ch, p, r, sc);
            glm::vec2 d = dirOf(glm::vec3(p[0], p[1], p[2]));
            if (std::getenv("DEPURAR") && k % 40 == 0) std::printf("    %3d: %.2f %.2f %.2f -> %.2f %.2f\n", k, p[0], p[1], p[2], d.x, d.y);
            if (glm::length(d) > 1.f) d = glm::normalize(d);
            engine_set_camera(e, p[0], p[1] + 1.f, p[2], 0.f, 0.3f, 4.f);
            engine_player_input(e, d.x, -d.y, run);
            frames(e, 1);
        }
        engine_player_input(e, 0.f, 0.f, 0);
        engine_get_transform(e, ch, p, r, sc);
        return glm::vec3(p[0], p[1], p[2]);
    };
    auto placeChar = [&](int ch, glm::vec3 at) {
        float p[3], r[3], sc[3];
        engine_get_transform(e, ch, p, r, sc);
        p[0] = at.x; p[1] = at.y; p[2] = at.z;
        engine_set_transform(e, ch, p, r, sc);
    };

    auto jogo = [&]() {
        using nlohmann::json;
        std::printf("[33] jogo em primeira pessoa\n");
        abrirPartida();
        engine_set_quality(e, 1);
        jogo_skin_arma(e, "tt33", "gold_relic");
        jogo_escolhas(e, 0, 2, "m4colt", "tt33");
        engine_set_first_person(e, 1);
        engine_set_play(e, 1);
        engine_set_camera(e, 0.f, 0.f, 0.f, 0.f, 0.05f, 3.f);
        frames(e, 30);
        auto hud = [&]() { return json::parse(std::string(jogo_hud(e))); };
        json h = hud();
        std::printf("  HUD: %s\n", h.dump().substr(0, 400).c_str());
        if (!h.value("ativo", false) || h["armas"][0]["arma"] != "m4colt") { std::printf("  [ERRO] arma escolhida não entrou\n"); ++g_fail; }
        save(out, "33_quadril.png");
        engine_player_aim(e, 1);
        frames(e, 30);
        save(out, "33_mira.png");
        engine_set_camera(e, 0.f, 0.f, 0.f, 0.f, -0.55f, 3.f);
        frames(e, 10);
        save(out, "33_mira_cima.png");
        engine_set_camera(e, 0.f, 0.f, 0.f, 0.f, 0.6f, 3.f);
        frames(e, 10);
        save(out, "33_mira_baixo.png");
        engine_player_aim(e, 0);
        engine_set_camera(e, 0.f, 0.f, 0.f, 0.f, 0.05f, 3.f);
        for (int k = 0; k < 40; ++k) { engine_player_fire(e); frames(e, 1); if (k == 6) save(out, "33_tiro_quadril.png"); }
        h = hud();
        int balas = h["armas"][0]["balas"];
        std::printf("  depois de atirar: %d balas, reserva %d\n", balas, int(h["armas"][0]["reserva"]));
        if (balas >= 30) { std::printf("  [ERRO] as balas não gastaram\n"); ++g_fail; }
        engine_player_reload(e);
        frames(e, 2);
        if (!hud().value("recarregando", false)) { std::printf("  [ERRO] não recarregou\n"); ++g_fail; }
        frames(e, 150);
        jogo_trocar_arma(e);
        frames(e, 20);
        h = hud();
        if (h.value("slot", 0) != 1) { std::printf("  [ERRO] troca rápida não trocou\n"); ++g_fail; }
        save(out, "33_pistola.png");
        engine_player_aim(e, 1);
        frames(e, 25);
        save(out, "33_pistola_mira.png");
        engine_player_aim(e, 0);
        jogo_trocar_arma(e);
        frames(e, 10);
        engine_player_input(e, 0.f, 1.f, 1);
        frames(e, 40);
        save(out, "33_correndo.png");
        engine_player_input(e, 0.f, 0.f, 0);
        engine_player_crouch(e, 1);
        frames(e, 30);
        save(out, "33_agachado.png");
        engine_player_crouch(e, 0);
        engine_set_camera(e, 0.f, 0.f, 0.f, 0.f, -0.15f, 3.f);
        frames(e, 5);
        if (!jogo_granada(e)) { std::printf("  [ERRO] granada não saiu\n"); ++g_fail; }
        frames(e, 25);
        save(out, "33_granada.png");
        frames(e, 170);
        save(out, "33_granada_explodiu.png");
        h = hud();
        std::printf("  granadas: %d, placar %s\n", int(h["granadas"]), h["placar"].dump().c_str());
        if (h["granadas"] != 1) { std::printf("  [ERRO] contador de granadas\n"); ++g_fail; }
        for (int k = 0; k < 30 * 40; ++k) frames(e, 1, 1.f / 30.f);
        h = hud();
        std::printf("  após 40 s: placar %s, você %d abates %d mortes\n", h["placar"].dump().c_str(), int(h["abates"]), int(h["mortes"]));
        save(out, "33_depois.png");
        engine_set_play(e, 0);
        engine_set_first_person(e, 0);
    };
    // Partida online simulada: eu (quem criou a sala), um aliado e um adversário "de outro celular"
    auto online = [&]() {
        using nlohmann::json;
        std::printf("[34] online (rede simulada)\n");
        abrirPartida();
        json roster = json::array({{{"id", "eu"}, {"nome", "Eu"}, {"time", 0}, {"skin", 1}, {"arma", "m4a1"}},
                                   {{"id", "amigo"}, {"nome", "Amigo"}, {"time", 1}, {"skin", 2}, {"arma", "ak74"}, {"arma2", "tt33"},
                                    {"skins", {{"ak74", "favela"}, {"tt33", "gold_relic"}}}},
                                   {{"id", "aliado"}, {"nome", "Aliado"}, {"time", 0}, {"skin", 3}, {"arma", "m4colt"}}});
        jogo_rede_sala(e, 1, "eu", roster.dump().c_str());
        jogo_escolhas(e, 0, 1, "m4a1", "tt33");
        engine_set_first_person(e, 1);
        engine_set_play(e, 1);
        engine_set_camera(e, 0.f, 0.f, 0.f, 0.f, 0.f, 3.f);
        frames(e, 20);
        auto hud = [&]() { return json::parse(std::string(jogo_hud(e))); };
        json h = hud();
        int n = 0; bool amigo = false;
        for (auto& p : h["jogadores"]) { ++n; if (p["nome"] == "Amigo") amigo = true; }
        std::printf("  lutadores: %d (Amigo na partida: %s)\n", n, amigo ? "sim" : "não");
        if (n != 3 || !amigo) { std::printf("  [ERRO] escalação online (só gente, sem robôs)\n"); ++g_fail; }
        json mine = json::parse(std::string(jogo_rede_coletar(e)));
        std::printf("  meus lutadores: %zu\n", mine.size());
        if (mine.size() != 1 || !mine.contains("eu")) { std::printf("  [ERRO] estado enviado\n"); ++g_fail; }
        // o amigo aparece 9 m na minha frente
        float p[3], r[3], sc[3];
        engine_get_transform(e, engine_player_id(e), p, r, sc);
        json st = {{"amigo", {{"p", {p[0], p[1], p[2] - 9.f}}, {"y", 0.0}, {"pi", 0.0}, {"c", false}, {"a", true}, {"w", "ak74"}, {"v", true}, {"n", 0}}}};
        jogo_rede_aplicar(e, st.dump().c_str());
        frames(e, 20);
        st["amigo"]["n"] = 3;  // ele atirou 3 vezes
        jogo_rede_aplicar(e, st.dump().c_str());
        frames(e, 3);
        save(out, "34_amigo.png");
        jogo_rede_eventos(e);  // limpa
        engine_player_aim(e, 1);
        frames(e, 25);
        for (int k = 0; k < 20; ++k) { engine_player_fire(e); frames(e, 1); }
        json ev = json::parse(std::string(jogo_rede_eventos(e)));
        int danos = 0;
        for (auto& x : ev) if (x["t"] == "dano" && x["alvo"] == "amigo") ++danos;
        std::printf("  tiros que acertaram o amigo (eventos de dano): %d\n", danos);
        if (danos == 0) { std::printf("  [ERRO] tiro no jogador remoto não virou evento\n"); ++g_fail; }
        // o amigo me acerta: caio e aviso todo mundo
        jogo_rede_evento(e, json{{"t", "dano"}, {"alvo", "eu"}, {"dano", 150}, {"de", "amigo"}, {"arma", "AK-74"}}.dump().c_str());
        frames(e, 2);
        ev = json::parse(std::string(jogo_rede_eventos(e)));
        bool abate = false;
        for (auto& x : ev) if (x["t"] == "abate" && x["v"] == "eu" && x["a"] == "amigo") abate = true;
        h = hud();
        std::printf("  depois do tiro do amigo: morto=%d placar %s, aviso de abate %s\n", int(h.value("morto", false)), h["placar"].dump().c_str(), abate ? "sim" : "não");
        if (!abate || h["placar"][1] != 1) { std::printf("  [ERRO] abate do jogador local\n"); ++g_fail; }
        // eu abato o amigo (aviso que chegou do celular dele), com skin na minha M4A1: o abate sai com a skin
        jogo_skin_arma(e, "m4a1", "neon");
        jogo_rede_evento(e, json{{"t", "abate"}, {"v", "amigo"}, {"a", "eu"}, {"arma", "M4A1"}}.dump().c_str());
        frames(e, 2);
        h = hud();
        std::printf("  depois de abater o amigo: placar %s, meus abates %d\n", h["placar"].dump().c_str(), int(h["abates"]));
        if (h["placar"][0] != 1 || h["abates"] != 1) { std::printf("  [ERRO] abate recebido\n"); ++g_fail; }
        {
            const json& fd = h["abatidos"].back();
            std::printf("  abate com skin: quadro skin=%s\n", fd.value("skin", "").c_str());
            if (fd.value("skin", "") != "m4a1:neon") {
                std::printf("  [ERRO] abate com skin\n"); ++g_fail;
            }
        }
        jogo_skin_arma(e, "m4a1", "");
        st["amigo"]["v"] = true; st["amigo"]["p"] = {p[0] + 3.f, p[1], p[2] - 12.f};
        jogo_rede_aplicar(e, st.dump().c_str());
        frames(e, 30);
        jogo_rede_evento(e, json{{"t", "granada"}, {"de", "amigo"}, {"p", {p[0], p[1] + 1.5f, p[2] - 6.f}}, {"v", {0, 3, 4}}}.dump().c_str());
        frames(e, 200);
        jogo_rede_evento(e, json{{"t", "saiu"}, {"id", "amigo"}}.dump().c_str());
        frames(e, 10);
        save(out, "34_depois.png");
        engine_set_play(e, 0);
        engine_set_first_person(e, 0);
        jogo_rede_sala(e, 0, "", "");
    };
    // Memória do jogo do APK: abre o projeto da partida (o mesmo do app) e joga uns segundos.
    // SO_MEMORIA = caminho do json; o nível gráfico vem de shaders/qualidade.txt (0 a 3)
    if (const char* proj = std::getenv("SO_MEMORIA")) {
        auto mem = [&](const char* fase) {
            long rss = 0, pico = 0;
            if (FILE* f = std::fopen("/proc/self/status", "r")) {
                char l[256];
                while (std::fgets(l, sizeof l, f)) { std::sscanf(l, "VmRSS: %ld", &rss); std::sscanf(l, "VmHWM: %ld", &pico); }
                std::fclose(f);
            }
            float m[4] = {};
            engine_memory_stats(e, m);
            std::printf("  %-26s RAM %4ld MB (pico %4ld MB) · texturas %4.0f MB · malhas %4.0f MB (%.0f)\n", fase, rss / 1024, pico / 1024, m[0], m[1], m[2]);
        };
        mem("motor criado");
        auto j = lerProjeto(proj);
        auto t0 = std::chrono::steady_clock::now();
        if (!engine_import_project(e, j.dump().c_str())) { std::printf("  projeto não abriu\n"); ++g_fail; }
        std::printf("  projeto aberto em %.2f s\n", std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count());
        engine_set_camera(e, 0.f, 8.f, 20.f, 3.6f, 0.42f, 115.f);
        frames(e, 5);
        mem("menu (mapa ao fundo)");
        save(out, "40_menu.png");
        jogo_escolhas(e, 0, 0, "m4a1", "tt33");
        engine_set_first_person(e, 1);
        t0 = std::chrono::steady_clock::now();
        engine_set_play(e, 1);
        std::printf("  partida começou em %.2f s\n", std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count());
        engine_set_camera(e, 0.f, 0.f, 0.f, 0.f, 0.05f, 3.f);
        frames(e, 120);
        mem("partida (10 lutadores)");
        {   // loja de armas na base: botão no HUD, abre, granada sem dinheiro é recusada
            auto hud = nlohmann::json::parse(std::string(jogo_hud(e)));
            std::string label = hud.value("interagir", "");
            std::printf("  na base: botão \"%s\", $%d\n", label.c_str(), hud.value("dinheiro", -1));
            if (label.find("Loja de armas") == std::string::npos) { std::printf("  [ERRO] sem loja na base\n"); ++g_fail; }
            engine_interact(e);
            std::string loja = jogo_loja(e);
            auto lj = nlohmann::json::parse(loja.empty() ? "{}" : loja);
            std::printf("  loja: %s, %zu itens\n", lj.value("loja", "?").c_str(), lj.contains("itens") ? lj["itens"].size() : 0);
            if (!lj.contains("itens") || lj["itens"].size() < 3) { std::printf("  [ERRO] loja vazia\n"); ++g_fail; }
            std::printf("  granada: %s\n", jogo_loja_comprar(e, 200));
            jogo_loja_fechar(e);
        }
        save(out, "40_partida.png");
        stats(e, "partida");
        if (std::getenv("TRIS")) {  // de onde vêm os triângulos (por nome de objeto)
            std::map<std::string, long> por;
            eng::Engine* E = reinterpret_cast<eng::Engine*>(e);
            E->renderer.trisPorNome = &por;
            frames(e, 1);
            E->renderer.trisPorNome = nullptr;
            std::vector<std::pair<long, std::string>> v;
            long tot = 0;
            for (auto& [n, t] : por) { v.push_back({t, n}); tot += t; }
            std::sort(v.rbegin(), v.rend());
            std::printf("  triângulos por objeto (total %ld):\n", tot);
            for (size_t i = 0; i < v.size() && i < 25; ++i) std::printf("    %7ld  %s\n", v[i].first, v[i].second.c_str());
        }
        {   // de cima, olhando o mapa inteiro (onde a distância de visão e os detalhes contam)
            engine_set_first_person(e, 0);
            engine_set_camera(e, 0.f, 22.f, 45.f, 0.f, 0.35f, 30.f);
            frames(e, 5);
            stats(e, "partida (de cima)");
            save(out, "40_de_cima.png");
            engine_set_first_person(e, 1);
            frames(e, 3);
        }
        engine_set_play(e, 0);
        engine_set_first_person(e, 0);
        frames(e, 5);
        jogo_escolhas(e, 1, 0, "ak47", "tt33");  // bandido de cabelo 3D (Rubro): na primeira pessoa o cabelo não aparece
        engine_set_first_person(e, 1);
        engine_set_play(e, 1);
        frames(e, 60);
        engine_set_camera(e, 0.f, 0.f, 0.f, 0.f, -0.5f, 3.f);  // olhando para cima: se o cabelo aparecesse, taparia aqui
        frames(e, 5);
        save(out, "41_bandido_cabelo_fp.png");
        mem("2ª partida");
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Escada externa em zigue-zague (casa de 3 andares da favela): o boneco sobe da rua até a laje do telhado
    if (std::getenv("SO_ESCADA_EXTERNA")) {
        engine_new_scene(e);
        const float W = 4.f, D = 5.f, lane = 0.9f;
        int base = -1;
        for (int f = 0; f < 3; ++f) {
            nlohmann::json m = {{"tipo", "modulo"}, {"largura", W}, {"profundidade", D}, {"altura", 2.7}, {"laje", true},
                                {"paredes", {"porta", "lisa", "lisa", "lisa"}}, {"escada", "externa"}, {"largura_escada", lane},
                                {"corredor_escada", (2 - f) % 2}, {"escada_para_frente", f % 2 == 1}, {"zigue_zague", true}};
            if (base < 0) m["pos"] = {0, 0, 0}; else m["sobre"] = base;
            base = engine_add_spec(e, m.dump().c_str());
            if (base < 0) { std::printf("  [ERRO] andar %d: %s\n", f, engine_last_error(e)); ++g_fail; }
        }
        engine_set_camera(e, -2.f, 3.5f, 0.f, -1.1f, 0.3f, 13.f);
        frames(e, 3);
        save(out, "escada_externa.png");
        int ch = engine_create_random_character(e, 7, 1);
        const float xi = -W / 2 - lane * 0.5f, xo = -W / 2 - lane * 1.5f;
        placeChar(ch, {xi, 0.3f, D / 2 + 0.8f});
        engine_set_play(e, 1);
        frames(e, 20);
        int etapa = 0;
        // lances com patamar no fim: 0 sobe para o fundo no corredor de dentro; no patamar, força para fora pela
        // borda (não pode cair); passa para o corredor de fora; 1 sobe para a frente; no patamar passa para o de
        // dentro; 2 sobe para o fundo; no último patamar força para o lado de fora; vai para a laje (+X)
        int empurra = 0;
        float yPonta0 = 1e9f, yPonta2 = 1e9f;
        glm::vec3 fim = steer(ch, 1100, [&](glm::vec3 p) -> glm::vec2 {
            switch (etapa) {
                case 0: case 5: if (p.z < -D / 2 + 0.45f) { ++etapa; empurra = 0; return {0.f, 0.f}; } return {(xi - p.x) * 3.f, -1.f};
                case 1: yPonta0 = std::min(yPonta0, p.y); if (++empurra > 70) ++etapa; return {0.f, -1.f};
                case 2: if (p.x < xo + 0.08f) { ++etapa; return {0.f, 0.f}; } return {-1.f, 0.f};
                case 3: if (p.z > D / 2 - 0.45f) { ++etapa; return {0.f, 0.f}; } return {(xo - p.x) * 3.f, 1.f};
                case 4: if (p.x > xi - 0.08f) { ++etapa; return {0.f, 0.f}; } return {1.f, 0.f};
                case 6: yPonta2 = std::min(yPonta2, p.y); if (++empurra > 70) ++etapa; return {-1.f, -0.3f};
                default: return p.x > 0.f ? glm::vec2(0.f) : glm::vec2(1.f, 0.f);
            }
        });
        std::printf("  chegou em %.2f %.2f %.2f (etapa %d) · empurrando nas pontas: y mín %.2f e %.2f\n", fim.x, fim.y, fim.z, etapa, yPonta0, yPonta2);
        if (yPonta0 < 2.4f) { std::printf("  [ERRO] caiu pela ponta do primeiro lance\n"); ++g_fail; }
        if (yPonta2 < 8.f) { std::printf("  [ERRO] caiu pelo lado no fim do último lance\n"); ++g_fail; }
        save(out, "escada_externa_play.png");
        if (fim.y < 8.f || fim.x < -W / 2) { std::printf("  [ERRO] não chegou na laje do telhado\n"); ++g_fail; }
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Personagem da tela inicial (SO_LOBBY=1): em duas camadas para combinar qualquer roupa com qualquer arma/skin,
    // em ANGULOS voltas (padrão 24, de 15 em 15 graus: a pessoa gira o personagem arrastando o dedo).
    //   previa_p_<roupa>_<NN>.png     o personagem segurando a arma, mas com a arma escondida (a pose fica)
    //   lobby_com/sem_<arma>_<NN>.png com e sem a arma: a diferença é só a arma (a mão por cima fica de fora)
    // LOBBY_SO=p (só roupas) ou a (só armas). Depois: python3 tools/recortar_lobby.py <saida> app/src/main/assets/lobby
    if (std::getenv("SO_LOBBY")) {
        using nlohmann::json;
        eng::Engine* E = reinterpret_cast<eng::Engine*>(e);
        static const char* keys[2][6] = {{"pm", "civil", "exercito", "choque", "especial", "bope_caveira"}, {"rubro", "alvinegro", "tricolor", "canarinho", "faixa", ""}};
        const float cam[6] = {0.f, 1.0f, 0.f, std::getenv("CAM_YAW") ? float(std::atof(std::getenv("CAM_YAW"))) : -0.62f, 0.05f,
                              std::getenv("CAM_DIST") ? float(std::atof(std::getenv("CAM_DIST"))) : 2.4f};
        auto camera = [&] { engine_set_camera(e, cam[0], cam[1], cam[2], cam[3], cam[4], cam[5]); };
        // um personagem com a arma na mão; devolve o objeto da arma (ou nulo)
        auto montar = [&](int t, int k, const std::string& arma, const std::string& skin, float giro) -> eng::GameObject* {
            engine_new_scene(e);
            engine_set_time_of_day(e, 13.f);
            engine_set_quality(e, 0);
            camera();
            json look = json::parse(jogo_aparencia_roupa(t, k));
            const int id = engine_add_spec(e, json{{"tipo", "personagem"}, {"aparencia", look}, {"semente", 7}, {"pos", {0, 0, 0}},
                                                   {"rot", {0, giro, 0}}, {"arma", arma}}.dump().c_str());
            if (id < 0) { std::printf("  [ERRO] %s: %s\n", arma.c_str(), engine_last_error(e)); ++g_fail; return nullptr; }
            frames(e, 40);  // a pose assenta (sempre o mesmo número de quadros: a mesma pose em todas as fotos)
            for (const eng::CharacterInfo& c : E->characters)
                if (eng::GameObject* w = E->scene.get(c.weapon)) {
                    if (!skin.empty()) {
                        float metal = -1.f, rough = -1.f;
                        eng::gunSkinFinish(arma, skin, metal, rough);
                        eng::applyKitTexture(E, *w, "armas/skins/" + arma + "_" + skin + ".jpg", metal, rough);
                    }
                    return w;
                }
            return nullptr;
        };
        auto esconder = [&](eng::GameObject* w) { if (w) eng::Scene::forEachInSubtree(*w, [](eng::GameObject& o) { o.active = false; }); };
        engine_new_scene(e);
        engine_set_time_of_day(e, 13.f);
        engine_set_quality(e, 0);
        camera();
        frames(e, 3);
        save(out, "previa_fundo.png");
        const int angulos = std::max(1, std::getenv("ANGULOS") ? std::atoi(std::getenv("ANGULOS")) : 24);
        const std::string so = std::getenv("LOBBY_SO") ? std::getenv("LOBBY_SO") : "";
        auto sufixo = [&](int i) { char b[8]; std::snprintf(b, sizeof(b), "_%02d", i); return std::string(b); };
        for (int t = 0; t < 2 && so != "a"; ++t)
            for (int k = 0; k < 6; ++k) {
                if (!keys[t][k][0]) continue;
                for (int i = 0; i < angulos; ++i) {
                    eng::GameObject* w = montar(t, k, "m4a1", "", 360.f * i / angulos);
                    esconder(w);
                    frames(e, 1, 0.f);
                    save(out, (std::string("previa_p_") + keys[t][k] + sufixo(i) + ".png").c_str());
                }
            }
        // armas (e as skins de cada uma): com a arma e sem ela, na mesma pose
        std::string lista = std::getenv("ARMAS_LOBBY") ? std::getenv("ARMAS_LOBBY") : "m4a1,m4colt,ak47,ak74,ak74_tatica";
        size_t a = 0;
        while (a < lista.size() && so != "p") {
            size_t b = lista.find(',', a); if (b == std::string::npos) b = lista.size();
            std::string item = lista.substr(a, b - a); a = b + 1;
            size_t c = item.find(':');
            std::string arma = item.substr(0, c), skin = c == std::string::npos ? "" : item.substr(c + 1);
            const std::string nome = arma + (skin.empty() ? "" : "_" + skin);
            for (int i = 0; i < angulos; ++i) {
                eng::GameObject* w = montar(0, 0, arma, skin, 360.f * i / angulos);
                if (!w) break;
                frames(e, 1, 0.f);
                save(out, ("lobby_com_" + nome + sufixo(i) + ".png").c_str());
                esconder(w);
                frames(e, 1, 0.f);
                save(out, ("lobby_sem_" + nome + sufixo(i) + ".png").c_str());
            }
        }
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    if (std::getenv("SO_RETRATOS_BLOCO")) {
        using nlohmann::json;
        static const char* keys[2][6] = {{"pm", "civil", "exercito", "choque", "especial", "bope_caveira"}, {"rubro", "alvinegro", "tricolor", "canarinho", "faixa", ""}};
        engine_new_scene(e);
        engine_set_time_of_day(e, 13.f);
        engine_set_quality(e, 0);  // sem sombra no chão (o recorte pega só o boneco)
        engine_set_camera(e, 0.f, 0.95f, 0.f, 0.35f, 0.08f, 3.2f);
        frames(e, 3);
        save(out, "previa_fundo.png");
        for (int t = 0; t < 2; ++t)
            for (int k = 0; k < 6; ++k) {
                if (!keys[t][k][0]) continue;
                json look = json::parse(jogo_aparencia_roupa(t, k));
                if (std::getenv("COLETE")) { look["colete"] = true; look["cor_colete"] = "#2A2D33"; }  // colete em todas (conferir)
                int id = engine_add_spec(e, json{{"tipo", "personagem"}, {"aparencia", look}, {"semente", 7}, {"pos", {0, 0, 0}}}.dump().c_str());
                if (id < 0) { std::printf("  [ERRO] roupa %d %d: %s\n", t, k, engine_last_error(e)); ++g_fail; continue; }
                engine_set_camera(e, 0.f, 0.95f, 0.f, 0.35f, 0.08f, 3.2f);
                frames(e, 4);
                save(out, (std::string("previa_") + keys[t][k] + ".png").c_str());
                if (std::getenv("TRONCO")) {  // o tronco de perto (colete), de três quartos e de trás
                    engine_set_camera(e, 0.f, 1.2f, 0.f, 0.55f, 0.15f, 1.5f);
                    frames(e, 2);
                    save(out, (std::string("tronco_") + keys[t][k] + ".png").c_str());
                    engine_set_camera(e, 0.f, 1.2f, 0.f, 3.6f, 0.2f, 1.5f);
                    frames(e, 2);
                    save(out, (std::string("costas_") + keys[t][k] + ".png").c_str());
                    engine_set_camera(e, 0.f, 1.2f, 0.f, 1.5708f, 0.1f, 1.3f);
                    frames(e, 2);
                    save(out, (std::string("lado_") + keys[t][k] + ".png").c_str());
                }
                if (std::getenv("PERTO")) {  // a cabeça de perto (cabelo 3D), de três quartos e de trás
                    engine_set_camera(e, 0.f, 1.62f, 0.f, 0.6f, 0.12f, 0.75f);
                    frames(e, 2);
                    save(out, (std::string("cabeca_") + keys[t][k] + ".png").c_str());
                    engine_set_camera(e, 0.f, 1.62f, 0.f, 3.5f, 0.25f, 0.75f);
                    frames(e, 2);
                    save(out, (std::string("nuca_") + keys[t][k] + ".png").c_str());
                }
                engine_remove_object(e, id);
            }
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Prévias das armas com as skins do mercado (fundo puro: o recorte é feito depois por tools/recortar_previas.py)
    // SO_PREVIAS = "arma:skin,arma:skin,..." (skin vazia = padrão)
    if (const char* lista = std::getenv("SO_PREVIAS")) {
        using nlohmann::json;
        engine_new_scene(e);
        engine_set_time_of_day(e, 13.f);
        engine_set_camera(e, 0.f, 1.02f, 0.f, 1.5708f + 0.18f, 0.12f, 1.05f);
        engine_set_quality(e, 0);
        frames(e, 3);
        save(out, "previa_fundo.png");
        engine_set_quality(e, 3);
        if (std::getenv("HORA")) engine_set_time_of_day(e, std::atof(std::getenv("HORA")));  // ex.: HORA=22 (efeito à noite)
        std::string l = lista;
        size_t a = 0;
        while (a < l.size()) {
            size_t b = l.find(',', a); if (b == std::string::npos) b = l.size();
            std::string item = l.substr(a, b - a); a = b + 1;
            size_t c = item.find(':');
            std::string g = item.substr(0, c), sk = c == std::string::npos ? "" : item.substr(c + 1);
            json spec = {{"tipo", "arma"}, {"arma", g}, {"pos", {0, 1.0, 0}}};
            if (!sk.empty()) spec["textura"] = "kit:armas/skins/" + g + "_" + sk + ".jpg";
            float metal = -1.f, rough = -1.f;
            eng::gunSkinFinish(g, sk, metal, rough);
            if (!sk.empty() && metal >= 0.f) { spec["metal"] = metal; spec["rugosidade"] = rough; }
            int id = engine_add_spec(e, spec.dump().c_str());
            if (id < 0) { std::printf("  [ERRO] %s: %s\n", item.c_str(), engine_last_error(e)); ++g_fail; continue; }
            engine_set_camera(e, 0.f, 1.02f, 0.f, 1.5708f + 0.18f, 0.12f, g == "tt33" ? 0.6f : 1.05f);
            engine_set_quality(e, 0);  // sem sombra na foto (a arma já foi carregada no nível do arquivo)
            frames(e, 3);
            save(out, ("previa_" + g + (sk.empty() ? "" : "_" + sk) + ".png").c_str());
            if (std::getenv("ANGULOS")) {  // mais fotos: o outro lado, de cima na diagonal e de perto (conferir a skin)
                const float d = g == "tt33" ? 0.6f : 1.05f;
                const float cams[4][3] = {{-1.5708f - 0.18f, 0.12f, d}, {0.7f, 0.55f, d * 0.95f}, {2.4f, 0.35f, d * 0.95f}, {1.5708f + 0.4f, 0.2f, d * 0.55f}};
                for (int k = 0; k < 4; ++k) {
                    engine_set_camera(e, k == 3 ? -0.15f : 0.f, 1.02f, 0.f, cams[k][0], cams[k][1], cams[k][2]);
                    frames(e, 20);  // (efeitos de partícula da skin, como os raios, já aparecem)
                    if (std::getenv("TIRO")) { engine_test_shot(e, id); frames(e, std::atoi(std::getenv("TIRO"))); }  // TIRO=quadros depois do tiro
                    save(out, ("angulo" + std::to_string(k) + "_" + g + (sk.empty() ? "" : "_" + sk) + ".png").c_str());
                }
            }
            engine_set_quality(e, 3);
            engine_remove_object(e, id);
        }
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Pular janela e câmera colada na parede (SO_JANELA=1): casa com janela na frente; o boneco corre, pula e tem que
    // cair dentro; depois anda contra a parede do fundo e o olho não pode entrar nela (fotos janela_*.png)
    if (std::getenv("SO_JANELA")) {
        using nlohmann::json;
        eng::Engine* E = reinterpret_cast<eng::Engine*>(e);
        for (int fp = 1; fp >= 0; --fp) {
            engine_new_scene(e);
            engine_add_spec(e, json{{"tipo", "modulo"}, {"pos", {0, 0, -3.3}}, {"paredes", {"janela", "lisa", "lisa", "lisa"}}, {"escada", "nenhuma"}}.dump().c_str());
            const int ch = engine_create_random_character(e, 7, 1);
            engine_set_first_person(e, fp);
            engine_set_play(e, 1);
            engine_set_camera(e, 0.f, 1.f, 0.f, 0.f, fp ? 0.f : 0.25f, 4.f);
            frames(e, 30);
            float p[3], r[3], sc[3];
            engine_player_input(e, 0.f, 1.f, 0);
            for (int k = 0; k < 120; ++k) {
                frames(e, 1);
                engine_get_transform(e, ch, p, r, sc);
                if (p[2] < -0.55f) break;
            }
            engine_player_jump(e);
            for (int k = 0; k < 50; ++k) {
                frames(e, 1);
                if (k % 6 == 0) save(out, ("janela_" + std::string(fp ? "fp_" : "tp_") + std::to_string(k) + ".png").c_str());
            }
            engine_player_input(e, 0.f, 0.f, 0);
            frames(e, 20);
            engine_get_transform(e, ch, p, r, sc);
            std::printf("  janela (%s): depois do pulo z=%.2f y=%.2f\n", fp ? "1ª pessoa" : "3ª pessoa", p[2], p[1]);
            if (p[2] > -1.75f || p[1] < 0.05f || p[1] > 0.5f) { std::printf("  [ERRO] não entrou pela janela\n"); ++g_fail; }
            if (fp) {  // contra a parede do fundo (face de dentro em z = -5,15): o olho fica do lado de cá
                engine_player_input(e, 0.f, 1.f, 1);
                frames(e, 90);
                save(out, "janela_parede.png");
                std::printf("  olho colado na parede: z=%.3f (parede em -5,15)\n", E->fpEye.z);
                if (E->fpEye.z < -5.15f + 0.09f) { std::printf("  [ERRO] o olho entrou na parede\n"); ++g_fail; }
                engine_player_input(e, 0.f, 0.f, 0);
            }
            engine_set_play(e, 0);
        }
        // parado perto da janela, de frente (1) ou de lado (2): só o botão de pular, sem andar
        for (int modo = 1; modo <= 2; ++modo) {
            engine_new_scene(e);
            engine_add_spec(e, json{{"tipo", "modulo"}, {"pos", {0, 0, -3.3}}, {"paredes", {"janela", "lisa", "lisa", "lisa"}}, {"escada", "nenhuma"}}.dump().c_str());
            const int ch = engine_create_random_character(e, 7, 1);
            engine_set_first_person(e, 1);
            engine_set_play(e, 1);
            engine_set_camera(e, 0.f, 1.f, 0.f, 0.f, 0.f, 4.f);
            frames(e, 30);
            float p[3], r[3], sc[3];
            engine_player_input(e, 0.f, 1.f, 0);
            for (int k = 0; k < 120; ++k) {
                frames(e, 1);
                engine_get_transform(e, ch, p, r, sc);
                if (p[2] < -0.3f) break;
            }
            engine_player_input(e, 0.f, 0.f, 0);
            if (modo == 2) engine_set_camera(e, p[0], 1.f, p[2], 1.5708f, 0.f, 4.f);  // vira de lado para a janela
            frames(e, 10);
            engine_player_jump(e);
            for (int k = 0; k < 50; ++k) {
                frames(e, 1);
                if (k % 10 == 0) save(out, ("janela_" + std::string(modo == 1 ? "parado_" : "lado_") + std::to_string(k) + ".png").c_str());
            }
            frames(e, 20);
            engine_get_transform(e, ch, p, r, sc);
            std::printf("  janela (%s, só pular): depois do pulo z=%.2f y=%.2f\n", modo == 1 ? "parado de frente" : "de lado", p[2], p[1]);
            if (p[2] > -1.75f || p[1] < 0.05f || p[1] > 0.5f) { std::printf("  [ERRO] não entrou pela janela\n"); ++g_fail; }
            engine_set_play(e, 0);
        }
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Porta levando tiro (SO_PORTA=1): de um lado abre para o outro (o lado para onde a bala ia), e ao contrário
    if (std::getenv("SO_PORTA")) {
        using nlohmann::json;
        eng::Engine* E = reinterpret_cast<eng::Engine*>(e);
        engine_new_scene(e);
        engine_add_spec(e, json{{"tipo", "modulo"}, {"pos", {0, 0, -3.3}}, {"paredes", {"porta", "lisa", "lisa", "lisa"}}, {"escada", "nenhuma"}}.dump().c_str());
        engine_create_random_character(e, 7, 1);
        engine_set_first_person(e, 1);
        engine_set_play(e, 1);
        engine_set_camera(e, 6.f, 1.f, 6.f, 0.f, 0.f, 4.f);
        frames(e, 30);
        if (E->doors.empty()) { std::printf("  [ERRO] módulo sem porta\n"); ++g_fail; }
        else {
            eng::DoorRT& d = E->doors[0];
            eng::GameObject* o = E->scene.get(d.h);
            const glm::vec3 n = d.base * glm::vec3(0, 0, 1);
            const glm::vec3 c = glm::vec3(o->transform.world() * glm::vec4(0.5f, 0.5f, 0.f, 1.f));
            for (float lado : {1.f, -1.f}) {  // atira de +n para -n e depois de -n para +n
                const glm::vec3 de = c + n * (3.f * lado), ate = c - n * (3.f * lado);
                const bool acertou = eng::tiroNasPortas(E, de, ate);
                frames(e, 90);
                const glm::vec3 dob = glm::vec3(o->transform.world() * glm::vec4(0.f, 0.5f, 0.f, 1.f));
                const glm::vec3 meio = glm::vec3(o->transform.world() * glm::vec4(0.5f, 0.5f, 0.f, 1.f));
                const float para = glm::dot(meio - dob, -n * lado);  // > 0: a folha foi para o lado da bala
                std::printf("  porta: tiro %s, acertou=%d, ângulo %.2f, folha para o lado da bala %.2f m\n",
                            lado > 0 ? "de fora (+n)" : "de dentro (-n)", acertou, d.angle, para);
                if (!acertou || para < 0.3f) { std::printf("  [ERRO] a porta não abriu para o lado do tiro\n"); ++g_fail; }
                save(out, lado > 0 ? "porta_tiro_1.png" : "porta_tiro_2.png");
            }
            // tiro que passa longe da porta: não mexe
            const float antes = d.tiroTempo;
            if (eng::tiroNasPortas(E, c + glm::vec3(10, 0, 10), c + glm::vec3(10, 0, -10)) || d.tiroTempo > antes) {
                std::printf("  [ERRO] tiro longe abriu a porta\n"); ++g_fail;
            }
        }
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Mirando com cada arma (SO_MIRA=1): foto da primeira pessoa mirando (mira_<arma>.png); o centro da tela é
    // onde a bala vai e onde fica o ponto vermelho do HUD
    if (std::getenv("SO_MIRA")) {
        const char* armas[] = {"m4a1", "m4colt", "ak47", "ak74", "ak74_tatica"};
        for (const char* a : armas) {
            abrirPartida();
            const int time = a[0] == 'a' ? 1 : 0;
            jogo_rede_sala(e, 1, "eu", (std::string(R"([{"id":"eu","nome":"Eu","time":)") + std::to_string(time) + R"(,"skin":0,"arma":")" + a + R"("}])").c_str());
            jogo_escolhas(e, time, 0, a, "tt33");
            engine_set_first_person(e, 1);
            engine_set_play(e, 1);
            engine_set_camera(e, 0.f, 0.f, 0.f, 0.f, 0.05f, 3.f);
            frames(e, 60);
            engine_player_aim(e, 1);
            frames(e, 40);
            save(out, (std::string("mira_") + a + ".png").c_str());
            {   // onde a alça de mira está em relação ao olho (deve ficar na linha do olhar)
                eng::Engine* E = reinterpret_cast<eng::Engine*>(e);
                auto& c = E->characters[E->player];
                if (auto* w = E->scene.get(c.weapon)) {
                    glm::vec3 sw = glm::vec3(w->transform.world() * glm::vec4(c.sight, 1.f));
                    glm::vec3 v = glm::normalize(E->camera.rotation * glm::vec3(0, 0, -1)), d = sw - E->fpEye;
                    glm::vec3 up = glm::normalize(E->camera.rotation * glm::vec3(0, 1, 0));
                    glm::vec3 rt = glm::normalize(glm::cross(v, up));
                    std::printf("  %s: adsW %.2f supportW %.2f · alça %.3f m à frente, %.3f m acima, %.3f m à direita da linha do olhar\n", a, c.adsW, c.supportW,
                                glm::dot(d, v), glm::dot(d, up), glm::dot(d, rt));
                }
            }
            if (const char* z = std::getenv("ZOOM")) { engine_scope_zoom(e, std::atoi(z)); frames(e, 30); save(out, (std::string("mira_") + a + "_zoom.png").c_str()); }
            engine_player_aim(e, 0);
            engine_set_play(e, 0);
        }
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Offline: inimigo abatido derruba munição; a vida volta fora do combate (SO_DROP=1)
    if (std::getenv("SO_DROP")) {
        eng::Engine* E = reinterpret_cast<eng::Engine*>(e);
        abrirPartida();
        jogo_escolhas(e, 0, 0, "m4a1", "tt33");
        engine_set_first_person(e, 1);
        engine_set_play(e, 1);
        frames(e, 30);
        eng::TeamMatch& M = eng::matchOf(E);
        int pf = -1, en = -1;
        for (size_t i = 0; i < M.f.size(); ++i) { if (!M.f[i].bot) pf = int(i); else if (M.f[i].team == 1 && en < 0) en = int(i); }
        if (pf < 0 || en < 0) { std::printf("  [ERRO] sem jogador ou inimigo\n"); ++g_fail; }
        else {
            M.attacker = pf;
            E->module->onDamage(E, E->scene.get(M.f[en].root), 500.f);
            M.attacker = -1;
            std::printf("  inimigo abatido: %s · caixas no chão: %zu\n", M.f[en].dead ? "sim" : "não", M.drops.size());
            if (M.drops.size() != 1) { std::printf("  [ERRO] não caiu munição\n"); ++g_fail; }
            else {
                int ci = E->player;
                int antes = E->characters[ci].reserve;
                float p[3], r[3], sc[3];
                int ch = engine_player_id(e);
                engine_get_transform(e, ch, p, r, sc);
                p[0] = M.drops[0].pos.x; p[1] = M.drops[0].pos.y + 0.1f; p[2] = M.drops[0].pos.z;
                engine_set_transform(e, ch, p, r, sc);
                frames(e, 10);
                std::printf("  munição: reserva %d -> %d · caixas %zu\n", antes, E->characters[ci].reserve, M.drops.size());
                if (E->characters[ci].reserve <= antes || !M.drops.empty()) { std::printf("  [ERRO] não pegou a munição\n"); ++g_fail; }
            }
            for (auto& f : M.f) if (f.bot) { f.dead = true; f.deadTime = -1e6f; }  // sem robôs atirando no teste da vida
            E->game.playerHp = 40.f; M.lastDamage = 0.f;
            frames(e, 180);
            const float v3 = E->game.playerHp;
            frames(e, 240);
            std::printf("  vida: 40 -> %.0f (3 s) -> %.0f (7 s)\n", v3, E->game.playerHp);
            if (v3 > 41.f || E->game.playerHp < 60.f) { std::printf("  [ERRO] a vida não voltou como esperado\n"); ++g_fail; }
        }
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Foto do mapa (SO_FOTO="x,y,z,yaw,pitch,dist;..."; HORA=17): fotos foto_<n>.png (tamanho: LARGURA/ALTURA)
    if (const char* lista = std::getenv("SO_FOTO")) {
        const char* proj = "app/src/main/assets/exemplos/5_policia_x_bandidos.json";
        if (!engine_import_project(e, lerProjeto(proj).dump().c_str())) { std::printf("  projeto não abriu\n"); ++g_fail; }
        engine_set_time_of_day(e, std::getenv("HORA") ? std::atof(std::getenv("HORA")) : 17.f);
        std::string l = lista;
        int n = 0;
        size_t a = 0;
        while (a < l.size()) {
            size_t b = l.find(';', a); if (b == std::string::npos) b = l.size();
            float v[6] = {0, 8, 20, 3.6f, 0.42f, 115};
            std::sscanf(l.substr(a, b - a).c_str(), "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
            a = b + 1;
            engine_set_camera(e, v[0], v[1], v[2], v[3], v[4], v[5]);
            frames(e, 4);
            char fn[32]; std::snprintf(fn, sizeof fn, "foto_%d.png", n++);
            save(out, fn);
        }
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Escadarias de perto (SO_ESCADARIAS=1): fotos escadaria_<rota>_<n>.png em pontos de cada rota
    if (std::getenv("SO_ESCADARIAS")) {
        const char* proj = "app/src/main/assets/exemplos/5_policia_x_bandidos.json";
        if (!engine_import_project(e, lerProjeto(proj).dump().c_str())) { std::printf("  projeto não abriu\n"); ++g_fail; }
        engine_set_time_of_day(e, 11.f);
        const auto& rotas = eng::favelaRotas();
        for (size_t ri = 0; ri < rotas.size(); ++ri) {
            const auto& rt = rotas[ri];
            for (size_t k = 0; k + 1 < rt.size(); k += std::max<size_t>(1, rt.size() / 2)) {
                glm::vec2 a = rt[k], b = rt[k + 1], m = (a + b) * 0.5f, d = glm::normalize(b - a);
                glm::vec3 hit; eng::GameObject* obj = nullptr;
                float h = reinterpret_cast<eng::Engine*>(e)->physics.raycast({m.x, 80.f, m.y}, {m.x, -40.f, m.y}, hit, obj) ? hit.y : 0.f;
                engine_set_camera(e, m.x, h + 0.5f, m.y, std::atan2(-d.x, -d.y) + 0.5f, 0.32f, 7.f);
                frames(e, 3);
                char n[48]; std::snprintf(n, sizeof n, "escadaria_%zu_%zu.png", ri, k);
                save(out, n);
            }
        }
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Céu ao longo do dia (SO_CEU=1): o mapa da partida de várias horas e com chuva (fotos ceu_<hora>.png, chuva_<hora>.png)
    if (std::getenv("SO_CEU")) {
        const char* proj = "app/src/main/assets/exemplos/5_policia_x_bandidos.json";
        if (!engine_import_project(e, lerProjeto(proj).dump().c_str())) { std::printf("  projeto não abriu\n"); ++g_fail; }
        engine_set_quality(e, 1);
        for (float h : {5.6f, 7.f, 12.f, 17.f, 17.8f, 18.3f, 19.f, 21.f, 1.f}) {
            engine_set_time_of_day(e, h);
            engine_set_camera(e, 0.f, 8.f, 20.f, 3.6f, 0.3f, 115.f);
            frames(e, 3);
            char n[32]; std::snprintf(n, sizeof n, "ceu_%04.1f.png", h);
            save(out, n);
        }
        engine_set_play(e, 1);
        eng::Engine* E = reinterpret_cast<eng::Engine*>(e);  // (o Engine da API é o eng::Engine)
        E->rainMode = 2;
        for (float h : {14.f, 21.f}) {
            engine_set_time_of_day(e, h);
            E->rain = E->rainTarget = 1.f;
            engine_set_camera(e, 0.f, 8.f, 20.f, 3.6f, 0.15f, 18.f);
            frames(e, 40);
            char n[32]; std::snprintf(n, sizeof n, "chuva_%04.1f.png", h);
            save(out, n);
        }
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Movimento no chão plano: a mesma velocidade para frente, para o lado e para trás; pulo rápido (sobe e desce)
    if (std::getenv("SO_PULO")) {
        engine_new_scene(e);
        const int ch = engine_create_random_character(e, 7, 1);
        engine_set_play(e, 1);
        frames(e, 30);
        float p[3], r[3], sc[3];
        auto vel = [&](float x, float y, int corre) {
            engine_set_camera(e, 0.f, 1.f, 0.f, 0.f, 0.2f, 4.f);
            engine_player_input(e, x, y, corre);
            frames(e, 30);
            engine_get_transform(e, ch, p, r, sc);
            glm::vec2 a(p[0], p[2]);
            frames(e, 30);
            engine_get_transform(e, ch, p, r, sc);
            engine_player_input(e, 0.f, 0.f, 0);
            frames(e, 20);
            return glm::length(glm::vec2(p[0], p[2]) - a) * 2.f;  // m/s (30 quadros = 0,5 s)
        };
        const float vf = vel(0.f, 1.f, 1), vl = vel(1.f, 0.f, 1), vt = vel(0.f, -1.f, 1), vd = vel(0.7071f, 0.7071f, 1);
        std::printf("  correndo: frente %.2f · lado %.2f · trás %.2f · diagonal %.2f m/s\n", vf, vl, vt, vd);
        for (float v : {vl, vt, vd}) if (std::abs(v - vf) > 0.15f * vf) { std::printf("  [ERRO] velocidade diferente conforme a direção\n"); ++g_fail; break; }
        engine_get_transform(e, ch, p, r, sc);
        const float y0 = p[1];
        engine_player_jump(e);
        float topo = y0; int noAr = 0;
        for (int k = 0; k < 120; ++k) {
            frames(e, 1);
            engine_get_transform(e, ch, p, r, sc);
            topo = std::max(topo, p[1]);
            if (p[1] > y0 + 0.06f) noAr = k + 1;  // (parado, o boneco assenta uns 3 cm acima de onde estava)
            if (std::getenv("DEPURAR") && k % 6 == 0) std::printf("    %3d y=%.3f\n", k, p[1] - y0);
            if (std::getenv("FOTOS") && k % 5 == 0 && k <= 50) save(out, ("pulo_" + std::to_string(k) + ".png").c_str());
        }
        std::printf("  pulo: altura %.2f m · no ar %.2f s\n", topo - y0, noAr / 60.f);
        if (topo - y0 < 0.5f || topo - y0 > 1.f || noAr / 60.f > 0.75f) { std::printf("  [ERRO] pulo fora do esperado (0,5 a 1 m, até 0,75 s)\n"); ++g_fail; }
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    // Caminhada pelas escadarias principais: o boneco anda (e corre) por cada rota e o teste anota onde ele trava
    // (velocidade bem abaixo da pedida por vários quadros seguidos). SO_CAMINHADA=1
    if (std::getenv("SO_CAMINHADA")) {
        abrirPartida();
        // sozinho (sala online só comigo: sem robôs atirando, morrendo e renascendo no meio do caminho) e com a
        // imagem pequena (o teste anda muito; desenhar menos deixa ele bem mais rápido)
        jogo_rede_sala(e, 1, "eu", R"([{"id":"eu","nome":"Eu","time":1,"skin":0,"arma":"ak47"}])");
        engine_resize(e, 240, 135);
        jogo_escolhas(e, 1, 0, "ak47", "tt33");
        engine_set_play(e, 1);
        frames(e, 10);
        const int ch = engine_player_id(e);
        int travadas = 0, quadros = 0;
        double perda = 0.0;
        const size_t nRotas = std::getenv("ROTA") ? eng::favelaRotas().size() : std::getenv("ROTAS") ? size_t(std::atoi(std::getenv("ROTAS"))) : eng::favelaRotas().size();
        const int soCorre = std::getenv("CORRE") ? std::atoi(std::getenv("CORRE")) : -1;    // 0 andando, 1 correndo
        const int soSentido = std::getenv("SENTIDO") ? std::atoi(std::getenv("SENTIDO")) : -1;  // 0 subindo, 1 descendo
        for (int corre = 0; corre < 2; ++corre)
        for (size_t ri = 0; ri < std::min(nRotas, eng::favelaRotas().size()); ++ri) {
            const auto& rt = eng::favelaRotas()[ri];
            if (soCorre >= 0 && corre != soCorre) continue;
            if (std::getenv("ROTA") && int(ri) != std::atoi(std::getenv("ROTA"))) continue;  // uma rota só
            for (int sentido = 0; sentido < 2; ++sentido) {
                if (soSentido >= 0 && sentido != soSentido) continue;
                std::vector<glm::vec2> pts(rt.begin(), rt.end());
                if (sentido) std::reverse(pts.begin(), pts.end());
                float p[3], r[3], sc[3];
                engine_get_transform(e, ch, p, r, sc);
                p[0] = pts[0].x; p[2] = pts[0].y; p[1] = 60.f;  // cai no começo da rota
                engine_set_transform(e, ch, p, r, sc);
                for (int k = 0; k < 60; ++k) frames(e, 1);
                size_t alvo = 1;
                glm::vec3 ant(p[0], p[1], p[2]);
                int lento = 0;
                const float pedida = corre ? 6.5f : 2.6f;
                for (int k = 0; k < 60 * 90 && alvo < pts.size(); ++k) {
                    engine_get_transform(e, ch, p, r, sc);
                    glm::vec2 pos(p[0], p[2]);
                    if (glm::length(pts[alvo] - pos) < 1.2f) { ++alvo; continue; }
                    glm::vec2 d = glm::normalize(pts[alvo] - pos);
                    engine_set_camera(e, p[0], p[1] + 1.f, p[2], 0.f, 0.3f, 4.f);
                    engine_player_input(e, d.x, -d.y, corre);
                    frames(e, 1);
                    engine_get_transform(e, ch, p, r, sc);
                    glm::vec3 agora(p[0], p[1], p[2]);
                    float v = glm::length(glm::vec2(agora.x - ant.x, agora.z - ant.z)) * 60.f;
                    ant = agora;
                    ++quadros;
                    perda += std::max(0.f, pedida - v) / pedida;
                    if (v < pedida * 0.4f) {
                        if (++lento == 6) {
                            ++travadas;
                            if (travadas <= 3) {  // foto de onde travou (de trás do boneco e de cima)
                                engine_set_camera(e, agora.x, agora.y + 1.f, agora.z, std::atan2(-d.x, -d.y), 0.35f, 4.f);
                                engine_set_first_person(e, 0); engine_resize(e, W, H); frames(e, 2);
                                char fn[64]; std::snprintf(fn, sizeof fn, "trava_%d.png", travadas); save(out, fn);
                                engine_resize(e, 240, 135);
                            }
                            if (travadas <= 25) std::printf("  trava: rota %zu %s %s em (%.1f, %.1f, %.1f) v=%.2f\n", ri, sentido ? "descendo" : "subindo",
                                                            corre ? "correndo" : "andando", agora.x, agora.y, agora.z, v);
                        }
                    } else lento = 0;
                }
                if (alvo < pts.size()) { std::printf("  [ERRO] rota %zu não chegou ao fim (parou no ponto %zu de %zu)\n", ri, alvo, pts.size()); ++g_fail; }
                engine_player_input(e, 0.f, 0.f, 0);
            }
        }
        std::printf("  travadas: %d · velocidade perdida em média: %.0f%% (%d quadros)\n", travadas, perda / std::max(1, quadros) * 100.0, quadros);
        engine_destroy(e);
        std::printf(g_fail ? "FALHOU\n" : "OK\n");
        return g_fail ? 1 : 0;
    }
    if (std::getenv("SO_ONLINE")) { online(); engine_destroy(e); std::printf(g_fail ? "FALHOU\n" : "OK\n"); return g_fail ? 1 : 0; }
    if (std::getenv("SO_JOGO")) { jogo(); engine_destroy(e); std::printf(g_fail ? "FALHOU\n" : "OK\n"); return g_fail ? 1 : 0; }
    // sem variável: os testes principais
    jogo();
    online();
    engine_destroy(e);
    std::printf(g_fail ? "FALHOU\n" : "OK\n");
    return g_fail ? 1 : 0;
}
