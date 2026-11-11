#pragma once
// API C do Favela Stricker: o que o app Android (JNI), o site (WebAssembly) e os testes chamam do jogo.
// O motor tem a API dele (platform/engine_c_api.h: tela, câmera, controles, Play); esta é só a do jogo.
// Os textos devolvidos ficam válidos até a próxima chamada que devolve texto.
#include "platform/engine_c_api.h"
#ifdef __cplusplus
extern "C" {
#endif

// Cria o motor e liga o jogo nele (no lugar de engine_create)
Engine* jogo_criar(const char* shader_dir);

// Antes do Play: lado (0 polícia, 1 bandidos), roupa (0..4 do lado), arma principal e segunda ("" = só uma)
void        jogo_escolhas(Engine*, int time, int roupa, const char* principal, const char* segunda);
void        jogo_modo(Engine*, int modo);                                   // 0 Polícia x Bandidos, 1 Ressurgência
void        jogo_skin_arma(Engine*, const char* arma, const char* skin);   // skin da arma do jogador ("" = padrão)
const char* jogo_aparencia_roupa(int time, int roupa);                      // aparência do boneco da roupa (JSON)
void        jogo_aquecimento(Engine*, float segundos);   // antes do Play: aquecimento (0 = sem); na partida: encurta (0 = já vale)
int         jogo_equipar(Engine*, const char* arma);     // no aquecimento: troca a arma principal (1 se trocou)

// Na partida
const char* jogo_hud(Engine*);                       // estado para o HUD (JSON)
int         jogo_trocar_arma(Engine*);               // troca rápida principal <-> segunda (1 se trocou)
int         jogo_granada(Engine*);                   // joga granada (1 se jogou)
const char* jogo_loja(Engine*);                      // loja aberta (JSON com itens) ou ""
const char* jogo_loja_comprar(Engine*, int item);    // compra/equipa; devolve a mensagem
void        jogo_loja_fechar(Engine*);

// Online: sala antes do Play ("" = offline), estado dos meus lutadores, dos outros e os eventos
void        jogo_rede_sala(Engine*, int host, const char* id_local, const char* sala_json);
const char* jogo_rede_coletar(Engine*);
void        jogo_rede_aplicar(Engine*, const char* estados_json);
const char* jogo_rede_eventos(Engine*);              // eventos a enviar (array JSON)
void        jogo_rede_evento(Engine*, const char* evento_json);

#ifdef __cplusplus
}
#endif
