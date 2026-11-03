#pragma once
#include <stdint.h>
// API C estável: consumida via JNI (Android), dart:ffi (Flutter) e Swift/ObjC (iOS).
// Todas as funções devem ser chamadas na thread que tem o contexto GL (thread de render).
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Engine Engine;

Engine* engine_create(const char* shader_dir);                  // contexto GL já ativo na thread de render
void    engine_destroy(Engine*);
void    engine_resize(Engine*, int w, int h);
void    engine_frame(Engine*, float dt_seconds);                // update + render de um frame
void    engine_set_target_fbo(Engine*, uint32_t fbo);           // 0 = tela

// Modo
void    engine_set_play(Engine*, int playing);                  // 1 = Play (física/animação), 0 = Stop (restaura snapshot)
int     engine_is_playing(Engine*);

// Cenas
void    engine_new_scene(Engine*);                              // só chão + luz
const char* engine_last_error(Engine*);

int     engine_get_transform(Engine*, int id, float* pos3, float* euler3, float* scale3);  // 0 se id inválido
void    engine_set_transform(Engine*, int id, const float* pos3, const float* euler3, const float* scale3);

// Visual
void    engine_set_exposure(Engine*, float exposure);
float   engine_get_exposure(Engine*);
void    engine_set_sun_intensity(Engine*, float intensity);
float   engine_get_sun_intensity(Engine*);

void    engine_player_aim(Engine*, int on);                     // jogador mirando (botão segurado)
const char* engine_interact_label(Engine*);                     // texto do botão de ação perto do jogador ("" = nada)
int     engine_interact(Engine*);                               // usa (ex.: elevador para o terraço)

// Estatísticas: out[0] fps, [1] ms por frame, [2] ms de CPU do motor, [3] draw calls, [4] triângulos, [5] objetos
void    engine_get_stats(Engine*, float* out6);
void    engine_get_profile(Engine*, float* out3);
void    engine_memory_stats(Engine*, float* out4);           // texturas MB, malhas MB, nº de malhas
void    engine_set_quality(Engine*, int q);                  // nível 0 Leve, 1 Padrão, 2 Intermediária, 3 Ultra

int     engine_create_random_character(Engine*, uint32_t seed, int controllable);

// ---- Jogador (Play)
int     engine_has_player(Engine*);
void    engine_set_near(Engine*, float near_m);                 // plano próximo fixo (0 = automático)
int     engine_player_id(Engine*);                              // id do objeto do jogador (no Play pode ser a cópia em jogo) ou -1
void    engine_player_input(Engine*, float x, float y, int run); // joystick -1..1 (y para frente)
const char* engine_poll_sounds(Engine*);                        // sons pedidos: "nome\tvolume\tpan" por linha (e limpa a fila)
void    engine_get_weather(Engine*, float* out3);               // chuva 0..1, noite 0..1, hora
void    engine_player_jump(Engine*);
void    engine_player_crouch(Engine*, int on);                  // agachar (1) / levantar (0)
void    engine_player_fire(Engine*);
int     engine_player_weapon(Engine*);                          // tipo da arma ou -1
// ---- Jogo (app do jogo): primeira pessoa, escolhas antes do Play e HUD
void    engine_set_first_person(Engine*, int on);               // câmera no olho do jogador
void    engine_player_reload(Engine*);
void    engine_remove_object(Engine*, int id);                  // apaga o objeto (e o que estiver preso nele)
void    engine_look(Engine*, float dx, float dy);               // girar o olhar (pixels arrastados)
// ---- Online (sala do Firebase): o app leva e traz os dados; o motor só sabe quem é quem

// Câmera do editor: alvo, ângulos (radianos) e distância
void    engine_set_camera(Engine*, float tx, float ty, float tz, float yaw, float pitch, float distance);

int     engine_import_project(Engine*, const char* json);       // 0 se inválido (ver engine_last_error)
int     engine_add_spec(Engine*, const char* spec_json);        // cria um objeto pela receita JSON e seleciona
void    engine_set_time_of_day(Engine*, float hour);
float   engine_get_time_of_day(Engine*);

// ---- Jogo (Play): out[0] estado (0 jogando, 1 vitória, 2 derrota), [1] vida, [2] vida máx, [3] pontos,
// [4] tempo, [5] limite de tempo (0 = sem), [6] dano recente (>0 pisca), [7] tem objetivo (0/1)
void    engine_game_status(Engine*, float* out8);
const char* engine_game_text(Engine*, int which);               // 0 objetivo, 1 mensagem, 2 título, 3 descrição

#ifdef __cplusplus
}
#endif
