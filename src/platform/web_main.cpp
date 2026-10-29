// Versão site (Emscripten): o motor desenha no <canvas id="tela"> com WebGL 2 e a página (web/jogo.js) chama
// estas funções web_* (sem o ponteiro do motor: há um só). Os arquivos do jogo ficam em /jogo (pacote favela.data).
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstdio>
#include <string>
#include "platform/engine_c_api.h"

static Engine* g = nullptr;
static double g_last = 0.0;
static int g_w = 0, g_h = 0;

static void frame() {
    if (!g) return;
    double now = emscripten_get_now();
    float dt = g_last > 0.0 ? float((now - g_last) / 1000.0) : 1.f / 60.f;
    g_last = now;
    int w = 0, h = 0;
    emscripten_get_canvas_element_size("#tela", &w, &h);
    if (w != g_w || h != g_h) { g_w = w; g_h = h; engine_resize(g, w, h); }
    engine_frame(g, dt);
    EM_ASM({ if (Module.aposQuadro) Module.aposQuadro(); });
}

extern "C" {

// Cria o WebGL 2 e o motor; qualidade 0 (leve) a 2 (alta). 1 = deu certo.
EMSCRIPTEN_KEEPALIVE int web_iniciar(int quality) {
    EmscriptenWebGLContextAttributes a;
    emscripten_webgl_init_context_attributes(&a);
    a.majorVersion = 2;
    a.minorVersion = 0;
    a.alpha = false;
    a.depth = true;
    a.antialias = false;
    a.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#tela", &a);
    if (ctx <= 0) return 0;
    emscripten_webgl_make_context_current(ctx);
    // céu/reflexos em ponto flutuante e filtro anisotrópico
    emscripten_webgl_enable_extension(ctx, "EXT_color_buffer_float");
    emscripten_webgl_enable_extension(ctx, "EXT_color_buffer_half_float");
    emscripten_webgl_enable_extension(ctx, "OES_texture_float_linear");
    emscripten_webgl_enable_extension(ctx, "EXT_texture_filter_anisotropic");
    if (FILE* f = std::fopen("/jogo/qualidade.txt", "w")) { std::fprintf(f, "%d", quality); std::fclose(f); }
    g = engine_create("/jogo");
    if (!g) return 0;
    engine_set_quality(g, quality);
    emscripten_set_main_loop(frame, 0, 0);
    return 1;
}

EMSCRIPTEN_KEEPALIVE void web_qualidade(int q) { if (g) engine_set_quality(g, q); }
EMSCRIPTEN_KEEPALIVE int web_abrir_projeto(const char* json) { return g ? engine_import_project(g, json) : 0; }
EMSCRIPTEN_KEEPALIVE void web_selecionar(int id) { if (g) engine_select(g, id); }
EMSCRIPTEN_KEEPALIVE void web_sem_editor() { if (g) engine_set_editor_overlay(g, 0); }
EMSCRIPTEN_KEEPALIVE void web_play(int on) { if (g) engine_set_play(g, on); }
EMSCRIPTEN_KEEPALIVE void web_camera(float tx, float ty, float tz, float yaw, float pitch, float dist) { if (g) engine_set_camera(g, tx, ty, tz, yaw, pitch, dist); }
EMSCRIPTEN_KEEPALIVE void web_olhar(float dx, float dy) { if (g) engine_look(g, dx, dy); }
EMSCRIPTEN_KEEPALIVE void web_primeira_pessoa(int on) { if (g) engine_set_first_person(g, on); }
EMSCRIPTEN_KEEPALIVE void web_escolhas(int team, int skin, const char* primary, const char* secondary) { if (g) engine_team_loadout(g, team, skin, primary, secondary); }
EMSCRIPTEN_KEEPALIVE void web_skin_arma(const char* gun, const char* skin) { if (g) engine_team_gun_skin(g, gun, skin); }
EMSCRIPTEN_KEEPALIVE const char* web_hud() { return g ? engine_hud_json(g) : "{}"; }
EMSCRIPTEN_KEEPALIVE const char* web_sons() { return g ? engine_poll_sounds(g) : ""; }
EMSCRIPTEN_KEEPALIVE void web_andar(float x, float y, int run) { if (g) engine_player_input(g, x, y, run); }
EMSCRIPTEN_KEEPALIVE void web_pular() { if (g) engine_player_jump(g); }
EMSCRIPTEN_KEEPALIVE void web_agachar(int on) { if (g) engine_player_crouch(g, on); }
EMSCRIPTEN_KEEPALIVE void web_atirar() { if (g) engine_player_fire(g); }
EMSCRIPTEN_KEEPALIVE void web_mirar(int on) { if (g) engine_player_aim(g, on); }
EMSCRIPTEN_KEEPALIVE int web_trocar_arma() { return g ? engine_player_swap(g) : 0; }
EMSCRIPTEN_KEEPALIVE int web_granada() { return g ? engine_player_grenade(g) : 0; }
EMSCRIPTEN_KEEPALIVE void web_recarregar() { if (g) engine_player_reload(g); }
EMSCRIPTEN_KEEPALIVE int web_interagir() { return g ? engine_interact(g) : 0; }
// online (a página leva e traz os dados da sala)
EMSCRIPTEN_KEEPALIVE void web_rede_sala(int host, const char* id, const char* roster) { if (g) engine_net_roster(g, host, id, roster); }
EMSCRIPTEN_KEEPALIVE const char* web_rede_coletar() { return g ? engine_net_collect(g) : "{}"; }
EMSCRIPTEN_KEEPALIVE void web_rede_aplicar(const char* s) { if (g) engine_net_apply(g, s); }
EMSCRIPTEN_KEEPALIVE const char* web_rede_eventos() { return g ? engine_net_events(g) : "[]"; }
EMSCRIPTEN_KEEPALIVE void web_rede_evento(const char* s) { if (g) engine_net_event(g, s); }

}  // extern "C"

int main() { return 0; }  // a página chama web_iniciar quando o pacote de arquivos termina de baixar
