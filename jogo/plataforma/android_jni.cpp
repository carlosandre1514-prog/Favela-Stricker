// Ponte Android do Favela Stricker: GLSurfaceView (thread de render) <-> motor (engine_c_api) e jogo (jogo_api).
// Só o que o app do jogo usa (sem editor).
// Todas as chamadas chegam na thread GL (o Kotlin usa queueEvent / onDrawFrame).
#include <EGL/egl.h>
#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <jni.h>
#include <signal.h>
#include <unistd.h>
#include <unwind.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "platform/engine_c_api.h"
#include "jogo/jogo_api.h"

static Engine* g_engine = nullptr;

// ---------- registro de travamento ----------
// Se o motor travar (sinal), grava o que estava fazendo e a pilha num arquivo; o app mostra na próxima abertura.
static char g_crashFile[512] = "";
static const char* volatile g_phase = "iniciando";
static struct sigaction g_oldActions[NSIG];

static void putStr(int fd, const char* s) { if (s) (void)!write(fd, s, std::strlen(s)); }
static void putNum(int fd, unsigned long v, int base) {
    char b[32]; int i = 31; b[i] = 0;
    do { b[--i] = "0123456789abcdef"[v % base]; v /= base; } while (v && i > 0);
    putStr(fd, b + i);
}
struct Frames { void* pc[24]; int n; };
static _Unwind_Reason_Code unwindOne(struct _Unwind_Context* ctx, void* arg) {
    Frames* f = static_cast<Frames*>(arg);
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc && f->n < 24) f->pc[f->n++] = reinterpret_cast<void*>(pc);
    return f->n < 24 ? _URC_NO_REASON : _URC_END_OF_STACK;
}
static void onCrash(int sig, siginfo_t* info, void* uctx) {
    int fd = g_crashFile[0] ? open(g_crashFile, O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
    if (fd >= 0) {
        putStr(fd, "Erro no motor (sinal "); putNum(fd, unsigned(sig), 10);
        putStr(fd, sig == SIGSEGV ? " SIGSEGV" : sig == SIGABRT ? " SIGABRT" : sig == SIGBUS ? " SIGBUS" : sig == SIGFPE ? " SIGFPE" : " SIGILL");
        putStr(fd, ") enquanto: "); putStr(fd, g_phase);
        putStr(fd, "\nendereço: 0x"); putNum(fd, reinterpret_cast<unsigned long>(info ? info->si_addr : nullptr), 16); putStr(fd, "\n");
        Frames f{{}, 0};
        _Unwind_Backtrace(unwindOne, &f);
        for (int i = 0; i < f.n; ++i) {
            Dl_info d{};
            putStr(fd, "  #"); putNum(fd, unsigned(i), 10); putStr(fd, " ");
            if (dladdr(f.pc[i], &d) && d.dli_fname) {
                const char* lib = std::strrchr(d.dli_fname, '/');
                putStr(fd, lib ? lib + 1 : d.dli_fname);
                putStr(fd, " +0x"); putNum(fd, reinterpret_cast<uintptr_t>(f.pc[i]) - reinterpret_cast<uintptr_t>(d.dli_fbase), 16);
                if (d.dli_sname) { putStr(fd, " "); putStr(fd, d.dli_sname); }
            } else { putStr(fd, "0x"); putNum(fd, reinterpret_cast<uintptr_t>(f.pc[i]), 16); }
            putStr(fd, "\n");
        }
        close(fd);
    }
    // segue para o tratamento normal do Android (fecha o app)
    sigaction(sig, &g_oldActions[sig], nullptr);
    raise(sig);
    (void)uctx;
}

#define JNI_FN(ret, name) JNIEXPORT ret JNICALL Java_com_mobile3d_NativeBridge_##name

static std::string str(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r = c ? c : "";
    env->ReleaseStringUTFChars(s, c);
    return r;
}

// Texto JSON vai e volta como bytes UTF-8 (o JNI usa UTF-8 "modificado", que quebra com emoji)
static std::string bytes(JNIEnv* env, jbyteArray a) {
    if (!a) return {};
    jsize n = env->GetArrayLength(a);
    std::string s(size_t(n), '\0');
    if (n) env->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(s.data()));
    return s;
}
static jbyteArray toBytes(JNIEnv* env, const char* s) {
    jsize n = jsize(s ? std::strlen(s) : 0);
    jbyteArray a = env->NewByteArray(n);
    if (n) env->SetByteArrayRegion(a, 0, n, reinterpret_cast<const jbyte*>(s));
    return a;
}

