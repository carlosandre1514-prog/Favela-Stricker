#!/usr/bin/env python3
"""Prepara um personagem da Meshy usando o RIG DO PRÓPRIO MESHY (API de rigging) para o esqueleto do jogo.

Entradas:
  <modelo.glb>      o .glb do tools/converter_meshy.cpp (malha, UV e as texturas cor / normal / ORM do jogo)
  <rig.glb>         o mesmo modelo depois do rig automático do Meshy (Character_output.glb: esqueleto de 24 ossos
                    no estilo Mixamo, com os pesos de cada vértice feitos pelo Meshy)
Saída: igual à de tools/preparar_personagem.py (FSP3: as 37 juntas + vértices com 4 ossos + índices, e as texturas).

O que muda em relação ao preparar_personagem.py: os pesos do corpo e as juntas são os do Meshy (não medidos aqui);
os braços descem da pose A para a de repouso do jogo GIRANDO OS OSSOS DO RIG (a malha dobra no ombro como o
Meshy pesou, sem esticar); depois a palma vira de frente (torção espalhada no antebraço) e a mão ganha os ossos
dos dedos (o rig do Meshy não tem dedos).

Uso: python3 tools/preparar_personagem_rig.py <modelo.glb> <rig.glb> <pasta_saida> <nome>
"""
import json
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from preparar_personagem import ALTURA, ler_glb, dist_segmento  # noqa: E402

HIPS, SPINE, CHEST, HEAD, LUP, LFO, LHA, RUP, RFO, RHA, LTH, LSH, LFT, RTH, RSH, RFT, NECK = range(17)
# ossos do Meshy -> ossos do jogo (a clavícula e a parte de cima da coluna ficam no peito)
MAPA = {'Hips': HIPS, 'Spine02': SPINE, 'Spine01': CHEST, 'Spine': CHEST, 'neck': NECK, 'Head': HEAD,
        'head_end': HEAD, 'headfront': HEAD, 'LeftShoulder': CHEST, 'RightShoulder': CHEST,
        'LeftArm': LUP, 'LeftForeArm': LFO, 'LeftHand': LHA, 'RightArm': RUP, 'RightForeArm': RFO, 'RightHand': RHA,
        'LeftUpLeg': LTH, 'LeftLeg': LSH, 'LeftFoot': LFT, 'LeftToeBase': LFT,
        'RightUpLeg': RTH, 'RightLeg': RSH, 'RightFoot': RFT, 'RightToeBase': RFT}


