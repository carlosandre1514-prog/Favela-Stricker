// API C do Favela Stricker (ver jogo_api.h)
#include "jogo/jogo_api.h"
#include "engine/Engine.h"
#include "jogo/Partida.h"

struct Engine : eng::Engine {};

extern "C" {

Engine* jogo_criar(const char* shader_dir) {
    Engine* e = engine_create(shader_dir);
    if (e) eng::jogoInstalar(e);
    return e;
}

}  // extern "C"
