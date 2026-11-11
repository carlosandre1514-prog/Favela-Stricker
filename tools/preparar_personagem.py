#!/usr/bin/env python3
"""Prepara um personagem da Meshy (sem esqueleto) para o esqueleto dos personagens do jogo.

Entrada: o .glb que o tools/converter_meshy.cpp gera para o personagem (sem --arma: pés no chão, de frente para +Z,
uma malha, texturas cor / normal / ORM embutidas). A Meshy entrega em pose A (braços abertos para baixo).
Saída (shaders/kit/personagens/meshy/):
  <nome>.bin           malha já com os ossos: os braços descem para a pose de repouso do esqueleto do jogo (braços
                       retos ao lado do corpo, motor/procgen/Character.cpp), altura ALTURA, e cada vértice preso a até
                       4 ossos (pesos pela distância até cada osso, só os ossos da região certa: braço, perna ou tronco)
  <nome>.jpg, <nome>_normal.jpg, <nome>_orm.jpg   as texturas
Formato do .bin: "FSP1", nv, ni (uint32); por vértice pos(3f) normal(3f) uv(2f) tangente(4f) ossos(4 uint16) pesos(4f);
depois os índices (uint32).

Uso: python3 tools/preparar_personagem.py <entrada.glb> <pasta_saida> <nome>
"""
import io
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

ALTURA = 1.78  # a mesma do "altura" das roupas (jogo/Partida.cpp)

# ossos do esqueleto do jogo (CharJoint em motor/procgen/Character.h)
HIPS, SPINE, CHEST, HEAD, LUP, LFO, LHA, RUP, RFO, RHA, LTH, LSH, LFT, RTH, RSH, RFT, NECK = range(17)


def ler_glb(p):
    d = open(p, 'rb').read()
    l = struct.unpack('<I', d[12:16])[0]
    js = json.loads(d[20:20 + l])
    off = 20 + l + 8

    def acc(i, c, dt):
        a = js['accessors'][i]
        bv = js['bufferViews'][a['bufferView']]
        o = off + bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        return np.frombuffer(d, dtype=dt, count=a['count'] * c, offset=o).reshape(-1, c).copy()

    pr = js['meshes'][0]['primitives'][0]
    at = pr['attributes']
    P = acc(at['POSITION'], 3, np.float32)
    N = acc(at['NORMAL'], 3, np.float32)
    U = acc(at['TEXCOORD_0'], 2, np.float32)
    T = acc(at['TANGENT'], 4, np.float32)
    I = acc(pr['indices'], 1, np.uint32).ravel()
    imgs = []
    for im in js['images']:
        bv = js['bufferViews'][im['bufferView']]
        o = off + bv.get('byteOffset', 0)
        imgs.append(Image.open(io.BytesIO(d[o:o + bv['byteLength']])).convert('RGB'))
    return P, N, U, T, I, imgs


def juntas(H):
    """Posições do esqueleto do jogo em repouso (as mesmas contas de buildCharacter, corpo normal, sem bloco)."""
    sx, tx = 0.153 * H, 0.055 * H
    sy, ey, wy = 0.825 * H, 0.65 * H, 0.495 * H
    return {
        'hips': np.array([0, 0.53 * H, 0]), 'spine': np.array([0, 0.62 * H, 0]), 'chest': np.array([0, 0.72 * H, 0]),
        'neck': np.array([0, 0.835 * H, 0]), 'head': np.array([0, 0.865 * H, 0]),
        'lsh': np.array([sx, sy, 0]), 'lel': np.array([sx, ey, 0]), 'lwr': np.array([sx, wy, 0]),
        'rsh': np.array([-sx, sy, 0]), 'rel': np.array([-sx, ey, 0]), 'rwr': np.array([-sx, wy, 0]),
        'lhi': np.array([tx, 0.52 * H, 0]), 'lkn': np.array([tx, 0.285 * H, 0]), 'lan': np.array([tx, 0.045 * H, 0]),
        'rhi': np.array([-tx, 0.52 * H, 0]), 'rkn': np.array([-tx, 0.285 * H, 0]), 'ran': np.array([-tx, 0.045 * H, 0]),
    }


