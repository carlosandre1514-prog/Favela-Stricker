cmake_minimum_required(VERSION 3.22)
project(FavelaStricker LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Obs.: exceções/RTTI ficam ligados por enquanto (tinygltf/Bullet). Desligar depois, com tudo funcionando.

include(FetchContent)
FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1)
FetchContent_Declare(tinygltf GIT_REPOSITORY https://github.com/syoyo/tinygltf.git GIT_TAG v2.9.3)
FetchContent_Declare(bullet GIT_REPOSITORY https://github.com/bulletphysics/bullet3.git GIT_TAG 3.25)
FetchContent_Declare(ufbx GIT_REPOSITORY https://github.com/ufbx/ufbx.git GIT_TAG v0.21.5)

set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_OPENGL3_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(USE_GRAPHICAL_BENCHMARK OFF CACHE BOOL "" FORCE)
set(INSTALL_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm tinygltf bullet)
FetchContent_GetProperties(ufbx)
if(NOT ufbx_POPULATED)
  FetchContent_Populate(ufbx)  # só os fontes (ufbx.c/ufbx.h), sem o CMake de testes deles
endif()

# Android: biblioteca do app (JNI) · navegador (Emscripten): estática, entra no favela.js/.wasm
if(EMSCRIPTEN)
  set(MOBILE3D_LIB_TYPE STATIC)
else()
  set(MOBILE3D_LIB_TYPE SHARED)
endif()
add_library(mobile3d ${MOBILE3D_LIB_TYPE}
  src/render/Renderer.cpp
  src/render/Particles.cpp
  src/render/Effects.cpp
  src/assets/GltfLoader.cpp
  src/assets/FbxLoader.cpp
  src/scene/Humanoid.cpp
  ${ufbx_SOURCE_DIR}/ufbx.c
  src/procgen/TextureGen.cpp
  src/procgen/Simplify.cpp
  src/procgen/Buildings.cpp
  src/procgen/Weapons.cpp
  src/procgen/Character.cpp
  src/procgen/Mocap.cpp
  src/engine/Engine.cpp
  src/engine/World.cpp
  src/engine/Project.cpp
  src/engine/Game.cpp
  src/engine/Weather.cpp
  src/engine/Export.cpp
  src/engine/AnimEdit.cpp
  src/engine/Planet.cpp
  src/platform/engine_c_api.cpp)

target_compile_definitions(mobile3d PUBLIC GLM_ENABLE_EXPERIMENTAL)
# Site: o cache de texturas é gerado no build pelo motor do PC; os dois usam a mesma chave
set(MOTOR_CACHE_KEY "" CACHE STRING "chave fixa do cache de texturas (vazio = muda a cada compilação)")
if(MOTOR_CACHE_KEY)
  target_compile_definitions(mobile3d PRIVATE MOTOR_CACHE_KEY="${MOTOR_CACHE_KEY}")
endif()
target_include_directories(mobile3d PUBLIC
  src
  ${glm_SOURCE_DIR}
  ${tinygltf_SOURCE_DIR}
  ${ufbx_SOURCE_DIR}
  ${bullet_SOURCE_DIR}/src)
target_link_libraries(mobile3d PUBLIC BulletDynamics BulletCollision LinearMath)

if(ANDROID)
  target_sources(mobile3d PRIVATE src/platform/android_jni.cpp)
  target_link_libraries(mobile3d PUBLIC GLESv3 EGL android log)
elseif(EMSCRIPTEN)
  # Versão site: WebGL 2 no navegador; os arquivos do jogo vão num pacote (favela.data) baixado junto
  add_executable(favela src/platform/web_main.cpp)
  target_link_libraries(favela PRIVATE mobile3d)
  set_target_properties(favela PROPERTIES SUFFIX ".js")
  target_link_options(favela PRIVATE
    -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=1048576
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8
    -sEXPORTED_FUNCTIONS=@${CMAKE_SOURCE_DIR}/web/exportadas.txt
    -sMODULARIZE=1 -sEXPORT_NAME=FavelaMotor -sENVIRONMENT=web
    --preload-file ${CMAKE_BINARY_DIR}/pacote@/jogo
    -O2)
else()
  # Build de desenvolvimento no PC/CI (Linux + Mesa): teste headless que renderiza as cenas em PNG
  target_link_libraries(mobile3d PUBLIC GLESv2 EGL)
  add_executable(mobile3d_headless tools/headless_test.cpp)
  target_link_libraries(mobile3d_headless PRIVATE mobile3d)
endif()
