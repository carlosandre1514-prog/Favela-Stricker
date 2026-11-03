#pragma once
// API C do Favela Stricker: o que o app Android (JNI), o site (WebAssembly) e os testes chamam do jogo.
// O motor tem a API dele (platform/engine_c_api.h); esta é só a do jogo.
#include "platform/engine_c_api.h"
#ifdef __cplusplus
extern "C" {
#endif

// Cria o motor e liga o jogo nele (no lugar de engine_create)
Engine* jogo_criar(const char* shader_dir);

#ifdef __cplusplus
}
#endif
