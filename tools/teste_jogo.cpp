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

static const int W = 960, H = 540;
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
        // eu abato o amigo (aviso que chegou do celular dele)
        jogo_rede_evento(e, json{{"t", "abate"}, {"v", "amigo"}, {"a", "eu"}, {"arma", "M4A1"}}.dump().c_str());
        frames(e, 2);
        h = hud();
        std::printf("  depois de abater o amigo: placar %s, meus abates %d\n", h["placar"].dump().c_str(), int(h["abates"]));
        if (h["placar"][0] != 1 || h["abates"] != 1) { std::printf("  [ERRO] abate recebido\n"); ++g_fail; }
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
        jogo_escolhas(e, 1, 3, "ak47", "tt33");
        engine_set_first_person(e, 1);
        engine_set_play(e, 1);
        frames(e, 60);
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
    if (std::getenv("SO_RETRATOS_BLOCO")) {
        using nlohmann::json;
        static const char* keys[2][5] = {{"pm", "civil", "exercito", "choque", "especial"}, {"rubro", "alvinegro", "tricolor", "canarinho", "faixa"}};
        engine_new_scene(e);
        engine_set_time_of_day(e, 13.f);
        engine_set_quality(e, 0);  // sem sombra no chão (o recorte pega só o boneco)
        engine_set_camera(e, 0.f, 0.95f, 0.f, 0.35f, 0.08f, 3.2f);
        frames(e, 3);
        save(out, "previa_fundo.png");
        for (int t = 0; t < 2; ++t)
            for (int k = 0; k < 5; ++k) {
                json look = json::parse(jogo_aparencia_roupa(t, k));
                int id = engine_add_spec(e, json{{"tipo", "personagem"}, {"aparencia", look}, {"semente", 7}, {"pos", {0, 0, 0}}}.dump().c_str());
                if (id < 0) { std::printf("  [ERRO] roupa %d %d: %s\n", t, k, engine_last_error(e)); ++g_fail; continue; }
                engine_set_camera(e, 0.f, 0.95f, 0.f, 0.35f, 0.08f, 3.2f);
                frames(e, 4);
                save(out, (std::string("previa_") + keys[t][k] + ".png").c_str());
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
            engine_set_quality(e, 3);
            engine_remove_object(e, id);
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
        const size_t nRotas = std::getenv("ROTAS") ? size_t(std::atoi(std::getenv("ROTAS"))) : eng::favelaRotas().size();
        const int soCorre = std::getenv("CORRE") ? std::atoi(std::getenv("CORRE")) : -1;    // 0 andando, 1 correndo
        const int soSentido = std::getenv("SENTIDO") ? std::atoi(std::getenv("SENTIDO")) : -1;  // 0 subindo, 1 descendo
        for (int corre = 0; corre < 2; ++corre)
        for (size_t ri = 0; ri < std::min(nRotas, eng::favelaRotas().size()); ++ri) {
            const auto& rt = eng::favelaRotas()[ri];
            if (soCorre >= 0 && corre != soCorre) continue;
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
