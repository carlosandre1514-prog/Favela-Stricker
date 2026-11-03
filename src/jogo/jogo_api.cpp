// API C do Favela Stricker (ver jogo_api.h)
#include <string>
#include "jogo/jogo_api.h"
#include "engine/Engine.h"
#include "jogo/Partida.h"

struct Engine : eng::Engine {};

namespace {
std::string g_texto;  // texto devolvido (vale até a próxima chamada)
const char* texto(std::string s) { g_texto = std::move(s); return g_texto.c_str(); }
}

extern "C" {

Engine* jogo_criar(const char* shader_dir) {
    Engine* e = engine_create(shader_dir);
    if (e) eng::jogoInstalar(e);
    return e;
}

void jogo_escolhas(Engine* e, int time, int roupa, const char* principal, const char* segunda) {
    if (e) eng::teamLoadout(e, time, roupa, principal ? principal : "", segunda ? segunda : "");
}
void jogo_skin_arma(Engine* e, const char* arma, const char* skin) { if (e && arma) eng::teamGunSkin(e, arma, skin ? skin : ""); }
const char* jogo_aparencia_roupa(int time, int roupa) { return texto(eng::teamSkinLook(time, roupa)); }

const char* jogo_hud(Engine* e) { return texto(e ? eng::hudJson(e) : "{}"); }
int jogo_trocar_arma(Engine* e) { return e && eng::teamSwapWeapon(e) ? 1 : 0; }
int jogo_granada(Engine* e) { return e && eng::teamThrowGrenade(e) ? 1 : 0; }
const char* jogo_loja(Engine* e) { return texto(e ? eng::shopJson(e) : ""); }
const char* jogo_loja_comprar(Engine* e, int item) { return texto(e ? eng::shopBuy(e, item) : ""); }
void jogo_loja_fechar(Engine* e) { if (e) eng::shopClose(e); }

void jogo_rede_sala(Engine* e, int host, const char* id, const char* sala) { if (e) eng::netRoster(e, host != 0, id ? id : "", sala ? sala : ""); }
const char* jogo_rede_coletar(Engine* e) { return texto(e ? eng::netCollect(e) : "{}"); }
void jogo_rede_aplicar(Engine* e, const char* s) { if (e && s) eng::netApply(e, s); }
const char* jogo_rede_eventos(Engine* e) { return texto(e ? eng::netTakeEvents(e) : "[]"); }
void jogo_rede_evento(Engine* e, const char* s) { if (e && s) eng::netEvent(e, s); }

}  // extern "C"
