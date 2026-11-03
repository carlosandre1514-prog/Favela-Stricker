# Motor 3D (runtime): desenho (GLES 3 / WebGL 2), física (Bullet), cena, animação, modelos (glTF/FBX),
# peças procedurais e mapas cadastrados. Não tem editor nem nada de um jogo específico: o jogo liga o
# módulo dele (GameModule) e cadastra os mapas dele (engine/MapKit.h).
# CMake 4 recusa bibliotecas com cmake_minimum_required antigo (a Bullet pede < 3.5): aceita assim mesmo
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

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

add_library(motor STATIC
  render/Renderer.cpp
  render/Particles.cpp
  render/Effects.cpp
  assets/GltfLoader.cpp
  assets/FbxLoader.cpp
  scene/Humanoid.cpp
  ${ufbx_SOURCE_DIR}/ufbx.c
  procgen/TextureGen.cpp
  procgen/Simplify.cpp
  procgen/Buildings.cpp
  procgen/Weapons.cpp
  procgen/Character.cpp
  procgen/Mocap.cpp
  engine/Engine.cpp
  engine/World.cpp
  engine/Project.cpp
  engine/Game.cpp
  engine/Weather.cpp
  platform/engine_c_api.cpp)

target_compile_definitions(motor PUBLIC GLM_ENABLE_EXPERIMENTAL)
# Site: o cache de texturas é gerado no build pelo motor do PC; os dois usam a mesma chave
set(MOTOR_CACHE_KEY "" CACHE STRING "chave fixa do cache de texturas (vazio = muda a cada compilação)")
if(MOTOR_CACHE_KEY)
  target_compile_definitions(motor PRIVATE MOTOR_CACHE_KEY="${MOTOR_CACHE_KEY}")
endif()
target_include_directories(motor PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${glm_SOURCE_DIR}
  ${tinygltf_SOURCE_DIR}
  ${ufbx_SOURCE_DIR}
  ${bullet_SOURCE_DIR}/src)
target_link_libraries(motor PUBLIC BulletDynamics BulletCollision LinearMath)
if(ANDROID)
  target_link_libraries(motor PUBLIC GLESv3 EGL android log)
else()
  target_link_libraries(motor PUBLIC GLESv2 EGL)
endif()
