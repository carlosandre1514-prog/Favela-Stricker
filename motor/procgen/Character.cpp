#include "procgen/Character.h"
#include "procgen/Mocap.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include "procgen/Buildings.h"
#include "procgen/Noise.h"

namespace eng {

CharacterParams defaultCharacter() {
    CharacterParams p;
    p.skin = srgb(198, 140, 105);
    p.shirt = srgb(40, 110, 190);
    p.pants = srgb(50, 55, 70);
    p.shoes = srgb(240, 240, 240);
    p.hair = srgb(35, 25, 20);
    return p;
}

CharacterParams randomCharacter(uint32_t seed) {
    Rng r(seed);
    static const int skins[][3] = {{250, 214, 185}, {232, 180, 140}, {198, 140, 105}, {150, 98, 66}, {100, 64, 42}, {70, 46, 32}};
    static const int clothes[][3] = {{200, 40, 40}, {40, 110, 190}, {240, 240, 240}, {30, 30, 35}, {250, 200, 40}, {40, 150, 80},
                                     {240, 120, 30}, {120, 70, 160}, {90, 160, 220}, {230, 120, 160}};
    static const int pants[][3] = {{50, 55, 70}, {40, 60, 110}, {30, 30, 32}, {120, 100, 80}, {90, 90, 95}, {70, 90, 60}};
    static const int hairs[][3] = {{20, 16, 14}, {60, 40, 25}, {120, 80, 40}, {200, 170, 110}, {150, 60, 30}, {110, 110, 110}};
    CharacterParams p;
    auto pick = [&](const int (*a)[3], int n) { const int* c = a[r.irange(0, n - 1)]; return srgb(c[0], c[1], c[2]); };
    p.skin = pick(skins, 6);
    p.shirt = pick(clothes, 10);
    p.pants = pick(pants, 6);
    p.shoes = pick(clothes, 10);
    p.hair = pick(hairs, 6);
    p.height = r.range(1.6f, 1.9f);
    p.build = r.irange(0, 2);
    p.hairStyle = r.irange(0, 3);
    p.longSleeves = r.chance(0.3f);
    return p;
}

const char* weaponName(int k) {
    static const char* n[] = {"Pistola", "Fuzil", "Escopeta", "Sniper", "Faca", "Taco"};
    return k >= 0 && k < WK_Count ? n[k] : "Arma";
}

int armedClipBase(int kind) {
    if (kind == WK_Rifle || kind == WK_Shotgun || kind == WK_Sniper) return CC_Rifle;
    if (kind == WK_Pistol) return CC_Pistol;
    if (kind == WK_Knife || kind == WK_Bat) return CC_Melee;
    return -1;
}

glm::mat4 weaponGripOffset(int kind, float H) {
    // origem da arma = empunhadura, um pouco abaixo do pulso (dentro da mão fechada); cano para onde apontam os dedos
    float front = (kind == WK_Knife || kind == WK_Bat) ? 0.f : 0.012f * H;
    return glm::translate(glm::mat4(1.f), glm::vec3(0.f, -0.05f * H, front)) * glm::rotate(glm::mat4(1.f), glm::radians(90.f), glm::vec3(1, 0, 0));
}

namespace {

struct Joints { glm::vec3 pos[CJ_Total]; int parent[CJ_Total]; float H = 1.75f; };

using RotFn = std::function<glm::quat(float)>;    // fase 0..2pi -> rotação local
using PosFn = std::function<glm::vec3(float)>;

glm::quat rx(float a) { return glm::angleAxis(a, glm::vec3(1, 0, 0)); }
glm::quat ry(float a) { return glm::angleAxis(a, glm::vec3(0, 1, 0)); }
glm::quat rz(float a) { return glm::angleAxis(a, glm::vec3(0, 0, 1)); }

// rotação mínima que leva a direção a até b
glm::quat arc(glm::vec3 a, glm::vec3 b) {
    a = glm::normalize(a); b = glm::normalize(b);
    float d = glm::dot(a, b);
    if (d > 0.99999f) return glm::quat(1, 0, 0, 0);
    if (d < -0.99999f) {
        glm::vec3 ax = glm::cross(a, glm::vec3(1, 0, 0));
        if (glm::dot(ax, ax) < 1e-6f) ax = glm::cross(a, glm::vec3(0, 0, 1));
        return glm::angleAxis(3.14159265f, glm::normalize(ax));
    }
    glm::vec3 c = glm::cross(a, b);
    return glm::normalize(glm::quat(1.f + d, c.x, c.y, c.z));
}

struct ArmPose { glm::quat upper, fore, hand; };

// IK de 2 ossos: leva o pulso até `wrist` (espaço do modelo), cotovelo para o lado de `pole`, mão na rotação `handRot`.
// chest = rotação do peito (pai do braço) no espaço do modelo; as rotações devolvidas são locais (bind sem rotação).
ArmPose solveArm(const Joints& J, int up, int fo, int ha, glm::quat chest, glm::vec3 wrist, glm::vec3 pole, glm::quat handRot) {
    glm::vec3 pivot = J.pos[CJ_Chest];
    glm::vec3 S = pivot + chest * (J.pos[up] - pivot);
    float L1 = glm::length(J.pos[fo] - J.pos[up]), L2 = glm::length(J.pos[ha] - J.pos[fo]);
    glm::vec3 D = wrist - S;
    float dist = std::clamp(glm::length(D), 0.05f, (L1 + L2) * 0.999f);
    glm::vec3 u = glm::normalize(D);
    float ca = std::clamp((L1 * L1 + dist * dist - L2 * L2) / (2.f * L1 * dist), -1.f, 1.f);
    float a = std::acos(ca);
    glm::vec3 v = pole - glm::dot(pole, u) * u;
    v = glm::dot(v, v) > 1e-8f ? glm::normalize(v) : glm::normalize(glm::cross(u, glm::vec3(1, 0, 0)));
    glm::vec3 E = S + L1 * (std::cos(a) * u + std::sin(a) * v);
    glm::vec3 W = S + u * dist;
    glm::quat Mu = arc(chest * (J.pos[fo] - J.pos[up]), E - S) * chest;
    glm::quat Mf = arc(Mu * (J.pos[ha] - J.pos[fo]), W - E) * Mu;
    return {glm::normalize(glm::inverse(chest) * Mu), glm::normalize(glm::inverse(Mu) * Mf), glm::normalize(glm::inverse(Mf) * handRot)};
}

struct ClipWriter {
    LoadedModel& m;
    AnimationClip clip;
    float dur;
    size_t first;
    static constexpr int kKeys = 25;
    ClipWriter(LoadedModel& model, const char* name, float duration) : m(model), dur(duration) {
        std::snprintf(clip.name, sizeof(clip.name), "%s", name);
        clip.duration = duration;
        first = m.channelStorage.size();
    }
    void add(int joint, ChannelPath path, int comps, const std::function<void(float, float*)>& f) {
        m.keyData.emplace_back(kKeys + kKeys * comps);
        std::vector<float>& buf = m.keyData.back();
        for (int k = 0; k < kKeys; ++k) {
            float t = dur * k / (kKeys - 1);
            buf[k] = t;
            f(6.2831853f * k / (kKeys - 1), &buf[kKeys + k * comps]);
        }
        Channel c;
        c.joint = joint; c.path = path; c.keyCount = kKeys;
        c.times = buf.data(); c.values = buf.data() + kKeys;
        m.channelStorage.push_back(c);
    }
    void rot(int joint, const RotFn& f) {
        add(joint, ChannelPath::Rotation, 4, [&](float ph, float* o) { glm::quat q = glm::normalize(f(ph)); o[0] = q.x; o[1] = q.y; o[2] = q.z; o[3] = q.w; });
    }
    void pos(int joint, const PosFn& f) {
        add(joint, ChannelPath::Translation, 3, [&](float ph, float* o) { glm::vec3 v = f(ph); o[0] = v.x; o[1] = v.y; o[2] = v.z; });
    }
    void finish() {
        clip.channels = m.channelStorage.data() + first;
        clip.channelCount = int(m.channelStorage.size() - first);
        m.clips.push_back(clip);
    }
};

// Pernas e quadril de andar/correr (amp 0 = parado)
void legs(ClipWriter& w, const Joints& J, float legAmp, float knee, float bob) {
    float hipsY = J.pos[CJ_Hips].y;
    w.pos(CJ_Hips, [=](float ph) { return glm::vec3(0, hipsY - bob + bob * std::abs(std::cos(ph)), 0); });
    w.rot(CJ_Hips, [=](float ph) { return ry(0.08f * legAmp * std::sin(ph)); });
    w.rot(CJ_LThigh, [=](float ph) { return rx(-legAmp * std::sin(ph)); });
    w.rot(CJ_RThigh, [=](float ph) { return rx(legAmp * std::sin(ph)); });
    w.rot(CJ_LShin, [=](float ph) { return rx(0.08f + knee * std::max(0.f, std::cos(ph))); });
    w.rot(CJ_RShin, [=](float ph) { return rx(0.08f + knee * std::max(0.f, -std::cos(ph))); });
    w.rot(CJ_LFoot, [=](float ph) { return rx(-0.3f * knee * std::max(0.f, std::cos(ph))); });
    w.rot(CJ_RFoot, [=](float ph) { return rx(-0.3f * knee * std::max(0.f, -std::cos(ph))); });
}

// Pose de locomoção (andar / correr) sem arma
void locomotion(ClipWriter& w, const Joints& J, float legAmp, float knee, float armAmp, float elbow, float lean, float bob) {
    legs(w, J, legAmp, knee, bob);
    w.rot(CJ_Spine, [=](float) { return rx(lean); });
    w.rot(CJ_Chest, [=](float ph) { return ry(-0.12f * armAmp * std::sin(ph)) * rx(lean * 0.5f); });
    w.rot(CJ_LUpper, [=](float ph) { return rz(0.07f) * rx(armAmp * std::sin(ph)); });
    w.rot(CJ_RUpper, [=](float ph) { return rz(-0.07f) * rx(-armAmp * std::sin(ph)); });
    w.rot(CJ_LFore, [=](float ph) { return rx(-elbow - 0.15f * armAmp * std::max(0.f, std::sin(ph))); });
    w.rot(CJ_RFore, [=](float ph) { return rx(-elbow - 0.15f * armAmp * std::max(0.f, -std::sin(ph))); });
}

// Dedos: curl > 0 fecha a mão (polegar junto); trigger: indicador meio esticado (no gatilho)
void fingers(ClipWriter& w, bool left, float curl, bool trigger) {
    int b = left ? CJ_LThumb1 : CJ_RThumb1;
    float side = left ? 1.f : -1.f;
    w.rot(b, [=](float) { return rz(side * 0.5f * curl) * rx(-0.6f * curl); });
    w.rot(b + 1, [=](float) { return rx(-0.6f * curl); });
    for (int f = 1; f < 5; ++f) {
        float c = (trigger && f == 1) ? 0.35f : curl;
        w.rot(b + f * 2, [=](float) { return rx(-1.35f * c); });
        w.rot(b + f * 2 + 1, [=](float) { return rx(-1.2f * c); });
    }
}

// Poses com arma (IK): fuzil com coronha no ombro, pistola com as duas mãos, faca e taco
void armedClip(ClipWriter& w, const Joints& J, int cls, int mode) {
    const float H = J.H, k = H / 1.75f;
    bool aim = mode == AP_Aim || mode == AP_AimWalk;
    float legAmp = mode == AP_Walk || mode == AP_AimWalk ? 0.5f : mode == AP_Run ? 0.85f : 0.f;
    float knee = mode == AP_Run ? 1.4f : 0.75f, bob = mode == AP_Run ? 0.05f : legAmp > 0.f ? 0.025f : 0.f;
    if (legAmp > 0.f) legs(w, J, legAmp, knee, bob);
    else {
        float hipsY = J.pos[CJ_Hips].y;
        w.pos(CJ_Hips, [=](float ph) { return glm::vec3(0, hipsY - 0.004f + 0.004f * std::sin(ph), 0); });
        // base afastada (postura de tiro)
        w.rot(CJ_LThigh, [=](float) { return rz(0.06f) * rx(-0.12f); });
        w.rot(CJ_RThigh, [=](float) { return rz(-0.06f) * rx(0.1f); });
        w.rot(CJ_LShin, [=](float) { return rx(0.18f); });
        w.rot(CJ_RShin, [=](float) { return rx(0.12f); });
    }
    float lean = mode == AP_Run ? 0.18f : 0.06f;
    float twist = cls == CC_Melee ? 0.f : aim ? 0.28f : 0.18f;  // ombro direito para trás, esquerdo para frente
    w.rot(CJ_Spine, [=](float) { return rx(lean); });
    w.rot(CJ_Chest, [=](float) { return ry(-twist); });
    glm::quat chest = rx(lean) * ry(-twist);
    w.rot(CJ_Neck, [=](float) { return ry(twist * 0.6f); });  // a cabeça continua olhando para frente
    w.rot(CJ_Head, [=](float) { return ry(twist * 0.4f) * rx(aim ? 0.08f : 0.f); });

    const glm::vec3 fwd(0, 0, 1), up(0, 1, 0);
    glm::vec3 pivot = J.pos[CJ_Chest];
    auto shoulder = [&](int j) { return pivot + chest * (J.pos[j] - pivot); };
    // posição do pulso para que a empunhadura (offset da arma) caia em `grip`, com a mão girada em `hand`
    auto wristFor = [&](glm::vec3 grip, glm::quat hand) {
        return grip - hand * glm::vec3(0.f, -0.05f * H, 0.012f * H);
    };
    float bobSway = legAmp > 0.f ? 0.012f * k : 0.004f * k;
    std::function<void(float, ArmPose&, ArmPose&)> pose;
    if (cls == CC_Rifle) {
        pose = [=](float ph, ArmPose& R, ArmPose& L) {
            // cano: mirando = reto para frente; senão para baixo (posição de segurança)
            float pitch = aim ? 0.f : (mode == AP_Run ? -0.75f : -0.6f);
            glm::quat hand = rx(-1.5707963f - pitch);  // mão: dedos (-Y) para o cano, palma (+Z) para cima
            glm::vec3 dir = hand * glm::vec3(0, -1, 0);
            glm::vec3 pocket = shoulder(CJ_RUpper) + glm::vec3(0.07f * k, -0.04f * k, 0.03f * k);  // coronha no ombro
            glm::vec3 grip = pocket + dir * (0.30f * k) + glm::vec3(0, -0.03f * k + bobSway * std::sin(2 * ph), 0);
            R = solveArm(J, CJ_RUpper, CJ_RFore, CJ_RHand, chest, wristFor(grip, hand), glm::vec3(-1, -0.6f, -0.3f), hand);
            glm::quat lhand = hand * rz(-0.5f);
            glm::vec3 guard = grip + dir * (0.24f * k) - (hand * glm::vec3(0, 0, 1)) * (0.035f * k);
            L = solveArm(J, CJ_LUpper, CJ_LFore, CJ_LHand, chest, guard - lhand * glm::vec3(0, -0.035f * H, 0), glm::vec3(1, -1, 0), lhand);
        };
    } else if (cls == CC_Pistol) {
        pose = [=](float ph, ArmPose& R, ArmPose& L) {
            float pitch = aim ? 0.f : -0.8f;
            glm::quat hand = rx(-1.5707963f - pitch);
            glm::vec3 dir = hand * glm::vec3(0, -1, 0);
            glm::vec3 base = aim ? glm::vec3(-0.04f * k, J.pos[CJ_RUpper].y - 0.05f * k, 0.36f * k)
                                 : glm::vec3(-0.05f * k, J.pos[CJ_RUpper].y - 0.24f * k, 0.22f * k);
            glm::vec3 grip = base + glm::vec3(0, bobSway * std::sin(2 * ph), 0);
            R = solveArm(J, CJ_RUpper, CJ_RFore, CJ_RHand, chest, wristFor(grip, hand), glm::vec3(-1, -1, 0), hand);
            glm::quat lhand = hand * rz(-0.9f);  // mão de apoio por baixo/ao lado
            glm::vec3 lw = wristFor(grip, hand) + glm::vec3(0.06f * k, -0.03f * k, -0.01f * k) - dir * (0.01f * k);
            L = solveArm(J, CJ_LUpper, CJ_LFore, CJ_LHand, chest, lw, glm::vec3(1, -1, 0), lhand);
            (void)dir;
        };
    } else {
        pose = [=](float ph, ArmPose& R, ArmPose& L) {
            glm::quat hand;
            glm::vec3 grip;
            if (aim) {  // golpe: de cima/trás para frente/baixo
                float t = std::min(1.f, ph / 3.f);           // ida rápida na 1ª metade, volta depois
                float s = ph < 3.f ? t : 1.f - (ph - 3.f) / 3.2832f;
                float a = glm::mix(2.3f, -1.1f, std::clamp(s, 0.f, 1.f));
                hand = rx(-1.5707963f - a + 1.5707963f);
                grip = shoulder(CJ_RUpper) + glm::vec3(0.06f * k, -0.05f * k, 0.f) + (rx(-a) * glm::vec3(0, 0, 1)) * (0.36f * k) * 0.9f;
            } else {  // pronto: lâmina para frente na altura da cintura
                hand = rx(-1.25f);
                grip = glm::vec3(-0.1f * k, J.pos[CJ_Hips].y + 0.12f * k + bobSway * std::sin(2 * ph), 0.22f * k);
            }
            R = solveArm(J, CJ_RUpper, CJ_RFore, CJ_RHand, chest, wristFor(grip, hand), glm::vec3(-1, -1, -0.2f), hand);
            L = {rz(0.1f) * rx(legAmp * 0.6f * std::sin(ph)), rx(-0.2f - (legAmp > 0.f ? 0.4f : 0.f)), glm::quat(1, 0, 0, 0)};
        };
    }
    auto channel = [&](int joint, int which) {
        w.rot(joint, [=](float ph) {
            ArmPose R, L;
            pose(ph, R, L);
            const ArmPose& A = joint == CJ_RUpper || joint == CJ_RFore || joint == CJ_RHand ? R : L;
            return which == 0 ? A.upper : which == 1 ? A.fore : A.hand;
        });
    };
    channel(CJ_RUpper, 0); channel(CJ_RFore, 1); channel(CJ_RHand, 2);
    channel(CJ_LUpper, 0); channel(CJ_LFore, 1); channel(CJ_LHand, 2);
    fingers(w, false, 1.f, cls != CC_Melee);   // direita: empunhadura, indicador no gatilho
    fingers(w, true, cls == CC_Melee ? 0.3f : 0.85f, false);
}

void idle(ClipWriter& w, const Joints& J) {
    float hipsY = J.pos[CJ_Hips].y;
    w.pos(CJ_Hips, [=](float ph) { return glm::vec3(0, hipsY - 0.004f + 0.004f * std::sin(ph), 0); });
    w.rot(CJ_Chest, [=](float ph) { return rx(0.025f * std::sin(ph)); });
    w.rot(CJ_Neck, [=](float ph) { return ry(0.08f * std::sin(ph)); });
    w.rot(CJ_Head, [=](float ph) { return ry(0.1f * std::sin(ph)) * rx(0.03f * std::sin(2 * ph)); });
    w.rot(CJ_LUpper, [=](float ph) { return rz(0.1f + 0.02f * std::sin(ph)); });
    w.rot(CJ_RUpper, [=](float ph) { return rz(-0.1f - 0.02f * std::sin(ph)); });
    w.rot(CJ_LFore, [=](float) { return rx(-0.12f); });
    w.rot(CJ_RFore, [=](float) { return rx(-0.12f); });
    fingers(w, false, 0.25f, false);
    fingers(w, true, 0.25f, false);
}
void dance(ClipWriter& w, const Joints& J) {
    float hipsY = J.pos[CJ_Hips].y;
    w.pos(CJ_Hips, [=](float ph) { return glm::vec3(0.04f * std::sin(ph), hipsY - 0.05f + 0.05f * std::abs(std::cos(ph)), 0); });
    w.rot(CJ_Hips, [=](float ph) { return rz(0.15f * std::sin(ph)) * ry(0.25f * std::sin(ph * 0.5f)); });
    w.rot(CJ_Chest, [=](float ph) { return rz(-0.2f * std::sin(ph)); });
    w.rot(CJ_Head, [=](float ph) { return rz(0.15f * std::sin(ph + 0.5f)); });
    w.rot(CJ_LUpper, [=](float ph) { return rz(2.3f + 0.35f * std::sin(ph)); });
    w.rot(CJ_RUpper, [=](float ph) { return rz(-2.3f + 0.35f * std::sin(ph)); });
    w.rot(CJ_LFore, [=](float ph) { return rz(0.5f + 0.4f * std::sin(2 * ph)); });
    w.rot(CJ_RFore, [=](float ph) { return rz(-0.5f + 0.4f * std::sin(2 * ph)); });
    w.rot(CJ_LThigh, [=](float ph) { return rx(-0.25f - 0.2f * std::max(0.f, std::sin(ph))); });
    w.rot(CJ_RThigh, [=](float ph) { return rx(-0.25f - 0.2f * std::max(0.f, -std::sin(ph))); });
    w.rot(CJ_LShin, [=](float ph) { return rx(0.45f + 0.35f * std::max(0.f, std::sin(ph))); });
    w.rot(CJ_RShin, [=](float ph) { return rx(0.45f + 0.35f * std::max(0.f, -std::sin(ph))); });
    w.rot(CJ_LFoot, [=](float) { return rx(-0.2f); });
    w.rot(CJ_RFoot, [=](float) { return rx(-0.2f); });
}

void wave(ClipWriter& w, const Joints& J) {
    float hipsY = J.pos[CJ_Hips].y;
    w.pos(CJ_Hips, [=](float ph) { return glm::vec3(0, hipsY - 0.004f + 0.004f * std::sin(ph), 0); });
    w.rot(CJ_RUpper, [=](float) { return rz(-2.65f); });
    w.rot(CJ_RFore, [=](float ph) { return rz(-0.25f + 0.55f * std::sin(2 * ph)); });
    w.rot(CJ_LUpper, [=](float) { return rz(0.1f); });
    w.rot(CJ_Head, [=](float ph) { return rz(-0.08f + 0.04f * std::sin(ph)); });
    w.rot(CJ_Chest, [=](float ph) { return rz(-0.04f * std::sin(ph)); });
}

}  // namespace


// Corpo em blocos com skin pintada: cada parte do molde é uma caixa; partes com mais de um bloco (tronco, braço,
// perna) dividem as laterais da imagem pela altura de cada bloco. Topo e base só nos blocos das pontas; entre
// dois blocos a tampa usa a linha da divisão (fica da cor da roupa ali quando a junta dobra).
static void buildTexturedBlockBody(const CharacterParams& p, MeshBuilder& b, float H, float sx, float tx, float shoulderY,
                                   float elbowY, float wristY, float thighY, float kneeY, float ankleY) {
    b.color = glm::vec4(1.f);
    // seg: parte do molde, osso, centro x/z, largura, profundidade, de yTop a yBot (metros), fração da parte [fa, fb]
    auto seg = [&](int part, int joint, float x, float z, float w, float d, float yTop, float yBot, float fa, float fb) {
        const glm::vec4* m = p.molde[part];
        glm::vec4 r[6];
        for (int f = 0; f < 4; ++f) {
            const glm::vec4 q = m[f];
            r[f] = {q.x, q.y + (q.w - q.y) * fa, q.z, q.y + (q.w - q.y) * fb};
        }
        const glm::vec4 fr = m[0];
        const float va = fr.y + (fr.w - fr.y) * fa, vb = fr.y + (fr.w - fr.y) * fb;
        r[4] = fa <= 0.f ? m[4] : glm::vec4(fr.x, va, fr.z, va);
        r[5] = fb >= 1.f ? m[5] : glm::vec4(fr.x, vb, fr.z, vb);
        b.joint = joint;
        b.faceRects = r;
        b.box({x - w * 0.5f, yBot, z - d * 0.5f}, {w, yTop - yBot, d});
        b.faceRects = nullptr;
    };
    const float tw = (sx - 0.05f * H) * 2.f, td = 0.12f * H;
    // tronco: peito, barriga, cintura (0.84H .. 0.5H)
    const float t0 = 0.84f * H, t1 = 0.68f * H, t2 = 0.6f * H, t3 = thighY, tl = t0 - t3;
    seg(SP_Tronco, CJ_Chest, 0, 0, tw, td, t0, t1, 0.f, (t0 - t1) / tl);
    seg(SP_Tronco, CJ_Spine, 0, 0, tw, td, t1, t2, (t0 - t1) / tl, (t0 - t2) / tl);
    seg(SP_Tronco, CJ_Hips, 0, 0, tw, td, t2, t3, (t0 - t2) / tl, 1.f);
    // cabeça e pescoço (o pescoço usa a base da cabeça)
    const float hs = 0.15f * H, hy = 0.915f * H;
    seg(SP_Cabeca, CJ_Head, 0, 0, hs, 0.14f * H, hy + hs / 2, hy - hs / 2, 0.f, 1.f);
    {
        glm::vec4 r[6];
        const glm::vec4 bs = p.molde[SP_Cabeca][5];
        const glm::vec4 c = {(bs.x + bs.z) * 0.5f, (bs.y + bs.w) * 0.5f, (bs.x + bs.z) * 0.5f, (bs.y + bs.w) * 0.5f};
        for (auto& q : r) q = c;
        b.joint = CJ_Neck; b.faceRects = r;
        b.boxC({0, 0.842f * H, 0}, {0.07f * H, 0.02f * H, 0.07f * H});
        b.faceRects = nullptr;
    }
    // braços: ombro, antebraço, mão (+X é o lado esquerdo do boneco)
    const float aw = 0.085f * H, a0 = shoulderY + 0.015f * H, a3 = wristY - 0.07f * H, al = a0 - a3;
    for (int side = 0; side < 2; ++side) {
        const float x = side ? -sx : sx;
        const int part = side ? SP_BracoDir : SP_BracoEsq;
        seg(part, side ? CJ_RUpper : CJ_LUpper, x, 0, aw, aw, a0, elbowY, 0.f, (a0 - elbowY) / al);
        seg(part, side ? CJ_RFore : CJ_LFore, x, 0, aw, aw, elbowY, wristY, (a0 - elbowY) / al, (a0 - wristY) / al);
        seg(part, side ? CJ_RHand : CJ_LHand, x, 0, aw, aw, wristY, a3, (a0 - wristY) / al, 1.f);
    }
    // pernas: coxa e canela; pé separado
    const float lw = 0.115f * H, ld = td * 0.95f, l0 = thighY + 0.005f * H, l1 = kneeY - 0.005f * H, l2 = ankleY + 0.005f * H;
    for (int side = 0; side < 2; ++side) {
        const float x = side ? -tx : tx;
        const int part = side ? SP_PernaDir : SP_PernaEsq;
        seg(part, side ? CJ_RThigh : CJ_LThigh, x, 0, lw, ld, l0, l1, 0.f, (l0 - l1) / (l0 - l2));
        seg(part, side ? CJ_RShin : CJ_LShin, x, 0, lw, ld, l1, l2, (l0 - l1) / (l0 - l2), 1.f);
        seg(side ? SP_PeDir : SP_PeEsq, side ? CJ_RFoot : CJ_LFoot, x, 0.018f * H, lw * 0.98f, td * 1.15f, 0.06f * H, 0.f, 0.f, 1.f);
    }
}

// Corpo em blocos, parecido com os bonecos do Roblox (R15): cabeça quadrada com rostinho (olhos e sorriso),
// tronco quadrado, braço (ombro, antebraço, mão) e perna (coxa, canela, pé) em blocos presos a um osso cada.
// Roupa: cores de camisa/calça/tênis, estampas da camisa, colete, chapéu e luvas (CharacterParams).
static void buildBlockBody(const CharacterParams& p, MeshBuilder& b, float H, float sx, float tx, float shoulderY, float elbowY,
                    float wristY, float thighY, float kneeY, float ankleY) {
    auto part = [&](int joint, glm::vec4 col, glm::vec3 center, glm::vec3 size) {
        b.joint = joint; b.color = col; b.boxC(center, size);
    };
    const glm::vec4 preto = srgb(22, 22, 26);
    const float tw = (sx - 0.05f * H) * 2.f, td = 0.12f * H;    // tronco: largura e profundidade
    const float waist = thighY, chestTop = 0.84f * H;
    // ---- tronco: cintura (calça), barriga e peito (camisa)
    // cintura num tom mais escuro que as pernas (dá para ver onde o tronco acaba e as pernas começam) e cinto em cima
    // (calça muito escura: a cintura fica um pouco mais clara, senão não aparece a diferença)
    const float lum = 0.2126f * p.pants.r + 0.7152f * p.pants.g + 0.0722f * p.pants.b;  // cores lineares
    const glm::vec4 cintura = lum < 0.02f ? glm::vec4(glm::vec3(p.pants) + glm::vec3(0.035f), 1.f)
                                          : glm::vec4(glm::vec3(p.pants) * 0.68f, 1.f);
    part(CJ_Hips, cintura, {0, (waist + 0.6f * H) * 0.5f, 0}, {tw, 0.6f * H - waist + 0.002f * H, td});
    {
        const float ec = 0.003f * H;
        part(CJ_Hips, srgb(30, 25, 22), {0, 0.59f * H, 0}, {tw + 2.f * ec, 0.022f * H, td + 2.f * ec});                 // cinto
        part(CJ_Hips, srgb(190, 170, 110), {0, 0.59f * H, td / 2 + 1.5f * ec}, {0.03f * H, 0.016f * H, 0.004f * H});  // fivela
    }
    part(CJ_Spine, p.shirt, {0, 0.64f * H, 0}, {tw, 0.08f * H, td});
    part(CJ_Chest, p.shirt, {0, (0.68f * H + chestTop) * 0.5f, 0}, {tw, chestTop - 0.68f * H, td});
    // estampas da camisa (um pouco por fora do tronco, para não piscar)
    const float e = 0.004f * H, y0 = 0.6f * H, y1 = chestTop;
    auto faixa = [&](int joint, glm::vec4 col, float ya, float yb, float xa, float xb, bool todaVolta) {
        part(joint, col, {(xa + xb) * 0.5f, (ya + yb) * 0.5f, 0}, {xb - xa + (todaVolta ? 2.f * e : 0.f), yb - ya, td + 2.f * e});
    };
    if (p.estampa == 1) {  // listras deitadas (dão a volta na camisa)
        for (float y = y0 + 0.03f * H; y + 0.03f * H <= y1; y += 0.06f * H) faixa(y < 0.68f * H ? CJ_Spine : CJ_Chest, p.cor2, y, y + 0.03f * H, -tw / 2, tw / 2, true);
    } else if (p.estampa == 2) {  // listras em pé (frente e costas)
        for (float x = -tw / 2 + 0.03f * H; x + 0.03f * H <= tw / 2 + 1e-4f; x += 0.06f * H) {
            faixa(CJ_Spine, p.cor2, y0, 0.68f * H, x, x + 0.03f * H, false);
            faixa(CJ_Chest, p.cor2, 0.68f * H, y1, x, x + 0.03f * H, false);
        }
    } else if (p.estampa == 4) {  // três cores em pé: camisa | cor2 | cor3
        for (int k = 1; k < 3; ++k) {
            float xa = -tw / 2 + tw * k / 3.f, xb = xa + tw / 3.f;
            faixa(CJ_Spine, k == 1 ? p.cor2 : p.cor3, y0, 0.68f * H, xa, xb, k == 2);
            faixa(CJ_Chest, k == 1 ? p.cor2 : p.cor3, 0.68f * H, y1, xa, xb, k == 2);
        }
    } else if (p.estampa == 3) {  // faixa na diagonal, do ombro esquerdo até o quadril direito (frente e costas)
        for (float lado : {1.f, -1.f}) {
            b.xf = glm::translate(glm::mat4(1.f), glm::vec3(0, (y0 + y1) * 0.5f, lado * (td / 2 + e * 0.5f))) *
                   glm::rotate(glm::mat4(1.f), lado * 0.75f, glm::vec3(0, 0, 1));
            part(CJ_Chest, p.cor2, {0, 0, 0}, {0.055f * H, (y1 - y0) * 1.25f, e});
            b.xf = glm::mat4(1.f);
        }
    } else if (p.estampa == 5) {  // gola
        part(CJ_Chest, p.cor2, {0, chestTop - 0.012f * H, 0}, {0.1f * H, 0.024f * H, td + 2.f * e});
    }
    // (o colete é uma peça separada, que veste o boneco por cima: buildBlockVest)
    // ---- cabeça: bloco com rostinho
    const float hs = 0.15f * H, hy = 0.915f * H, hf = 0.07f * H;  // tamanho, centro e frente
    const bool touca = p.chapeu == 3;
    part(CJ_Neck, p.skin, {0, 0.842f * H, 0}, {0.07f * H, 0.02f * H, 0.07f * H});
    part(CJ_Head, touca ? p.chapeuCor : p.skin, {0, hy, 0}, {hs, hs, 0.14f * H});
    if (touca) part(CJ_Head, p.skin, {0, hy + 0.012f * H, hf + 0.0015f * H}, {0.115f * H, 0.04f * H, 0.003f * H});  // abertura dos olhos
    for (float x : {-0.032f, 0.032f})
        part(CJ_Head, preto, {x * H, hy + 0.012f * H, hf + 0.003f * H}, {0.018f * H, 0.03f * H, 0.004f * H});  // olhos
    if (!touca) {  // sorriso
        part(CJ_Head, preto, {0, hy - 0.035f * H, hf + 0.003f * H}, {0.04f * H, 0.008f * H, 0.004f * H});
        for (float x : {-0.024f, 0.024f}) part(CJ_Head, preto, {x * H, hy - 0.028f * H, hf + 0.003f * H}, {0.01f * H, 0.012f * H, 0.004f * H});
    }
    const float topo = hy + hs / 2;
    if (p.chapeu == 0 && !touca && p.hairStyle > 0) {  // cabelo: tampa em cima e atrás
        part(CJ_Head, p.hair, {0, topo + 0.008f * H, -0.006f * H}, {hs + 0.008f * H, 0.022f * H, 0.152f * H});
        part(CJ_Head, p.hair, {0, hy + 0.02f * H, -0.072f * H}, {hs + 0.008f * H, 0.1f * H, 0.012f * H});
    } else if (p.chapeu == 1) {  // boné com aba
        part(CJ_Head, p.chapeuCor, {0, topo + 0.012f * H, -0.004f * H}, {hs + 0.01f * H, 0.035f * H, 0.15f * H});
        part(CJ_Head, p.chapeuCor * 0.85f, {0, topo - 0.002f * H, hf + 0.035f * H}, {hs * 0.85f, 0.01f * H, 0.07f * H});
    } else if (p.chapeu == 2 || p.chapeu == 4) {  // capacete (e viseira escura no choque)
        part(CJ_Head, p.chapeuCor, {0, topo - 0.015f * H, -0.004f * H}, {hs + 0.016f * H, 0.075f * H, 0.155f * H});
        if (p.chapeu == 4) part(CJ_Head, srgb(30, 36, 48), {0, hy + 0.012f * H, hf + 0.01f * H}, {hs + 0.01f * H, 0.05f * H, 0.012f * H});
    }
    // ---- braços: ombro (camisa), antebraço (camisa ou pele), mão (pele ou luva)
    const glm::vec4 mao = p.luvas.a > 0.f ? p.luvas : p.skin;
    const float aw = 0.085f * H;
    for (int side = 0; side < 2; ++side) {
        float x = side ? -sx : sx;
        int up = side ? CJ_RUpper : CJ_LUpper, fo = side ? CJ_RFore : CJ_LFore, ha = side ? CJ_RHand : CJ_LHand;
        part(up, p.shirt, {x, (shoulderY + 0.015f * H + elbowY) * 0.5f, 0}, {aw, shoulderY + 0.015f * H - elbowY, aw});
        if (p.estampa == 5) part(up, p.cor2, {x, elbowY + 0.008f * H, 0}, {aw + 2.f * e, 0.016f * H, aw + 2.f * e});  // punho da manga
        part(fo, p.longSleeves ? p.shirt : p.skin, {x, (elbowY + wristY) * 0.5f, 0}, {aw * 0.96f, elbowY - wristY, aw * 0.96f});
        part(ha, mao, {x, wristY - 0.035f * H, 0.004f * H}, {aw * 0.92f, 0.07f * H, aw * 0.8f});
    }
    // ---- pernas: coxa e canela (calça ou bermuda), pé (tênis)
    const float lw = 0.115f * H;
    for (int side = 0; side < 2; ++side) {
        float x = side ? -tx : tx;
        int th = side ? CJ_RThigh : CJ_LThigh, sh = side ? CJ_RShin : CJ_LShin, ft = side ? CJ_RFoot : CJ_LFoot;
        part(th, p.pants, {x, (thighY + kneeY) * 0.5f, 0}, {lw, thighY - kneeY + 0.01f * H, td * 0.95f});
        part(sh, p.bermuda ? p.skin : p.pants, {x, (kneeY + ankleY) * 0.5f + 0.01f * H, 0}, {lw * 0.94f, kneeY - ankleY + 0.01f * H, td * 0.9f});
        part(ft, p.shoes, {x, 0.03f * H, 0.018f * H}, {lw * 0.98f, 0.06f * H, td * 1.15f});
    }
}

// Colete tático do boneco em blocos, como peça separada do corpo (veste por cima da roupa ou da skin pintada).
// Malha no espaço do personagem (pés na origem, frente +Z); quem usa prende no osso do peito (attachGearMesh).
// Placas da frente e de trás, faixa da cintura, alças nos ombros e porta-carregadores na frente.
void buildBlockVest(const CharacterParams& p, MeshBuilder& b) {
    const float H = p.height;
    const float hw = 0.125f * H, hd = 0.066f * H;            // por fora do tronco (0,12H x 0,06H)
    const float y0 = 0.6f * H, y1 = 0.83f * H;               // da barriga até perto dos ombros
    glm::vec4 cor = p.coleteCor.a > 0.f ? p.coleteCor : srgb(32, 34, 38);
    cor.a = 1.f;
    const glm::vec4 escuro(cor.r * 0.7f, cor.g * 0.7f, cor.b * 0.7f, 1.f), alca(cor.r * 0.85f, cor.g * 0.85f, cor.b * 0.85f, 1.f);
    b.joint = -1;
    b.xf = glm::mat4(1.f);
    b.uvTile = glm::vec2(0.f);
    auto bx = [&](glm::vec4 c, glm::vec3 mn, glm::vec3 mx) { b.color = c; b.box(mn, mx - mn); };
    // placas e laterais (a lateral é mais baixa: o braço passa por cima)
    bx(cor, {-hw, y0, hd - 0.012f * H}, {hw, y1, hd});                       // frente
    bx(cor, {-hw, y0, -hd}, {hw, y1, -hd + 0.012f * H});                     // costas
    for (float s : {-1.f, 1.f}) bx(escuro, {s > 0 ? hw - 0.01f * H : -hw, y0, -hd}, {s > 0 ? hw : -hw + 0.01f * H, 0.72f * H, hd});
    bx(escuro, {-hw - 0.003f * H, y0, -hd - 0.003f * H}, {hw + 0.003f * H, y0 + 0.04f * H, hd + 0.003f * H});  // faixa da cintura
    // alças: por cima dos ombros, da frente até as costas
    for (float s : {-1.f, 1.f}) {
        const float xa = s * 0.055f * H - 0.022f * H, xb = s * 0.055f * H + 0.022f * H;
        bx(alca, {xa, y1 - 0.004f * H, -hd}, {xb, y1 + 0.008f * H, hd});
    }
    // porta-carregadores na frente (3) e um painel nas costas
    for (float x : {-0.07f, 0.f, 0.07f})
        bx(escuro, {(x - 0.028f) * H, 0.625f * H, hd}, {(x + 0.028f) * H, 0.69f * H, hd + 0.018f * H});
    bx(escuro, {-0.08f * H, 0.7f * H, -hd - 0.01f * H}, {0.08f * H, 0.79f * H, -hd});
    b.color = glm::vec4(1.f);
}

// ---------------- cabelo 3D ----------------
// Formas do cabelo: tubos ao longo de uma curva (espirais dos cachos, mechas dos dreads), touca arredondada
// (superelipsoide, abraça a cabeça quadrada) e bolinhas. Cada triângulo sai virado para fora (confere a normal).
namespace {
struct HairMesh {
    MeshBuilder& b;
    void tri(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 n0, glm::vec3 n1, glm::vec3 n2, glm::vec2 t0, glm::vec2 t1, glm::vec2 t2) {
        if (glm::dot(glm::cross(p1 - p0, p2 - p0), n0 + n1 + n2) < 0.f) { std::swap(p1, p2); std::swap(n1, n2); std::swap(t1, t2); }
        const glm::vec3 tg = glm::length(p1 - p0) > 1e-7f ? glm::normalize(p1 - p0) : glm::vec3(1, 0, 0);
        const uint32_t i = uint32_t(b.v.size());
        b.pushVertex(p0, n0, t0, tg); b.pushVertex(p1, n1, t1, tg); b.pushVertex(p2, n2, t2, tg);
        b.idx.push_back(i); b.idx.push_back(i + 1); b.idx.push_back(i + 2);
    }
    static glm::vec2 uvIn(glm::vec4 r, float u, float v) { return {r.x + (r.z - r.x) * u, r.y + (r.w - r.y) * v}; }
    // Tubo pela curva P com raio R em cada ponto (n lados); a ponta de cima é tampada
    void tube(const std::vector<glm::vec3>& P, const std::vector<float>& R, int n, glm::vec4 uvr) {
        const size_t N = P.size();
        if (N < 2) return;
        std::vector<glm::vec3> T(N), Nn(N), B(N);
        for (size_t i = 0; i < N; ++i) T[i] = glm::normalize(P[std::min(i + 1, N - 1)] - P[i > 0 ? i - 1 : 0]);
        glm::vec3 ref = std::abs(T[0].y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        Nn[0] = glm::normalize(glm::cross(ref, T[0]));
        for (size_t i = 1; i < N; ++i) {  // transporte paralelo (o tubo não torce)
            glm::vec3 q = Nn[i - 1] - T[i] * glm::dot(Nn[i - 1], T[i]);
            Nn[i] = glm::length(q) > 1e-6f ? glm::normalize(q) : Nn[i - 1];
        }
        for (size_t i = 0; i < N; ++i) B[i] = glm::cross(T[i], Nn[i]);
        auto ring = [&](size_t i, int k, glm::vec3& pos, glm::vec3& nor) {
            const float a = 6.2831853f * float(k) / float(n);
            nor = Nn[i] * std::cos(a) + B[i] * std::sin(a);
            pos = P[i] + nor * R[i];
        };
        // anéis com vértices compartilhados (malha indexada: bem menos memória que um vértice por canto de triângulo)
        const uint32_t v0i = uint32_t(b.v.size());
        for (size_t i = 0; i < N; ++i)
            for (int k = 0; k <= n; ++k) {
                glm::vec3 pos, nor;
                ring(i, k, pos, nor);
                b.pushVertex(pos, nor, uvIn(uvr, float(k) / n, float(i) / (N - 1)), T[i]);
            }
        // sentido dos triângulos: confere uma vez (todos os anéis seguem a mesma orientação)
        glm::vec3 a0, a1, b0, na;
        ring(0, 0, a0, na); ring(0, 1, a1, na); ring(1, 0, b0, na);
        const bool inv = glm::dot(glm::cross(a1 - a0, b0 - a0), na) < 0.f;
        auto quadIdx = [&](uint32_t x, uint32_t y, uint32_t z) {
            b.idx.push_back(x);
            b.idx.push_back(inv ? z : y);
            b.idx.push_back(inv ? y : z);
        };
        const uint32_t W = uint32_t(n + 1);
        for (size_t i = 0; i + 1 < N; ++i)
            for (int k = 0; k < n; ++k) {
                const uint32_t A0 = v0i + uint32_t(i) * W + k, A1 = A0 + 1, B0 = A0 + W, B1 = B0 + 1;
                quadIdx(A0, A1, B0);
                quadIdx(A1, B1, B0);
            }
        for (int k = 0; k < n; ++k) {  // tampa da ponta
            glm::vec3 a0, a1, na0, na1;
            ring(N - 1, k, a0, na0); ring(N - 1, k + 1, a1, na1);
            tri(P[N - 1], a0, a1, T[N - 1], T[N - 1], T[N - 1], uvIn(uvr, 0.5f, 1.f), uvIn(uvr, 0.f, 1.f), uvIn(uvr, 1.f, 1.f));
        }
    }
    // Touca: superelipsoide (cantos suaves que cobrem a cabeça quadrada) centrada em c, raios rad; do alto até o
    // ângulo phiMax(theta) (theta 0 = frente): desce pouco na frente (testa livre) e bastante atrás (nuca)
    static glm::vec3 superDir(float th, float ph, glm::vec3 rad, glm::vec3& nor) {
        const glm::vec3 d(std::sin(ph) * std::sin(th), std::cos(ph), std::sin(ph) * std::cos(th));
        auto f = [](float x) { return (x < 0.f ? -1.f : 1.f) * std::sqrt(std::abs(x)); };  // expoente 1/2: formato de caixa redonda
        const glm::vec3 p(rad.x * f(d.x), rad.y * f(d.y), rad.z * f(d.z));
        auto g = [](float x, float r) { float t = x / r; return t * t * t / r; };  // gradiente de (x/r)^4 (já tem o sinal)
        glm::vec3 n(g(p.x, rad.x), g(p.y, rad.y), g(p.z, rad.z));
        nor = glm::length(n) > 1e-9f ? glm::normalize(n) : d;
        return p;
    }
    void cap(glm::vec3 c, glm::vec3 rad, float phiFrente, float phiAtras, int fat, int pil, glm::vec4 uvr) {
        auto phiMax = [&](float th) { float t = 0.5f - 0.5f * std::cos(th); return phiFrente + (phiAtras - phiFrente) * t * t; };
        for (int i = 0; i < fat; ++i)
            for (int j = 0; j < pil; ++j) {
                const float th0 = 6.2831853f * i / fat, th1 = 6.2831853f * (i + 1) / fat;
                const float v0 = float(j) / pil, v1 = float(j + 1) / pil;
                glm::vec3 n00, n10, n01, n11;
                glm::vec3 p00 = c + superDir(th0, v0 * phiMax(th0), rad, n00), p10 = c + superDir(th1, v0 * phiMax(th1), rad, n10);
                glm::vec3 p01 = c + superDir(th0, v1 * phiMax(th0), rad, n01), p11 = c + superDir(th1, v1 * phiMax(th1), rad, n11);
                const float u0 = float(i) / fat, u1 = float(i + 1) / fat;
                if (j > 0) tri(p00, p10, p01, n00, n10, n01, uvIn(uvr, u0, v0), uvIn(uvr, u1, v0), uvIn(uvr, u0, v1));
                tri(p10, p11, p01, n10, n11, n01, uvIn(uvr, u1, v0), uvIn(uvr, u1, v1), uvIn(uvr, u0, v1));
            }
    }
    // ponto da touca (para plantar cachos e mechas): posição e normal
    glm::vec3 onCap(glm::vec3 c, glm::vec3 rad, float th, float ph, glm::vec3& nor) { return c + superDir(th, ph, rad, nor); }
    // bolinha (esfera baixa)
    void ball(glm::vec3 c, float r, glm::vec4 uvr) {
        const int fat = 6, pil = 4;
        for (int i = 0; i < fat; ++i)
            for (int j = 0; j < pil; ++j) {
                auto P = [&](int a, int bb, glm::vec3& n) {
                    const float th = 6.2831853f * a / fat, ph = 3.14159265f * bb / pil;
                    n = {std::sin(ph) * std::sin(th), std::cos(ph), std::sin(ph) * std::cos(th)};
                    return c + n * r;
                };
                glm::vec3 n00, n10, n01, n11;
                glm::vec3 p00 = P(i, j, n00), p10 = P(i + 1, j, n10), p01 = P(i, j + 1, n01), p11 = P(i + 1, j + 1, n11);
                const glm::vec2 t(uvIn(uvr, float(i) / fat, float(j) / pil));
                if (j > 0) tri(p00, p10, p01, n00, n10, n01, t, t, t);
                if (j + 1 < pil) tri(p10, p11, p01, n10, n11, n01, t, t, t);
            }
    }
};
}  // namespace

// Cabelo 3D (ver Character.h). Cabeça: cubo de 0,15H centrado em 0,915H (frente em +0,07H).
void buildBlockHair(const CharacterParams& p, Geo& g, uint32_t seed) {
    if (p.cabelo3d <= 0) return;
    const float H = p.height, top = 0.99f * H;
    const int tipo = p.cabelo3d;
    Rng r(seed * 2654435761u + 17u);
    // textura: com skin, um pedaço do desenho do cabelo do TOPO da cabeça; sem skin, branco (a cor vai no vértice)
    const glm::vec4 topo = p.texturizado ? p.molde[SP_Cabeca][4] : glm::vec4(0.f, 0.f, 1.f, 1.f);
    auto pedaco = [&](float tam) {
        const float w = (topo.z - topo.x) * tam, h = (topo.w - topo.y) * tam;
        const float u0 = topo.x + r.range(0.f, topo.z - topo.x - w), v0 = topo.y + r.range(0.f, topo.w - topo.y - h);
        return glm::vec4(u0, v0, u0 + w, v0 + h);
    };
    auto cor = [&](MeshBuilder& m) {
        const float k = p.texturizado ? 1.f : r.range(0.85f, 1.12f);
        m.color = p.texturizado ? glm::vec4(1.f) : glm::vec4(glm::min(glm::vec3(p.hair) * k, glm::vec3(1.f)), 1.f);
    };
    MeshBuilder& base = g[Mat::Painted];
    base.joint = -1; base.xf = glm::mat4(1.f); base.uvTile = glm::vec2(0.f);
    HairMesh hb{base};
    // touca de cada estilo (centro, raios, quanto desce na frente e atrás)
    glm::vec3 c(0.f, 0.945f * H, -0.006f * H), rad(0.106f * H, 0.098f * H, 0.106f * H);                          // cacheado: volume cheio
    float phiF = 1.3f, phiA = 2.5f;
    if (tipo == 2) { c.y = 0.965f * H; rad = glm::vec3(0.135f, 0.125f, 0.135f) * H; phiF = 1.18f; phiA = 2.25f; } // black: volume grande
    if (tipo == 3) { c.y = 0.94f * H; rad = glm::vec3(0.094f, 0.086f, 0.093f) * H; phiF = 1.25f; phiA = 1.8f; }   // curto: lados raspados
    if (tipo == 4) { c.y = 0.94f * H; rad = glm::vec3(0.099f, 0.09f, 0.098f) * H; phiF = 1.3f; phiA = 2.15f; }
    cor(base);
    hb.cap(c, rad, phiF, phiA, 16, 7, topo);
    // detalhe (só de perto): cachos, mechas, bolinhas
    DetailScope ds(g);
    MeshBuilder& det = g[Mat::Painted];
    det.joint = -1; det.xf = glm::mat4(1.f); det.uvTile = glm::vec2(0.f);
    HairMesh hd{det};
    auto plantar = [&](float th, float vFrac, float phiFr, float phiAt, glm::vec3& n) {
        float t = 0.5f - 0.5f * std::cos(th);
        float ph = vFrac * (phiFr + (phiAt - phiFr) * t * t);
        return hd.onCap(c, rad, th, ph, n);
    };
    auto espiral = [&](glm::vec3 base0, glm::vec3 eixo, float comp, float raioVolta, float raioTubo, float voltas, int lados) {
        eixo = glm::normalize(eixo);
        glm::vec3 ref = std::abs(eixo.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 a1 = glm::normalize(glm::cross(ref, eixo)), a2 = glm::cross(eixo, a1);
        const int seg = std::max(6, int(voltas * 6.f));
        const float fase = r.range(0.f, 6.2831853f);
        std::vector<glm::vec3> P; std::vector<float> R;
        for (int i = 0; i <= seg; ++i) {
            const float t = float(i) / seg, ang = fase + t * voltas * 6.2831853f;
            const float rv = raioVolta * (1.f - 0.25f * t);  // a ponta do cacho afina
            P.push_back(base0 + eixo * (comp * t) + (a1 * std::cos(ang) + a2 * std::sin(ang)) * rv);
            R.push_back(raioTubo * (1.f - 0.35f * t));
        }
        cor(det);
        hd.tube(P, R, lados, pedaco(0.25f));
    };
    const glm::vec3 up(0.f, 1.f, 0.f), frente(0.f, 0.f, 1.f);
    // plantio espalhado por igual (sem buracos): ângulo em volta da cabeça em "girassol" + altura com sorteio leve
    auto girassol = [&](int i, int n, float vMin, float vMax, float& th, float& vf) {
        th = std::fmod(i * 2.3999632f + r.range(-0.15f, 0.15f), 6.2831853f);
        vf = std::sqrt(vMin * vMin + (vMax * vMax - vMin * vMin) * (i + r.range(0.f, 1.f)) / n);
    };
    if (tipo == 1) {  // cacheado: duas camadas de espirais cobrindo a touca inteira + franja + cachos caindo na nuca e dos lados
        const int N1 = 150;
        for (int i = 0; i < N1; ++i) {
            glm::vec3 n; float th, vf;
            girassol(i, N1, 0.05f, 0.97f, th, vf);
            glm::vec3 pt = plantar(th, vf, phiF, phiA, n);
            glm::vec3 eixo = n + up * 0.25f + glm::vec3(r.range(-0.35f, 0.35f), r.range(-0.1f, 0.1f), r.range(-0.35f, 0.35f));
            espiral(pt - n * 0.008f * H, eixo, r.range(0.03f, 0.048f) * H, 0.011f * H, 0.0078f * H, r.range(1.2f, 1.8f), 4);
        }
        for (int i = 0; i < 70; ++i) {  // reforço atrás e nos lados (a touca desce mais ali)
            glm::vec3 n;
            const float th = 3.14159265f + r.range(-2.3f, 2.3f), vf = r.range(0.45f, 0.98f);
            glm::vec3 pt = plantar(th, vf, phiF, phiA, n);
            glm::vec3 eixo = n - up * 0.15f + glm::vec3(r.range(-0.35f, 0.35f), r.range(-0.1f, 0.1f), r.range(-0.35f, 0.35f));
            espiral(pt - n * 0.008f * H, eixo, r.range(0.03f, 0.046f) * H, 0.011f * H, 0.0078f * H, r.range(1.2f, 1.8f), 4);
        }
        for (int i = 0; i < 12; ++i) {  // franja
            glm::vec3 n;
            const float th = -0.85f + 1.7f * (i + r.range(0.3f, 0.7f)) / 12.f;
            glm::vec3 pt = plantar(th, r.range(0.93f, 1.f), phiF, phiA, n);
            espiral(pt, frente * 0.55f - up + glm::vec3(r.range(-0.25f, 0.25f), 0.f, 0.f), r.range(0.026f, 0.04f) * H, 0.008f * H, 0.0058f * H, 1.4f, 4);
        }
        for (int i = 0; i < 26; ++i) {  // cachos que caem dos lados e na nuca
            glm::vec3 n;
            const float th = 0.9f + (6.2831853f - 1.8f) * (i + r.range(0.2f, 0.8f)) / 26.f;
            glm::vec3 pt = plantar(th, r.range(0.85f, 1.f), phiF, phiA, n);
            glm::vec3 eixo = glm::vec3(n.x, 0.f, n.z) * 0.45f - up;
            espiral(pt - n * 0.004f * H, eixo, r.range(0.03f, 0.05f) * H, 0.0095f * H, 0.0068f * H, r.range(1.4f, 2.f), 4);
        }
    } else if (tipo == 2) {  // black: bolinhas em duas camadas por toda a superfície do volume
        const int N1 = 170;
        for (int i = 0; i < N1; ++i) {
            glm::vec3 n; float th, vf;
            girassol(i, N1, 0.f, 0.97f, th, vf);
            glm::vec3 pt = plantar(th, vf, phiF, phiA, n);
            cor(det);
            hd.ball(pt + n * r.range(0.f, 0.008f) * H, r.range(0.014f, 0.022f) * H, pedaco(0.2f));
        }
    } else if (tipo == 3) {  // curto: tapete denso de espirais pequenas no alto
        const int N1 = 110;
        for (int i = 0; i < N1; ++i) {
            glm::vec3 n; float th, vf;
            girassol(i, N1, 0.f, 0.85f, th, vf);
            glm::vec3 pt = plantar(th, vf, phiF, phiA, n);
            espiral(pt - n * 0.004f * H, n + up * 0.4f + glm::vec3(r.range(-0.2f, 0.2f), 0.f, r.range(-0.2f, 0.2f)),
                    r.range(0.016f, 0.026f) * H, 0.0068f * H, 0.0048f * H, r.range(1.f, 1.4f), 4);
        }
    } else if (tipo == 4) {  // dreads: três fileiras de mechas torcidas caindo com o peso + o alto todo coberto
        auto dread = [&](glm::vec3 pt, glm::vec3 n, float tt, float compMul, float grosso) {
            const float comp = (0.04f + 0.1f * tt) * H * compMul;
            glm::vec3 sai = glm::normalize(glm::vec3(n.x, 0.f, n.z) + glm::vec3(0.f, 0.001f, 0.f));
            std::vector<glm::vec3> P; std::vector<float> R;
            const int K = 7;
            const float bal = r.range(-0.006f, 0.006f) * H;
            const glm::vec3 lado = glm::cross(up, sai);
            for (int k = 0; k <= K; ++k) {
                const float t = float(k) / K;
                P.push_back(pt - n * 0.004f * H + sai * (0.02f * H * std::sqrt(t)) - up * (comp * std::pow(t, 1.35f)) + lado * (bal * t * t));
                R.push_back((0.0095f - 0.0028f * t) * H * grosso * (k % 2 ? 0.82f : 1.f));  // torção: grosso, fino, grosso...
            }
            cor(det);
            hd.tube(P, R, 5, pedaco(0.3f));
        };
        const int fil[3] = {44, 34, 24};
        const float vfil[3] = {0.9f, 0.68f, 0.46f};
        for (int f = 0; f < 3; ++f)
            for (int i = 0; i < fil[f]; ++i) {
                glm::vec3 n;
                const float th = 6.2831853f * (i + 0.5f * f + r.range(-0.2f, 0.2f)) / fil[f];
                const float tt = 0.5f - 0.5f * std::cos(th);  // 0 frente, 1 atrás
                if (f == 0 && tt < 0.12f) continue;            // testa livre na fileira de baixo
                glm::vec3 pt = plantar(th, vfil[f] + r.range(-0.04f, 0.04f), phiF, phiA, n);
                dread(pt, n, tt, r.range(0.85f, 1.15f) * (1.f + 0.12f * f), 1.f);
            }
        for (int i = 0; i < 40; ++i) {  // o alto: pontas curtas e grossas cobrindo o couro cabeludo
            glm::vec3 n; float th, vf;
            girassol(i, 40, 0.f, 0.5f, th, vf);
            glm::vec3 pt = plantar(th, vf, phiF, phiA, n);
            espiral(pt - n * 0.004f * H, n + up * 0.5f, r.range(0.016f, 0.024f) * H, 0.003f * H, 0.0075f * H, 0.6f, 5);
        }
    }
    base.color = glm::vec4(1.f);
}

void buildCharacter(const CharacterParams& p, LoadedModel& out, MeshBuilder& b) {
    const float H = p.height;
    const float s = p.build == 0 ? 0.85f : p.build == 2 ? 1.2f : 1.f;
    Joints J;
    // bloco (tipo Roblox): tronco largo e quadrado, braços e pernas grossos
    const float chestHalf = p.bloco ? 0.12f * H : 0.115f * H * s, armHalf = p.bloco ? 0.05f * H : 0.034f * H * (0.9f + 0.1f * s);
    const float sx = chestHalf + armHalf + (p.bloco ? 0.f : 0.004f * H), tx = p.bloco ? 0.06f * H : 0.055f * H * s;
    const float shoulderY = 0.825f * H, elbowY = shoulderY - (p.bloco ? 0.165f : 0.175f) * H, wristY = elbowY - (p.bloco ? 0.15f : 0.155f) * H;
    const float thighY = (p.bloco ? 0.5f : 0.52f) * H, kneeY = (p.bloco ? 0.27f : 0.285f) * H, ankleY = 0.045f * H;
    glm::vec3 pos[CJ_Total] = {
        {0, 0.53f * H, 0}, {0, 0.62f * H, 0}, {0, 0.72f * H, 0}, {0, 0.865f * H, 0},
        {sx, shoulderY, 0}, {sx, elbowY, 0}, {sx, wristY, 0}, {-sx, shoulderY, 0}, {-sx, elbowY, 0}, {-sx, wristY, 0},
        {tx, thighY, 0}, {tx, kneeY, 0}, {tx, ankleY, 0}, {-tx, thighY, 0}, {-tx, kneeY, 0}, {-tx, ankleY, 0}};
    int parent[CJ_Total] = {-1, CJ_Hips, CJ_Spine, CJ_Neck, CJ_Chest, CJ_LUpper, CJ_LFore, CJ_Chest, CJ_RUpper, CJ_RFore,
                            CJ_Hips, CJ_LThigh, CJ_LShin, CJ_Hips, CJ_RThigh, CJ_RShin};
    pos[CJ_Neck] = {0, 0.835f * H, 0};
    parent[CJ_Neck] = CJ_Chest;
    // dedos: palma de frente (+Z); polegar e indicador do lado de fora; 2 articulações por dedo
    const float palmBottom = 0.0575f * H;
    for (int side = 0; side < 2; ++side) {
        float sgn = side ? -1.f : 1.f;
        glm::vec3 wr = pos[side ? CJ_RHand : CJ_LHand];
        int hand = side ? CJ_RHand : CJ_LHand, b = side ? CJ_RThumb1 : CJ_LThumb1;
        pos[b] = wr + glm::vec3(sgn * 0.022f * H, -0.015f * H, 0.012f * H);
        pos[b + 1] = wr + glm::vec3(sgn * 0.03f * H, -0.036f * H, 0.02f * H);
        parent[b] = hand; parent[b + 1] = b;
        const float fx[4] = {0.018f, 0.006f, -0.006f, -0.018f};  // indicador, médio, anelar, mínimo
        for (int f = 0; f < 4; ++f) {
            int j = b + 2 + f * 2;
            pos[j] = wr + glm::vec3(sgn * fx[f] * H, -palmBottom, 0.004f * H);
            pos[j + 1] = pos[j] + glm::vec3(0, -(f == 3 ? 0.02f : 0.026f) * H, 0);
            parent[j] = hand; parent[j + 1] = j;
        }
    }
    for (int i = 0; i < CJ_Total; ++i) { J.pos[i] = pos[i]; J.parent[i] = parent[i]; }
    J.H = H;

    // ---- esqueleto (pose de bind sem rotações: inverseBind = translação inversa)
    Skeleton sk{};
    sk.jointCount = CJ_Total;
    {   // ordem de avaliação: pais antes dos filhos (o pescoço vem depois da cabeça na numeração)
        int n = 0;
        bool done[CJ_Total] = {};
        while (n < CJ_Total)
            for (int i = 0; i < CJ_Total; ++i)
                if (!done[i] && (parent[i] < 0 || done[parent[i]])) { done[i] = true; sk.order[n++] = i; }
    }
    for (int i = 0; i < CJ_Total; ++i) {
        sk.parent[i] = parent[i];
        sk.bindT[i] = parent[i] < 0 ? pos[i] : pos[i] - pos[parent[i]];
        sk.bindR[i] = glm::quat(1, 0, 0, 0);
        sk.bindS[i] = glm::vec3(1.f);
        sk.inverseBind[i] = glm::translate(glm::mat4(1.f), -pos[i]);
    }
    // nomes no padrão Mixamo: reconhecidos pelo editor de animação, Unity (Humanoid) e Blender ao exportar .glb
    static const char* names[CJ_Total] = {"Hips", "Spine", "Spine2", "Head", "LeftArm", "LeftForeArm", "LeftHand", "RightArm",
                                          "RightForeArm", "RightHand", "LeftUpLeg", "LeftLeg", "LeftFoot", "RightUpLeg", "RightLeg", "RightFoot",
                                          "Neck",
                                          "LeftHandThumb1", "LeftHandThumb2", "LeftHandIndex1", "LeftHandIndex2", "LeftHandMiddle1",
                                          "LeftHandMiddle2", "LeftHandRing1", "LeftHandRing2", "LeftHandPinky1", "LeftHandPinky2",
                                          "RightHandThumb1", "RightHandThumb2", "RightHandIndex1", "RightHandIndex2", "RightHandMiddle1",
                                          "RightHandMiddle2", "RightHandRing1", "RightHandRing2", "RightHandPinky1", "RightHandPinky2"};
    for (int i = 0; i < CJ_Total; ++i) std::snprintf(sk.name[i], sizeof(sk.name[i]), "%s", names[i]);
    out.skeletons.push_back(sk);

    // ---- malha (caixas presas a um osso cada, estilo low-poly)
    b.xf = glm::mat4(1.f);
    b.uvTile = glm::vec2(0.f);
    auto part = [&](int joint, glm::vec4 col, glm::vec3 center, glm::vec3 size) {
        b.joint = joint; b.color = col; b.boxC(center, size);
    };
    if (p.bloco && p.texturizado) {
        buildTexturedBlockBody(p, b, H, sx, tx, shoulderY, elbowY, wristY, thighY, kneeY, ankleY);
    } else if (p.bloco) {
        buildBlockBody(p, b, H, sx, tx, shoulderY, elbowY, wristY, thighY, kneeY, ankleY);
    } else {
    glm::vec4 dark = srgb(25, 20, 18), white = srgb(245, 245, 245), lips = p.skin * glm::vec4(0.75f, 0.55f, 0.55f, 1.f);
    lips.a = 1.f;
    // tronco
    part(CJ_Hips, p.pants, {0, 0.515f * H, 0}, {0.2f * H * s, 0.075f * H, 0.115f * H * s});
    part(CJ_Spine, p.shirt, {0, 0.605f * H, 0}, {0.19f * H * s, 0.12f * H, 0.105f * H * s});
    part(CJ_Chest, p.shirt, {0, 0.75f * H, 0}, {chestHalf * 2.f, 0.165f * H, 0.12f * H * s});
    part(CJ_Chest, p.shirt, {0, 0.83f * H, 0}, {chestHalf * 1.4f, 0.02f * H, 0.1f * H * s});  // ombros
    // cabeça
    const float headY = 0.925f * H, hw = 0.11f * H, hh = 0.13f * H, hd = 0.12f * H, front = hd / 2;
    part(CJ_Neck, p.skin, {0, 0.855f * H, 0}, {0.05f * H, 0.045f * H, 0.05f * H});           // pescoço
    part(CJ_Head, p.skin, {0, headY, 0.004f * H}, {hw, hh, hd});
    for (float side : {-1.f, 1.f}) {
        part(CJ_Head, white, {side * 0.024f * H, headY + 0.008f * H, front + 0.004f * H}, {0.022f * H, 0.014f * H, 0.004f * H});
        part(CJ_Head, dark, {side * 0.024f * H, headY + 0.008f * H, front + 0.0065f * H}, {0.01f * H, 0.012f * H, 0.003f * H});
        part(CJ_Head, p.hair, {side * 0.025f * H, headY + 0.025f * H, front + 0.004f * H}, {0.026f * H, 0.005f * H, 0.004f * H});
        part(CJ_Head, p.skin, {side * hw / 2, headY + 0.002f * H, 0.005f * H}, {0.012f * H, 0.03f * H, 0.022f * H});  // orelhas
    }
    part(CJ_Head, p.skin * 0.95f, {0, headY - 0.008f * H, front + 0.008f * H}, {0.014f * H, 0.024f * H, 0.014f * H});  // nariz
    part(CJ_Head, lips, {0, headY - 0.035f * H, front + 0.004f * H}, {0.034f * H, 0.007f * H, 0.004f * H});            // boca
    // cabelo
    const float top = headY + hh / 2;
    if (p.hairStyle == 1 || p.hairStyle == 2) {
        part(CJ_Head, p.hair, {0, top + 0.008f * H, 0.0f}, {hw + 0.008f * H, 0.022f * H, hd + 0.01f * H});
        part(CJ_Head, p.hair, {0, headY + 0.02f * H, -hd / 2 - 0.002f * H}, {hw + 0.008f * H, hh * 0.75f, 0.012f * H});
        part(CJ_Head, p.hair, {0, top - 0.004f * H, front + 0.004f * H}, {hw + 0.004f * H, 0.018f * H, 0.012f * H});  // franja
        for (float side : {-1.f, 1.f})
            part(CJ_Head, p.hair, {side * (hw / 2 + 0.003f * H), headY + 0.03f * H, -0.01f * H}, {0.008f * H, hh * 0.45f, hd * 0.75f});
        if (p.hairStyle == 2)
            part(CJ_Chest, p.hair, {0, 0.84f * H, -hd / 2 - 0.006f * H}, {hw + 0.01f * H, 0.16f * H, 0.02f * H});
    } else if (p.hairStyle == 3) {  // boné
        part(CJ_Head, p.shirt, {0, top + 0.012f * H, -0.004f * H}, {hw + 0.012f * H, 0.03f * H, hd + 0.01f * H});
        part(CJ_Head, p.shirt * 0.8f, {0, top + 0.0f, front + 0.035f * H}, {hw * 0.9f, 0.008f * H, 0.07f * H});
    }
    // braços (manga curta ou comprida)
    for (int side = 0; side < 2; ++side) {
        float x = side ? -sx : sx;
        int up = side ? CJ_RUpper : CJ_LUpper, fo = side ? CJ_RFore : CJ_LFore, ha = side ? CJ_RHand : CJ_LHand;
        part(up, p.shirt, {x, shoulderY - 0.045f * H, 0}, {armHalf * 2.2f, 0.1f * H, armHalf * 2.2f});
        part(up, p.longSleeves ? p.shirt : p.skin, {x, shoulderY - 0.125f * H, 0}, {armHalf * 1.8f, 0.1f * H, armHalf * 1.8f});
        part(fo, p.longSleeves ? p.shirt : p.skin, {x, elbowY - 0.075f * H, 0}, {armHalf * 1.65f, 0.16f * H, armHalf * 1.65f});
        part(ha, p.skin, {x, wristY - 0.03f * H, 0.004f * H}, {0.05f * H, 0.06f * H, 0.03f * H});  // palma
        int fb = side ? CJ_RThumb1 : CJ_LThumb1;
        for (int f = 0; f < 5; ++f) {
            for (int seg = 0; seg < 2; ++seg) {
                int j = fb + f * 2 + seg;
                glm::vec3 a = pos[j], c = seg == 0 ? pos[j + 1] : pos[j] + (pos[j] - pos[j - 1]) * (f == 0 ? 0.8f : 0.85f);
                glm::vec3 mid = (a + c) * 0.5f;
                float len = glm::length(c - a) + 0.004f * H, w = (f == 0 ? 0.013f : f == 4 ? 0.009f : 0.0105f) * H;
                part(j, p.skin, mid, {w, len, w * 1.15f});
            }
        }
    }
    // pernas
    for (int side = 0; side < 2; ++side) {
        float x = side ? -tx : tx;
        int th = side ? CJ_RThigh : CJ_LThigh, sh = side ? CJ_RShin : CJ_LShin, ft = side ? CJ_RFoot : CJ_LFoot;
        part(th, p.pants, {x, (thighY + kneeY) / 2 + 0.01f * H, 0}, {0.092f * H * s, thighY - kneeY + 0.03f * H, 0.098f * H * s});
        part(sh, p.pants, {x, (kneeY + ankleY) / 2 + 0.01f * H, 0}, {0.076f * H * s, kneeY - ankleY + 0.01f * H, 0.082f * H * s});
        part(ft, p.shoes, {x, 0.026f * H, 0.026f * H}, {0.078f * H, 0.052f * H, 0.155f * H});
    }
    }  // fim do corpo realista
    b.joint = -1;
    b.color = glm::vec4(1.f);
    // skinning rígido: todos os vértices já têm joints/weights do osso

    // ---- animações: captura de movimento (biblioteca) onde houver; senão, feitas por código
    const auto& lib = mocapLibrary();
    out.channelStorage.reserve((CC_Count + lib.size()) * 48);  // os ponteiros dos clips não podem mudar
    out.keyData.reserve(out.keyData.size() + (CC_Count + lib.size()) * 48);
    auto mocap = [&](const MocapClip& c, const char* name) {
        AnimationClip clip;
        std::snprintf(clip.name, sizeof(clip.name), "%s", name);
        clip.duration = c.duration();
        size_t first = out.channelStorage.size();
        for (int j = 0; j < CJ_Total; ++j) {  // rotações: direto da biblioteca (iguais para qualquer altura)
            Channel ch;
            ch.joint = j; ch.path = ChannelPath::Rotation; ch.keyCount = c.frames;
            ch.times = c.times.data(); ch.values = c.rot.data() + size_t(j) * c.frames * 4;
            out.channelStorage.push_back(ch);
        }
        out.keyData.emplace_back(size_t(c.frames) * 3);  // quadril na altura deste personagem
        std::vector<float>& hp = out.keyData.back();
        const glm::vec3 hips0 = J.pos[CJ_Hips];
        for (int f = 0; f < c.frames; ++f) {
            glm::vec3 v = hips0 + c.hips[f] * hips0.y;
            hp[f * 3] = v.x; hp[f * 3 + 1] = v.y; hp[f * 3 + 2] = v.z;
        }
        Channel ch;
        ch.joint = CJ_Hips; ch.path = ChannelPath::Translation; ch.keyCount = c.frames;
        ch.times = c.times.data(); ch.values = hp.data();
        out.channelStorage.push_back(ch);
        clip.channels = out.channelStorage.data() + first;
        clip.channelCount = int(out.channelStorage.size() - first);
        out.clips.push_back(clip);
    };
    // cada papel: a animação capturada (se a biblioteca tiver) ou a gerada
    auto slot = [&](int cc, const char* name, float dur, const std::function<void(ClipWriter&)>& gen) {
        if (const MocapClip* m = mocapForSlot(clipSlot(cc))) { mocap(*m, name); return; }
        ClipWriter w(out, name, dur);
        gen(w);
        w.finish();
    };
    slot(CC_Idle, "Parado", 3.f, [&](ClipWriter& w) { idle(w, J); });
    slot(CC_Walk, "Andando", 1.05f, [&](ClipWriter& w) { locomotion(w, J, 0.5f, 0.75f, 0.4f, 0.25f, 0.03f, 0.025f); });
    slot(CC_Run, "Correndo", 0.66f, [&](ClipWriter& w) { locomotion(w, J, 0.95f, 1.5f, 0.85f, 1.25f, 0.2f, 0.06f); });
    slot(CC_Dance, "Dancando", 1.3f, [&](ClipWriter& w) { dance(w, J); });
    slot(CC_Wave, "Acenando", 1.6f, [&](ClipWriter& w) { wave(w, J); });
    const char* cls[3] = {"fuzil", "pistola", "faca"};
    const char* modes[5] = {"Parado", "Andando", "Correndo", "Mirando", "Andando mirando"};
    const float durs[5] = {3.f, 1.05f, 0.7f, 2.f, 1.05f};
    for (int c = 0; c < 3; ++c)
        for (int m = 0; m < 5; ++m) {
            char name[32];
            std::snprintf(name, sizeof(name), "%s (%s)", c == 2 && m >= AP_Aim ? (m == AP_Aim ? "Golpe" : "Golpe andando") : modes[m], cls[c]);
            slot(CC_Rifle + c * 5 + m, name, c == 2 && m >= AP_Aim ? 0.5f : durs[m], [&](ClipWriter& w) { armedClip(w, J, CC_Rifle + c * 5, m); });
        }
    // extras: sem animação capturada, usam uma parecida feita por código
    slot(CC_Death, "Morte", 3.f, [&](ClipWriter& w) { idle(w, J); });
    slot(CC_Reload, "Recarregando (fuzil)", 2.f, [&](ClipWriter& w) { armedClip(w, J, CC_Rifle, AP_Idle); });
    slot(CC_RifleBack, "De costas (fuzil)", 1.05f, [&](ClipWriter& w) { armedClip(w, J, CC_Rifle, AP_AimWalk); });
    slot(CC_RifleLeft, "Lado esquerdo (fuzil)", 1.05f, [&](ClipWriter& w) { armedClip(w, J, CC_Rifle, AP_AimWalk); });
    slot(CC_RifleRight, "Lado direito (fuzil)", 1.05f, [&](ClipWriter& w) { armedClip(w, J, CC_Rifle, AP_AimWalk); });
    slot(CC_RifleFire, "Atirando (fuzil)", 1.f, [&](ClipWriter& w) { armedClip(w, J, CC_Rifle, AP_Aim); });
    slot(CC_Jump, "Pulando", 0.9f, [&](ClipWriter& w) { idle(w, J); });
    slot(CC_CrouchWalk, "Agachado andando (fuzil)", 1.05f, [&](ClipWriter& w) { armedClip(w, J, CC_Rifle, AP_Walk); });
    // o resto da biblioteca (sem papel) entra no fim da lista
    for (const MocapClip& m : lib) if (m.slot.empty() || m.slot == "-") mocap(m, m.name.c_str());
}

const char* clipSlot(int cc) {
    static const char* slots[CC_Count] = {"parado", "andando", "correndo", "dancando", "acenando",
                                          "fuzil_parado", "fuzil_andando", "fuzil_correndo", "fuzil_mirando", "fuzil_andando_mirando",
                                          "pistola_parado", "pistola_andando", "pistola_correndo", "pistola_mirando", "pistola_andando_mirando",
                                          "faca_parado", "faca_andando", "faca_correndo", "faca_golpe", "faca_golpe_andando",
                                          "morte", "recarregar", "fuzil_costas", "fuzil_esquerda", "fuzil_direita", "fuzil_atirando", "pulo",
                                          "agachado_andando"};
    return cc >= 0 && cc < CC_Count ? slots[cc] : "";
}

}  // namespace eng