extern "C" {

JNI_FN(void, nativeSetCrashFile)(JNIEnv* env, jclass, jstring path) {
    std::string p = str(env, path);
    std::snprintf(g_crashFile, sizeof(g_crashFile), "%s", p.c_str());
    struct sigaction sa{};
    sa.sa_sigaction = onCrash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) sigaction(sig, &sa, &g_oldActions[sig]);
}

// onSurfaceCreated: contexto ES 3.2 ativo. Se já havia motor, o contexto antigo foi perdido: recria.
JNI_FN(jboolean, nativeCreate)(JNIEnv* env, jclass, jstring shaderDir) {
    g_phase = "abrindo o motor";
    if (g_engine) { engine_destroy(g_engine); g_engine = nullptr; }
    g_engine = jogo_criar(str(env, shaderDir).c_str());  // motor + o jogo ligado nele
    g_phase = "pronto";
    if (!g_engine) __android_log_print(ANDROID_LOG_ERROR, "Mobile3D", "engine_create falhou (shaders?)");
    return g_engine != nullptr;
}
JNI_FN(void, nativeDestroy)(JNIEnv*, jclass) { if (g_engine) { engine_destroy(g_engine); g_engine = nullptr; } }
JNI_FN(void, nativeResize)(JNIEnv*, jclass, jint w, jint h) { if (g_engine) engine_resize(g_engine, w, h); }
JNI_FN(void, nativeFrame)(JNIEnv*, jclass, jfloat dt) { if (g_engine) { g_phase = "desenhando a tela"; engine_frame(g_engine, dt); } }
JNI_FN(void, nativeSetQuality)(JNIEnv*, jclass, jint q) { if (g_engine) engine_set_quality(g_engine, q); }
// sons pedidos pelo jogo desde a última chamada ("nome\tvolume\tpan" por linha) e tempo (chuva, noite, hora)
JNI_FN(jstring, nativePollSounds)(JNIEnv* env, jclass) { return env->NewStringUTF(g_engine ? engine_poll_sounds(g_engine) : ""); }
JNI_FN(void, nativeGetWeather)(JNIEnv* env, jclass, jfloatArray out3) {
    float v[3] = {0, 0, 13};
    if (g_engine) engine_get_weather(g_engine, v);
    env->SetFloatArrayRegion(out3, 0, 3, v);
}

// Modo
JNI_FN(void, nativeSetPlay)(JNIEnv*, jclass, jboolean p) { if (g_engine) { g_phase = p ? "começando a partida" : "saindo da partida"; engine_set_play(g_engine, p ? 1 : 0); } }

JNI_FN(void, nativePlayerInput)(JNIEnv*, jclass, jfloat x, jfloat y, jboolean run) { if (g_engine) engine_player_input(g_engine, x, y, run ? 1 : 0); }
JNI_FN(void, nativePlayerJump)(JNIEnv*, jclass) { if (g_engine) engine_player_jump(g_engine); }
JNI_FN(void, nativePlayerCrouch)(JNIEnv*, jclass, jboolean on) { if (g_engine) engine_player_crouch(g_engine, on ? 1 : 0); }
JNI_FN(void, nativePlayerFire)(JNIEnv*, jclass) { if (g_engine) engine_player_fire(g_engine); }

JNI_FN(jboolean, nativeImportProject)(JNIEnv* env, jclass, jbyteArray json) {
    g_phase = "abrindo o mapa da partida";
    bool ok = g_engine && engine_import_project(g_engine, bytes(env, json).c_str());
    g_phase = "pronto";
    return ok;
}