def ler_rig(p):
    d = open(p, 'rb').read()
    l = struct.unpack('<I', d[12:16])[0]
    js = json.loads(d[20:20 + l])
    off = 20 + l + 8
    tipos = {5126: np.float32, 5121: np.uint8, 5123: np.uint16, 5125: np.uint32}
    larg = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}

    def acc(i):
        a = js['accessors'][i]
        bv = js['bufferViews'][a['bufferView']]
        o = off + bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        c = larg[a['type']]
        return np.frombuffer(d, dtype=tipos[a['componentType']], count=a['count'] * c, offset=o).reshape(-1, c).astype(np.float64)

    pr = js['meshes'][0]['primitives'][0]['attributes']
    P = acc(pr['POSITION'])
    Jt = acc(pr['JOINTS_0']).astype(int)
    Wt = acc(pr['WEIGHTS_0'])
    sk = js['skins'][0]
    ibm = acc(sk['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)  # glTF: coluna por coluna
    nomes = [js['nodes'][j]['name'] for j in sk['joints']]
    esc = 1.0
    for n in js['nodes']:  # a armação do Meshy vem em centímetros com escala 0,01
        if 'scale' in n and n.get('children') and any(js['nodes'][c].get('mesh') is not None for c in n['children']):
            esc = n['scale'][0]
    juntas = np.array([np.linalg.inv(m)[:3, 3] for m in ibm])
    return P * esc, Jt, Wt, nomes, juntas * esc


def arc(a, b):
    """Matriz de rotação mínima que leva a direção a até b."""
    a = a / np.linalg.norm(a)
    b = b / np.linalg.norm(b)
    v = np.cross(a, b)
    c = float(a @ b)
    if c > 0.999999:
        return np.eye(3)
    vx = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + vx + vx @ vx / (1 + c)


def main():
    entrada, rig, saida, nome = sys.argv[1:5]
    P, N, U, T, I, imgs = ler_glb(entrada)
    y0 = P[:, 1].min()
    H0 = P[:, 1].max() - y0
    P[:, 1] -= y0
    P *= ALTURA / H0
    H = ALTURA
    P = P.astype(np.float64)
    N = N.astype(np.float64)
    T = T.astype(np.float64)

    # ---- o rig na mesma escala e posição do modelo; cada vértice do modelo pega os pesos do vértice igual no rig
    RP, RJ, RW, nomes, RJpos = ler_rig(rig)
    ry0 = RP[:, 1].min()
    rH = RP[:, 1].max() - ry0
    RP[:, 1] -= ry0
    RJpos[:, 1] -= ry0
    RP *= ALTURA / rH
    RJpos *= ALTURA / rH
    chave = lambda q: [tuple(x) for x in np.round(q / 2e-4).astype(np.int64)]
    tab = {}
    for i, k in enumerate(chave(RP)):
        tab.setdefault(k, i)
    par = np.array([tab.get(k, -1) for k in chave(P)])
    falta = np.where(par < 0)[0]
    for s in range(0, len(falta), 512):  # sem par exato: o mais perto
        q = P[falta[s:s + 512]]
        d2 = ((q[:, None, :] - RP[None, :, :]) ** 2).sum(-1)
        par[falta[s:s + 512]] = d2.argmin(1)
    print(f'  {len(P)} vértices, {len(falta)} sem par exato no rig')
    nj = len(nomes)
    Wm = np.zeros((len(P), nj))
    for k in range(4):
        np.add.at(Wm, (np.arange(len(P)), RJ[par, k]), RW[par, k])
    Wm /= np.maximum(Wm.sum(1, keepdims=True), 1e-9)
    ji = {n: i for i, n in enumerate(nomes)}
    J0 = {n: RJpos[i].copy() for n, i in ji.items()}

    # ---- braços para baixo girando os ossos do rig (skin linear com os pesos do Meshy)
    rot = {n: np.eye(3) for n in nomes}
    piv = {n: J0[n].copy() for n in nomes}     # junta original
    novo = {n: J0[n].copy() for n in nomes}    # junta na pose nova
    baixo = np.array([0.0, -1.0, 0.0])
    for lado in ('Left', 'Right'):
        a, f, m = lado + 'Arm', lado + 'ForeArm', lado + 'Hand'
        Ru = arc(J0[f] - J0[a], baixo)
        novo[f] = J0[a] + Ru @ (J0[f] - J0[a])
        Rf = arc(Ru @ (J0[m] - J0[f]), baixo) @ Ru
        novo[m] = novo[f] + Rf @ (J0[m] - J0[f])
        rot[a], rot[f], rot[m] = Ru, Rf, Rf
    Pn = np.zeros_like(P)
    Nn = np.zeros_like(N)
    Tn = np.zeros_like(T[:, :3])
    for n, i in ji.items():
        w = Wm[:, i:i + 1]
        if not w.any():
            continue
        R = rot[n]
        Pn += w * ((P - piv[n]) @ R.T + novo[n])
        Nn += w * (N @ R.T)
        Tn += w * (T[:, :3] @ R.T)
    P = Pn
    N = Nn / np.maximum(np.linalg.norm(Nn, axis=1, keepdims=True), 1e-9)
    T[:, :3] = Tn / np.maximum(np.linalg.norm(Tn, axis=1, keepdims=True), 1e-9)
    y = P[:, 1]

    # ---- pesos do Meshy nos ossos do jogo (37 colunas: 17 do corpo + 20 dos dedos)
    W = np.zeros((len(P), 37))
    for n, i in ji.items():
        if n in MAPA:
            W[:, MAPA[n]] += Wm[:, i]
    W /= np.maximum(W.sum(1, keepdims=True), 1e-9)

    # ---- juntas do jogo = as do rig (na pose nova)
    J = {'hips': novo['Hips'], 'spine': novo['Spine02'], 'chest': novo['Spine01'], 'neck': novo['neck'], 'head': novo['Head'],
         'lsh': novo['LeftArm'], 'lel': novo['LeftForeArm'], 'lwr': novo['LeftHand'],
         'rsh': novo['RightArm'], 'rel': novo['RightForeArm'], 'rwr': novo['RightHand'],
         'lhi': novo['LeftUpLeg'], 'lkn': novo['LeftLeg'], 'lan': novo['LeftFoot'],
         'rhi': novo['RightUpLeg'], 'rkn': novo['RightLeg'], 'ran': novo['RightFoot']}
    J = {k: np.array(v, dtype=np.float64) for k, v in J.items()}

    # ---- mão: a palma vira de frente (+Z) com a torção espalhada no antebraço; depois os dedos
    medido = {}
    for lado, k, mao_i, ant_i in ((1, 'l', LHA, LFO), (-1, 'r', RHA, RFO)):
        wy, ey = J[k + 'wr'][1], J[k + 'el'][1]
        wc = J[k + 'wr'][[0, 2]]
        mao = W[:, mao_i] > 0.5
        braco = (W[:, mao_i] + W[:, ant_i] + W[:, LUP if lado > 0 else RUP]) > 0.5
        q = P[mao] - P[mao].mean(0)
        n = np.linalg.svd(q, full_matrices=False)[2][2].copy()
        n[1] = 0
        n /= np.linalg.norm(n)
        m_ax = np.array([n[2], 0, -n[0]])
        ponta_y = float(P[mao, 1].min())
        alto = mao & (y > wy - 0.6 * (wy - ponta_y)) & (y < wy - 0.12 * (wy - ponta_y))
        pr = (P[alto] - P[mao].mean(0)) @ m_ax
        if (1.0 if np.percentile(pr, 98) > -np.percentile(pr, 2) else -1.0) != lado:
            n = -n
        medido[lado] = dict(n=n, certeza=abs(np.percentile(pr, 98) + np.percentile(pr, 2)), wy=wy, ey=ey, wc=wc,
                            mao=mao, braco=braco, ponta=ponta_y)
    ne, nd = medido[1]['n'], medido[-1]['n']
    if ne @ np.array([-nd[0], 0, nd[2]]) < 0:
        fraca = 1 if medido[1]['certeza'] < medido[-1]['certeza'] else -1
        medido[fraca]['n'] = -medido[fraca]['n']
    for lado in (1, -1):
        md = medido[lado]
        teta = -np.arctan2(md['n'][0], md['n'][2])
        f = np.where(y < md['wy'], 1.0, np.clip((y - md['ey']) / max(1e-6, md['wy'] - md['ey']), 0, 1))
        f = np.where(md['braco'] & (y < md['ey']), f * f * (3 - 2 * f), 0.0)
        a = teta * f
        c, sn = np.cos(a), np.sin(a)
        wc = md['wc']
        rx_, rz_ = P[:, 0] - wc[0], P[:, 2] - wc[1]
        P[:, 0] = np.where(f > 0, wc[0] + rx_ * c + rz_ * sn, P[:, 0])
        P[:, 2] = np.where(f > 0, wc[1] - rx_ * sn + rz_ * c, P[:, 2])
        for V in (N, T):
            vx, vz = V[:, 0].copy(), V[:, 2].copy()
            V[:, 0] = np.where(f > 0, vx * c + vz * sn, vx)
            V[:, 2] = np.where(f > 0, -vx * sn + vz * c, vz)
        q = P[md['mao']]
        md['centro'] = q.mean(0)
        md['largura'] = float(np.percentile(q[:, 0], 97) - np.percentile(q[:, 0], 3))
        print(f'  mão {"esquerda" if lado > 0 else "direita"}: palma girada {np.degrees(teta):.0f}°, mão de {md["wy"] - md["ponta"]:.3f} m')

    for lado, k in ((1, 'l'), (-1, 'r')):
        md = medido[lado]
        wr = np.array([md['centro'][0], md['wy'], md['centro'][2]])
        esc = (md['wy'] - md['ponta']) / (0.1095 * H)
        lx = np.clip(md['largura'] / (0.05 * H), 0.7, 1.4)
        J[k + 'polegar1'] = wr + np.array([lado * 0.022 * H * lx, -0.015 * H * esc, 0.012 * H])
        J[k + 'polegar2'] = wr + np.array([lado * 0.03 * H * lx, -0.036 * H * esc, 0.02 * H])
        for nd_, fx in (('indicador', 0.018), ('medio', 0.006), ('anelar', -0.006), ('minimo', -0.018)):
            J[k + nd_ + '1'] = wr + np.array([lado * fx * H * lx, -0.0575 * H * esc, 0.0])
            J[k + nd_ + '2'] = J[k + nd_ + '1'] + np.array([0, -(0.02 if nd_ == 'minimo' else 0.026) * H * esc, 0])

    # dedos: o peso da mão (do Meshy) se divide entre a palma e os ossos dos dedos, pela distância até cada osso
    for lado, k, mao_i in ((1, 'l', LHA), (-1, 'r', RHA)):
        md = medido[lado]
        base = 17 if lado > 0 else 27
        segs = [(mao_i, J[k + 'wr'], (J[k + 'indicador1'] + J[k + 'minimo1']) * 0.5)]
        for i, nd_ in enumerate(('polegar', 'indicador', 'medio', 'anelar', 'minimo')):
            j1, j2 = J[k + nd_ + '1'], J[k + nd_ + '2']
            dd = (j2 - j1) / max(1e-6, np.linalg.norm(j2 - j1))
            comp = (0.022 if nd_ == 'polegar' else 0.018 if nd_ == 'minimo' else 0.024) * H * (md['wy'] - md['ponta']) / (0.1095 * H)
            segs += [(base + 2 * i, j1, j2), (base + 2 * i + 1, j2, j2 + dd * comp)]
        sel = np.where(W[:, mao_i] > 0)[0]
        q = P[sel]
        D = np.stack([dist_segmento(q, a, b) for _, a, b in segs], 1)
        abaixo = q[:, 1] < J[k + 'medio1'][1] + 0.012 * H
        for c_, (o, _, _) in enumerate(segs):
            if o != mao_i and o not in (base, base + 1):
                D[:, c_] = np.where(abaixo, D[:, c_], 1e9)
        ordem = np.argsort(D, 1)[:, :2]
        dd = np.take_along_axis(D, ordem, 1)
        wv = 1.0 / (dd ** 4 + (0.006 * H) ** 4)
        wv = np.where(dd > 1e8, 0, wv)
        wv /= wv.sum(1, keepdims=True)
        wm = W[sel, mao_i].copy()
        W[sel, mao_i] = 0
        ids = np.array([o for o, _, _ in segs])
        for c_ in range(2):
            np.add.at(W, (sel, ids[ordem[:, c_]]), wm * wv[:, c_])

    # ---- 4 ossos por vértice
    jo = np.argsort(-W, axis=1)[:, :4]
    wv = np.take_along_axis(W, jo, axis=1)
    wv[wv < 0.01] = 0
    wv /= np.maximum(wv.sum(1, keepdims=True), 1e-9)
    print('  juntas: ombro x %.3f y %.3f, cotovelo y %.3f, pulso y %.3f, quadril y %.3f, joelho y %.3f' %
          (J['lsh'][0], J['lsh'][1], J['lel'][1], J['lwr'][1], J['hips'][1], J['lkn'][1]))

    os.makedirs(saida, exist_ok=True)
    nv, ni = len(P), len(I)
    with open(os.path.join(saida, nome + '.bin'), 'wb') as f:
        f.write(b'FSP3')
        f.write(struct.pack('<II', nv, ni))
        ordem_juntas = ['hips', 'spine', 'chest', 'head', 'lsh', 'lel', 'lwr', 'rsh', 'rel', 'rwr', 'lhi', 'lkn', 'lan', 'rhi', 'rkn', 'ran', 'neck']
        ordem_juntas += [k + d + n for k in 'lr' for d in ('polegar', 'indicador', 'medio', 'anelar', 'minimo') for n in '12']
        f.write(np.array([J[k] for k in ordem_juntas], dtype='<f4').tobytes())
        reg = np.zeros(nv, dtype=[('p', '<f4', 3), ('n', '<f4', 3), ('u', '<f4', 2), ('t', '<f4', 4), ('j', '<u2', 4), ('w', '<f4', 4)])
        reg['p'], reg['n'], reg['u'], reg['t'] = P, N, U, T
        reg['j'], reg['w'] = jo.astype(np.uint16), wv.astype(np.float32)
        f.write(reg.tobytes())
        f.write(I.astype('<u4').tobytes())
    for im, suf in zip(imgs[:3], ['', '_normal', '_orm']):
        im.save(os.path.join(saida, nome + suf + '.jpg'), quality=90)
    print(f'{nome}: {nv} vértices, {ni // 3} triângulos (rig do Meshy) -> {saida}')


if __name__ == '__main__':
    main()