def dist_segmento(P, a, b):
    ab = b - a
    t = np.clip(((P - a) @ ab) / max(1e-9, ab @ ab), 0, 1)
    return np.linalg.norm(P - (a + t[:, None] * ab), axis=1)


def main():
    entrada, saida, nome = sys.argv[1], sys.argv[2], sys.argv[3]
    P, N, U, T, I, imgs = ler_glb(entrada)
    H0 = P[:, 1].max() - P[:, 1].min()
    P[:, 1] -= P[:, 1].min()
    P *= ALTURA / H0
    H = ALTURA
    J = juntas(H)

    # ---- braços: da pose A da Meshy para a pose de repouso do jogo (retos para baixo, no ombro do esqueleto)
    piv_x = 0.125 * H                      # centro do ombro de uma pessoa (o esqueleto do jogo fica em 0,153 H)
    piv_y = 0.81 * H
    # o que é braço sai da pose A (lá o braço está longe do corpo): só esses vértices pegam os ossos do braço.
    # (com os braços já abaixados, a mão fica ao lado do quadril e o quadril pegava o osso do antebraço: a malha
    # esticava entre a mão e a perna nas animações)
    eh_braco = {}
    ao_longo = {}
    for lado in (1, -1):
        s = P[:, 0] * lado
        mao = np.argmax(s)                                     # na pose A, a ponta da mão é o ponto mais de fora
        piv = np.array([lado * piv_x, piv_y, 0.0])
        d = P[mao] - piv
        d[2] = 0
        d /= np.linalg.norm(d)
        ang = np.arctan2(d[0], -d[1])                           # ângulo do braço em relação a "para baixo"
        rel = P - piv
        t = rel[:, :2] @ d[:2]
        r = np.linalg.norm(rel[:, :2] - t[:, None] * d[:2][None, :], axis=1)
        braco = (s > piv_x * 0.55) & (t > -0.02 * H) & (r < 0.085 * H) & (P[:, 1] > 0.38 * H)
        eh_braco[lado] = braco.copy()
        ao_longo[lado] = t.copy()
        w = np.clip(t / (0.1 * H), 0, 1)
        w = w * w * (3 - 2 * w) * braco
        a = -ang * w                                           # gira em volta do eixo Z (de frente)
        c, sn = np.cos(a), np.sin(a)
        x, y = rel[:, 0].copy(), rel[:, 1].copy()
        P[:, 0] = np.where(braco, piv[0] + x * c - y * sn, P[:, 0])
        P[:, 1] = np.where(braco, piv[1] + x * sn + y * c, P[:, 1])
        for V in (N, T):
            vx, vy = V[:, 0].copy(), V[:, 1].copy()
            V[:, 0] = np.where(braco, vx * c - vy * sn, vx)
            V[:, 1] = np.where(braco, vx * sn + vy * c, vy)
        P[:, 0] += braco * w * lado * (J['lsh'][0] - piv_x)    # encaixa no ombro do esqueleto
        print(f'  braço {"esquerdo" if lado > 0 else "direito"}: {np.degrees(ang):.0f}° da vertical, {int(braco.sum())} vértices')

    # ---- pesos dos ossos: distância até cada osso, só os ossos da região do vértice
    sx = J['lsh'][0]
    ossos = [  # (osso, a, b)
        (HIPS, np.array([0, 0.45 * H, 0]), np.array([0, 0.58 * H, 0])),
        (SPINE, np.array([0, 0.58 * H, 0]), np.array([0, 0.68 * H, 0])),
        (CHEST, np.array([0, 0.68 * H, 0]), np.array([0, 0.80 * H, 0])),
        (NECK, np.array([0, 0.80 * H, 0]), np.array([0, 0.865 * H, 0])),
        (HEAD, np.array([0, 0.865 * H, 0]), np.array([0, 1.0 * H, 0])),
        (LUP, J['lsh'], J['lel']), (LFO, J['lel'], J['lwr']), (LHA, J['lwr'], J['lwr'] - np.array([0, 0.1 * H, 0])),
        (RUP, J['rsh'], J['rel']), (RFO, J['rel'], J['rwr']), (RHA, J['rwr'], J['rwr'] - np.array([0, 0.1 * H, 0])),
        (LTH, J['lhi'], J['lkn']), (LSH, J['lkn'], J['lan']), (LFT, J['lan'], J['lan'] + np.array([0, -0.03 * H, 0.08 * H])),
        (RTH, J['rhi'], J['rkn']), (RSH, J['rkn'], J['ran']), (RFT, J['ran'], J['ran'] + np.array([0, -0.03 * H, 0.08 * H])),
    ]
    D = np.stack([dist_segmento(P, a, b) for _, a, b in ossos], axis=1)
    idx = np.array([o for o, _, _ in ossos])
    x, y = P[:, 0], P[:, 1]
    braco_e, braco_d = eh_braco[1], eh_braco[-1]
    qualquer_braco = braco_e | braco_d
    t_e, t_d = ao_longo[1], ao_longo[-1]
    perna = (y < 0.56 * H) & ~qualquer_braco
    pode = np.zeros_like(D, dtype=bool)
    col = {o: k for k, o in enumerate(idx)}
    # braço: os 3 ossos do lado; perto do ombro (até 0,1 H do pivô) também o peito, para a malha passar suave
    for k in (col[LUP], col[LFO], col[LHA]): pode[:, k] = braco_e
    for k in (col[RUP], col[RFO], col[RHA]): pode[:, k] = braco_d
    ombro = (braco_e & (t_e < 0.1 * H)) | (braco_d & (t_d < 0.1 * H))
    for k in (col[CHEST], col[NECK]): pode[:, k] = ~qualquer_braco | ombro
    for k in (col[HIPS], col[SPINE], col[HEAD]): pode[:, k] = ~qualquer_braco
    # tronco na altura do ombro (alça da camisa, deltoide): pode pegar um pouco do braço do lado
    capa_e = ~qualquer_braco & (y > 0.72 * H) & (x > 0.08 * H)
    capa_d = ~qualquer_braco & (y > 0.72 * H) & (x < -0.08 * H)
    pode[:, col[LUP]] |= capa_e
    pode[:, col[RUP]] |= capa_d
    # pernas: só o que não é braço, do lado certo (um pouco dos dois no meio, entre as pernas)
    for k in (col[LTH], col[LSH], col[LFT]): pode[:, k] = perna & (x > -0.01 * H)
    for k in (col[RTH], col[RSH], col[RFT]): pode[:, k] = perna & (x < 0.01 * H)
    D = np.where(pode, D, 1e9)
    ordem = np.argsort(D, axis=1)[:, :3]
    dd = np.take_along_axis(D, ordem, axis=1)
    wv = 1.0 / (dd ** 4 + (0.012 * H) ** 4)
    wv = np.where(dd > 1e8, 0, wv)
    wv /= wv.sum(axis=1, keepdims=True)
    wv[wv < 0.03] = 0
    wv /= wv.sum(axis=1, keepdims=True)
    jo = idx[ordem]

    os.makedirs(saida, exist_ok=True)
    nv, ni = len(P), len(I)
    with open(os.path.join(saida, nome + '.bin'), 'wb') as f:
        f.write(b'FSP1')
        f.write(struct.pack('<II', nv, ni))
        reg = np.zeros(nv, dtype=[('p', '<f4', 3), ('n', '<f4', 3), ('u', '<f4', 2), ('t', '<f4', 4), ('j', '<u2', 4), ('w', '<f4', 4)])
        reg['p'], reg['n'], reg['u'], reg['t'] = P, N, U, T
        j4 = np.zeros((nv, 4), np.uint16); j4[:, :3] = jo
        w4 = np.zeros((nv, 4), np.float32); w4[:, :3] = wv
        reg['j'], reg['w'] = j4, w4
        f.write(reg.tobytes())
        f.write(I.astype('<u4').tobytes())
    nomes = ['', '_normal', '_orm']
    for im, suf in zip(imgs[:3], nomes):
        im.save(os.path.join(saida, nome + suf + '.jpg'), quality=90)
    print(f'{nome}: {nv} vértices, {ni // 3} triângulos, altura {H} m -> {saida}')


if __name__ == '__main__':
    main()