JNI_FN(jboolean, nativeInteract)(JNIEnv*, jclass) { return g_engine && engine_interact(g_engine); }
JNI_FN(jbyteArray, nativeShopJson)(JNIEnv* env, jclass) { return toBytes(env, g_engine ? jogo_loja(g_engine) : ""); }
JNI_FN(jbyteArray, nativeShopBuy)(JNIEnv* env, jclass, jint item) { return toBytes(env, g_engine ? jogo_loja_comprar(g_engine, item) : ""); }
JNI_FN(void, nativeShopClose)(JNIEnv*, jclass) { if (g_engine) jogo_loja_fechar(g_engine); }
JNI_FN(void, nativePlayerAim)(JNIEnv*, jclass, jboolean on) { if (g_engine) engine_player_aim(g_engine, on ? 1 : 0); }
JNI_FN(jint, nativeScopeZoom)(JNIEnv*, jclass, jint d) { return g_engine ? engine_scope_zoom(g_engine, d) : 0; }
JNI_FN(void, nativeSetFirstPerson)(JNIEnv*, jclass, jboolean on) { if (g_engine) engine_set_first_person(g_engine, on ? 1 : 0); }
JNI_FN(void, nativeTeamLoadout)(JNIEnv* env, jclass, jint team, jint skin, jstring primary, jstring secondary) {
    if (!g_engine) return;
    g_phase = "escolhendo roupa e armas";
    const char* p = env->GetStringUTFChars(primary, nullptr);
    const char* q = env->GetStringUTFChars(secondary, nullptr);
    jogo_escolhas(g_engine, team, skin, p, q);
    env->ReleaseStringUTFChars(primary, p);
    env->ReleaseStringUTFChars(secondary, q);
}
JNI_FN(void, nativeTeamGunSkin)(JNIEnv* env, jclass, jstring gun, jstring skin) {
    if (!g_engine) return;
    const char* g = env->GetStringUTFChars(gun, nullptr);
    const char* s = env->GetStringUTFChars(skin, nullptr);
    jogo_skin_arma(g_engine, g, s);
    env->ReleaseStringUTFChars(gun, g);
    env->ReleaseStringUTFChars(skin, s);
}
JNI_FN(jboolean, nativePlayerSwap)(JNIEnv*, jclass) { return g_engine && jogo_trocar_arma(g_engine); }
JNI_FN(jboolean, nativePlayerGrenade)(JNIEnv*, jclass) { return g_engine && jogo_granada(g_engine); }
JNI_FN(void, nativePlayerReload)(JNIEnv*, jclass) { if (g_engine) engine_player_reload(g_engine); }
static std::string jstr(JNIEnv* env, jbyteArray a) {
    if (!a) return {};
    jsize n = env->GetArrayLength(a);
    std::string s(size_t(n), '\0');
    env->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(s.data()));
    return s;
}
JNI_FN(void, nativeNetRoster)(JNIEnv* env, jclass, jboolean host, jbyteArray id, jbyteArray roster) {
    if (g_engine) jogo_rede_sala(g_engine, host ? 1 : 0, jstr(env, id).c_str(), jstr(env, roster).c_str());
}
JNI_FN(jbyteArray, nativeNetCollect)(JNIEnv* env, jclass) { return toBytes(env, g_engine ? jogo_rede_coletar(g_engine) : "{}"); }
JNI_FN(void, nativeNetApply)(JNIEnv* env, jclass, jbyteArray s) { if (g_engine) jogo_rede_aplicar(g_engine, jstr(env, s).c_str()); }
JNI_FN(jbyteArray, nativeNetEvents)(JNIEnv* env, jclass) { return toBytes(env, g_engine ? jogo_rede_eventos(g_engine) : "[]"); }
JNI_FN(void, nativeNetEvent)(JNIEnv* env, jclass, jbyteArray s) { if (g_engine) jogo_rede_evento(g_engine, jstr(env, s).c_str()); }
JNI_FN(void, nativeLook)(JNIEnv*, jclass, jfloat dx, jfloat dy) { if (g_engine) engine_look(g_engine, dx, dy); }
JNI_FN(void, nativeSetCamera)(JNIEnv*, jclass, jfloat tx, jfloat ty, jfloat tz, jfloat yaw, jfloat pitch, jfloat dist) {
    if (g_engine) engine_set_camera(g_engine, tx, ty, tz, yaw, pitch, dist);
}
JNI_FN(jbyteArray, nativeHudJson)(JNIEnv* env, jclass) { return toBytes(env, g_engine ? jogo_hud(g_engine) : "{}"); }

}  // extern "C"
