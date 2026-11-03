cmake_minimum_required(VERSION 3.22)
project(FavelaStricker LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Navegador: o motor usa exceções (leitura de JSON e glTF), que o Emscripten desliga por padrão
if(EMSCRIPTEN)
  string(APPEND CMAKE_CXX_FLAGS " -fexceptions")
  string(APPEND CMAKE_C_FLAGS " -fexceptions")
endif()

# O motor: biblioteca à parte (motor/), que não sabe nada do jogo
add_subdirectory(motor)

# O jogo Favela Stricker (jogo/): usa o motor como um jogo usa o motor dele.
# Android: biblioteca do app (JNI) · navegador (Emscripten): estática, entra no favela.js/.wasm · PC: testes
if(EMSCRIPTEN)
  set(JOGO_LIB_TYPE STATIC)
else()
  set(JOGO_LIB_TYPE SHARED)
endif()
add_library(mobile3d ${JOGO_LIB_TYPE}
  jogo/Partida.cpp
  jogo/MapaFavela.cpp
  jogo/jogo_api.cpp)
target_include_directories(mobile3d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mobile3d PUBLIC motor)
# nada pode ficar sem dono (função chamada que não existe): o erro aparece na hora de compilar, não no celular
if(NOT EMSCRIPTEN AND NOT APPLE)
  target_link_options(mobile3d PRIVATE -Wl,--no-undefined)
endif()

if(ANDROID)
  target_sources(mobile3d PRIVATE jogo/plataforma/android_jni.cpp)
elseif(EMSCRIPTEN)
  # Versão site: WebGL 2 no navegador; os arquivos do jogo vão num pacote (favela.data) baixado junto
  add_executable(favela jogo/plataforma/web_main.cpp)
  target_link_libraries(favela PRIVATE mobile3d)
  set_target_properties(favela PROPERTIES SUFFIX ".js")
  target_link_options(favela PRIVATE
    -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=1048576
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8
    -sEXPORTED_FUNCTIONS=@${CMAKE_SOURCE_DIR}/web/exportadas.txt
    -sMODULARIZE=1 -sEXPORT_NAME=FavelaMotor -sENVIRONMENT=web -fexceptions -sMAXIMUM_MEMORY=4GB
    --preload-file ${CMAKE_BINARY_DIR}/pacote@/jogo
    -O2)
else()
  # PC/CI (Linux + Mesa): testes do jogo sem janela, desenhando em PNG
  add_executable(favela_testes tools/teste_jogo.cpp)
  target_link_libraries(favela_testes PRIVATE mobile3d)
endif()
