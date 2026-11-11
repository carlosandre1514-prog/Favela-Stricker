#!/usr/bin/env python3
"""Prepara um personagem da Meshy (sem esqueleto) para o esqueleto dos personagens do jogo.

Entrada: o .glb que o tools/converter_meshy.cpp gera para o personagem (sem --arma: pés no chão, de frente para +Z,
uma malha, texturas cor / normal / ORM embutidas). A Meshy entrega em pose A (braços abertos para baixo).
Saída (shaders/kit/personagens/meshy/):
  <nome>.bin           malha já com os ossos: os braços descem para a pose de repouso do esqueleto do jogo (braços
                       retos ao lado do corpo, motor/procgen/Character.cpp), altura ALTURA, e cada vértice preso a até
                       4 ossos (pesos pela distância até cada osso, só os ossos da região certa: braço, perna ou tronco)
  <nome>.jpg, <nome>_normal.jpg, <nome>_orm.jpg   as texturas
Formato do .bin: "FSP2", nv, ni (uint32); as 17 juntas do esqueleto medidas NESTE modelo (17 x 3 floats, na ordem de
CharJoint + pescoço): o jogo monta o esqueleto e as animações nelas, então cada osso dobra onde o corpo do modelo
dobra; por vértice pos(3f) normal(3f) uv(2f) tangente(4f) ossos(4 uint16) pesos(4f); depois os índices (uint32).

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

    # ---- braços: da pose A da Meshy para a pose de repouso do jogo (retos para baixo, no ombro do esqueleto)
    # ombro: medido no tronco do modelo (a parte de fora do tronco na altura do peito, menos o deltoide)
    # (logo abaixo da axila, onde o braço da pose A já saiu do tronco: o que sobra é peito e costas)
    tronco_alto = (P[:, 1] > 0.69 * H) & (P[:, 1] < 0.73 * H) & (np.abs(P[:, 0]) < 0.15 * H)
    piv_x = float(np.percentile(np.abs(P[tronco_alto, 0]), 97)) + 0.005 * H
    piv_y = 0.81 * H
    ponta = {}
    # o que é braço sai da pose A (lá o braço está longe do corpo): só esses vértices pegam os ossos do braço.
    # (com os braços já abaixados, a mão fica ao lado do quadril e o quadril pegava o osso do antebraço: a malha
    # esticava entre a mão e a perna nas animações)
    eh_braco = {}
    ao_longo = {}
    for lado in (1, -1):
        s = P[:, 0] * lado
        mao = np.argmax(s)                                     # na pose A, a ponta da mão é o ponto mais de fora
        ponta[lado] = float(np.linalg.norm((P[mao] - np.array([lado * piv_x, piv_y, P[mao][2]]))[:2]))
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
        print(f'  braço {"esquerdo" if lado > 0 else "direito"}: {np.degrees(ang):.0f}° da vertical, {int(braco.sum())} vértices')

    # ---- juntas medidas neste modelo: ombro no pivô; cotovelo e pulso pelas proporções do braço (ombro-cotovelo
    # 0,186 H, cotovelo-pulso 0,146 H e pulso-ponta dos dedos 0,108 H numa pessoa), em escala com o braço medido;
    # pernas no centro de cada perna na altura do quadril, do joelho e do tornozelo; coluna no meio do tronco
    def centro(cond):
        q = P[cond]
        return q.mean(axis=0) if len(q) else np.zeros(3)
    x, y = P[:, 0], P[:, 1]
    J = {}
    for lado, k in ((1, 'l'), (-1, 'r')):
        L = ponta[lado] / 0.44
        bz = float(centro(eh_braco[lado])[2])
        J[k + 'sh'] = np.array([lado * piv_x, piv_y, bz])
        J[k + 'el'] = np.array([lado * piv_x, piv_y - 0.186 * L, bz])
        J[k + 'wr'] = np.array([lado * piv_x, piv_y - 0.332 * L, bz])
        for jn, yy in (('hi', 0.52 * H), ('kn', 0.285 * H), ('an', 0.06 * H)):
            faixa = (np.abs(y - yy) < 0.02 * H) & (x * lado > 0.005 * H) & ~eh_braco[1] & ~eh_braco[-1]
            c = centro(faixa)
            J[k + jn] = np.array([c[0], 0.045 * H if jn == 'an' else yy, c[2]])
    for jn, yy in (('hips', 0.53), ('spine', 0.62), ('chest', 0.72), ('neck', 0.835), ('head', 0.865)):
        faixa = (np.abs(y - yy * H) < 0.02 * H) & (np.abs(x) < 0.12 * H) & ~eh_braco[1] & ~eh_braco[-1]
        J[jn] = np.array([0.0, yy * H, float(centro(faixa)[2])])
    for k in ('lhi', 'rhi'):  # quadril um pouco para dentro do centro da coxa (a junta fica dentro da bacia)
        J[k][0] *= 0.85
    print('  juntas: ombro x %.3f, cotovelo y %.3f, pulso y %.3f, quadril x %.3f, joelho x %.3f' %
          (J['lsh'][0], J['lel'][1], J['lwr'][1], J['lhi'][0], J['lkn'][0]))

    # ---- pesos dos ossos: distância até cada osso, só os ossos da região do vértice
    ossos = [  # (osso, a, b)
        (HIPS, J['hips'] - np.array([0, 0.08 * H, 0]), (J['hips'] + J['spine']) * 0.5),
        (SPINE, (J['hips'] + J['spine']) * 0.5, (J['spine'] + J['chest']) * 0.5),
        (CHEST, (J['spine'] + J['chest']) * 0.5, J['neck'] - np.array([0, 0.035 * H, 0])),
        (NECK, J['neck'] - np.array([0, 0.035 * H, 0]), J['head']),
        (HEAD, J['head'], J['head'] + np.array([0, 0.135 * H, 0])),
        (LUP, J['lsh'], J['lel']), (LFO, J['lel'], J['lwr']), (LHA, J['lwr'], J['lwr'] - np.array([0, 0.1 * H, 0])),
        (RUP, J['rsh'], J['rel']), (RFO, J['rel'], J['rwr']), (RHA, J['rwr'], J['rwr'] - np.array([0, 0.1 * H, 0])),
        (LTH, J['lhi'], J['lkn']), (LSH, J['lkn'], J['lan']), (LFT, J['lan'], J['lan'] + np.array([0, -0.03 * H, 0.08 * H])),
        (RTH, J['rhi'], J['rkn']), (RSH, J['rkn'], J['ran']), (RFT, J['ran'], J['ran'] + np.array([0, -0.03 * H, 0.08 * H])),
    ]
    D = np.stack([dist_segmento(P, a, b) for _, a, b in ossos], axis=1)
    idx = np.array([o for o, _, _ in ossos])
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
    # (só a capa do ombro, por cima e por fora do pivô: o peito e as costas ficam no tronco)
    capa_e = ~qualquer_braco & (y > 0.77 * H) & (x > piv_x * 0.82)
    capa_d = ~qualquer_braco & (y > 0.77 * H) & (x < -piv_x * 0.82)
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
    jo = idx[ordem]

    # ---- suavização dos pesos como no MiniBlender (smoothWeights): cada vértice vira metade o seu peso, metade a
    # média dos vizinhos na malha. Tira as "costuras" duras de peso (onde a malha estica ou afina ao dobrar).
    # Vizinhança pela posição (os vértices repetidos na costura da UV contam como um só, senão a malha abre).
    nv0 = len(P)
    W = np.zeros((nv0, 17), np.float64)
    np.add.at(W, (np.repeat(np.arange(nv0), 3), jo.ravel()), wv.ravel())
    _, sold = np.unique(np.round(P / 1e-5).astype(np.int64), axis=0, return_inverse=True)
    sold = sold.ravel()
    ns = sold.max() + 1
    tri = sold[I.reshape(-1, 3)]
    a = np.concatenate([tri[:, 0], tri[:, 1], tri[:, 2], tri[:, 1], tri[:, 2], tri[:, 0]])
    b = np.concatenate([tri[:, 1], tri[:, 2], tri[:, 0], tri[:, 0], tri[:, 1], tri[:, 2]])
    par = np.unique(np.stack([a, b], 1)[a != b], axis=0)
    grau = np.bincount(par[:, 0], minlength=ns).astype(np.float64)
    Ws = np.zeros((ns, 17)); np.add.at(Ws, sold, W)
    Ws /= np.maximum(Ws.sum(1, keepdims=True), 1e-9)
    pode_s = np.zeros((ns, 17), bool)
    pode_full = np.zeros((nv0, 17), bool); pode_full[:, idx] = pode
    np.logical_or.at(pode_s, sold, pode_full)
    for _ in range(3):
        viz = np.zeros_like(Ws); np.add.at(viz, par[:, 0], Ws[par[:, 1]])
        media = viz / np.maximum(grau, 1)[:, None]
        Ws = np.where(grau[:, None] > 0, 0.5 * Ws + 0.5 * media, Ws)
        Ws = np.where(pode_s, Ws, 0)                      # nunca um osso fora da região (o braço não pega o quadril)
        Ws[Ws < 0.004] = 0
        Ws /= np.maximum(Ws.sum(1, keepdims=True), 1e-9)
    W = Ws[sold]
    jo = np.argsort(-W, axis=1)[:, :4]
    wv = np.take_along_axis(W, jo, axis=1)
    wv[wv < 0.02] = 0
    wv /= np.maximum(wv.sum(1, keepdims=True), 1e-9)
    print(f'  pesos suavizados (3 passadas), {int((wv > 0).sum(1).mean() * 100) / 100} ossos por vértice em média')

    os.makedirs(saida, exist_ok=True)
    nv, ni = len(P), len(I)
    with open(os.path.join(saida, nome + '.bin'), 'wb') as f:
        f.write(b'FSP2')
        f.write(struct.pack('<II', nv, ni))
        ordem_juntas = ['hips', 'spine', 'chest', 'head', 'lsh', 'lel', 'lwr', 'rsh', 'rel', 'rwr', 'lhi', 'lkn', 'lan', 'rhi', 'rkn', 'ran', 'neck']
        f.write(np.array([J[k] for k in ordem_juntas], dtype='<f4').tobytes())
        reg = np.zeros(nv, dtype=[('p', '<f4', 3), ('n', '<f4', 3), ('u', '<f4', 2), ('t', '<f4', 4), ('j', '<u2', 4), ('w', '<f4', 4)])
        reg['p'], reg['n'], reg['u'], reg['t'] = P, N, U, T
        j4 = jo.astype(np.uint16)
        w4 = wv.astype(np.float32)
        reg['j'], reg['w'] = j4, w4
        f.write(reg.tobytes())
        f.write(I.astype('<u4').tobytes())
    nomes = ['', '_normal', '_orm']
    for im, suf in zip(imgs[:3], nomes):
        im.save(os.path.join(saida, nome + suf + '.jpg'), quality=90)
    print(f'{nome}: {nv} vértices, {ni // 3} triângulos, altura {H} m -> {saida}')


if __name__ == '__main__':
    main()
