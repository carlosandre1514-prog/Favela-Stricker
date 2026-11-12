#!/usr/bin/env python3
"""Mede onde cada acessório encaixa em cada arma (.glb do kit) e grava shaders/kit/armas/montagem.json.

Uso: python3 tools/medir_montagem_armas.py [--perfis pasta]   (--perfis: desenha o perfil de cada arma com a grade)

Os pontos ficam no espaço do arquivo (o mesmo dos vértices, com as transformações dos nós), e o motor leva para o
espaço da arma com a mesma conta que posiciona o modelo (Engine.cpp: spawnWeaponModel, matriz fix). Medidas:
  boca     ponta do cano (centro) e o raio do cano ali          -> silenciador e freio de boca
  trilho   ponto de apoio da mira em cima da caixa da arma       -> mira holográfica e luneta
  pente    fundo do pente (centro), largura, profundidade e para onde ele desce   -> pente alongado
  coronha  centro da soleira da coronha e a altura dela          -> coronha (almofada)
A posição do trilho (de 0 = coronha a 1 = boca) de cada arma foi escolhida olhando os perfis (TRILHO abaixo).
"""
import glob
import json
import os
import struct
import sys

import numpy as np

PASTA = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'shaders', 'kit', 'armas')

# onde a mira vai em cima da arma (fração do comprimento, da coronha para a boca) e o comprimento da janela que
# conta a altura (a mira fica acima do ponto mais alto nela); escolhido olhando os perfis de cada arma
TRILHO = {
    'm4a1': 0.47, 'm4colt': 0.47, 'ak47': 0.47, 'ak74': 0.47, 'ak74_tatica': 0.47, 'smg': 0.50, 'doze': 0.44,
    'tt33': 0.55, 'ak47_cosmica': 0.47, 'm4a1_dragao_raio': 0.47, 'm4a1_mitica': 0.47,
}
# onde procurar o pente (fração do comprimento) e se a arma tem pente
PENTE = {'m4a1': (0.40, 0.62), 'm4colt': (0.40, 0.62), 'ak47': (0.40, 0.62), 'ak74': (0.40, 0.62), 'ak74_tatica': (0.40, 0.62),
         'smg': (0.40, 0.66), 'tt33': (0.05, 0.40), 'ak47_cosmica': (0.40, 0.62), 'm4a1_dragao_raio': (0.40, 0.62), 'm4a1_mitica': (0.40, 0.62)}


def quat_mat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def matriz_no(n):
    if 'matrix' in n:
        return np.array(n['matrix'], dtype=np.float64).reshape(4, 4).T
    m = np.eye(4)
    s = np.diag(list(n.get('scale', [1, 1, 1])) + [1])
    r = np.eye(4)
    if 'rotation' in n:
        r[:3, :3] = quat_mat(n['rotation'])
    t = np.eye(4)
    t[:3, 3] = n.get('translation', [0, 0, 0])
    return t @ r @ s @ m


def vertices(caminho):
    b = open(caminho, 'rb').read()
    ln = struct.unpack('<I', b[12:16])[0]
    j = json.loads(b[20:20 + ln])
    binario = b[20 + ln + 8:]
    pts = []

    def ler(ai):
        a = j['accessors'][ai]
        bv = j['bufferViews'][a['bufferView']]
        n = a['count']
        off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        passo = bv.get('byteStride', 12)
        if passo == 12:
            return np.frombuffer(binario, '<f4', n * 3, off).reshape(n, 3).astype(np.float64)
        return np.array([np.frombuffer(binario, '<f4', 3, off + k * passo) for k in range(n)], dtype=np.float64)

    def visita(i, pai):
        n = j['nodes'][i]
        m = pai @ matriz_no(n)
        if 'mesh' in n:
            for p in j['meshes'][n['mesh']]['primitives']:
                v = ler(p['attributes']['POSITION'])
                pts.append(v @ m[:3, :3].T + m[:3, 3])
        for c in n.get('children', []):
            visita(c, m)

    cena = j['scenes'][j.get('scene', 0)]['nodes'] if 'scenes' in j else range(len(j['nodes']))
    for i in cena:
        visita(i, np.eye(4))
    return np.concatenate(pts)


def para_r(P):
    """Como o motor deita a arma: o lado mais comprido vira Z (Engine.cpp: R). Devolve os pontos e a inversa."""
    ext = P.max(0) - P.min(0)
    if ext[0] > ext[2]:  # glm::rotate(-90°, Y): (x, y, z) -> (-z, y, x)
        return np.stack([-P[:, 2], P[:, 1], P[:, 0]], 1), lambda q: np.array([q[2], q[1], -q[0]])
    return P.copy(), lambda q: np.array(q)


def medir(chave, P):
    Q, volta = para_r(P)
    mn, mx = Q.min(0), Q.max(0)
    comp = mx[2] - mn[2]
    alt = mx[1] - mn[1]
    out = {}
    # boca: os vértices do último 1,5% do comprimento
    sel = Q[Q[:, 2] > mx[2] - 0.015 * comp]
    c = (sel[:, :2].min(0) + sel[:, :2].max(0)) / 2
    raio = float(np.sqrt(((sel[:, :2] - c) ** 2).sum(1)).max())
    out['boca'] = volta([c[0], c[1], mx[2]]).tolist()
    out['boca_raio'] = raio
    # trilho: o ponto mais alto numa janela de 14% do comprimento em volta da posição escolhida
    f = TRILHO.get(chave, 0.47)
    zc = mn[2] + comp * f
    jan = Q[np.abs(Q[:, 2] - zc) < comp * 0.07]
    topo = float(jan[:, 1].max()) if len(jan) else float(mx[1])
    xc = float((jan[:, 0].min() + jan[:, 0].max()) / 2) if len(jan) else float((mn[0] + mx[0]) / 2)
    out['trilho'] = volta([xc, topo, zc]).tolist()
    # folga: o ponto mais alto atrás da mira (entre ela e o olho: alça de mira, alavanca...); a linha da mira tem que
    # passar por cima dele (o motor põe um calço embaixo da mira se precisar)
    atras = Q[(Q[:, 2] > mn[2] + comp * max(0.12, f - 0.3)) & (Q[:, 2] < zc - comp * 0.07)]
    folga = float(atras[:, 1].max()) if len(atras) else topo
    out['folga'] = volta([xc, max(folga, topo), zc]).tolist()
    # pente: o ponto mais baixo na faixa do pente; a direção é do centro 4 cm acima até o fundo
    if chave in PENTE:
        a, b2 = PENTE[chave]
        faixa = Q[(Q[:, 2] > mn[2] + comp * a) & (Q[:, 2] < mn[2] + comp * b2)]
        fundo_y = faixa[:, 1].min()
        perto = faixa[faixa[:, 1] < fundo_y + alt * 0.06]
        acima = faixa[(faixa[:, 1] > fundo_y + alt * 0.12) & (faixa[:, 1] < fundo_y + alt * 0.2)]
        cf = (perto.min(0) + perto.max(0)) / 2
        cf[1] = fundo_y
        d = np.array([0.0, -1.0, 0.0])
        if len(acima):
            ca = (acima.min(0) + acima.max(0)) / 2
            v = cf - ca
            if np.linalg.norm(v) > 1e-6:
                d = v / np.linalg.norm(v)
        out['pente'] = volta(cf.tolist()).tolist()
        out['pente_dir'] = volta(d.tolist()).tolist()
        out['pente_larg'] = float(perto[:, 0].max() - perto[:, 0].min())
        out['pente_prof'] = float(perto[:, 2].max() - perto[:, 2].min())
    # coronha: os vértices do primeiro 1,5% do comprimento
    sel = Q[Q[:, 2] < mn[2] + 0.015 * comp]
    cc = (sel.min(0) + sel.max(0)) / 2
    cc[2] = mn[2]
    out['coronha'] = volta(cc.tolist()).tolist()
    out['coronha_alt'] = float(sel[:, 1].max() - sel[:, 1].min())
    out['coronha_larg'] = float(sel[:, 0].max() - sel[:, 0].min())
    return out


def perfil(chave, P, saida):
    from PIL import Image, ImageDraw
    Q, _ = para_r(P)
    mn, mx = Q.min(0), Q.max(0)
    W, H = 1400, int(1400 * (mx[1] - mn[1]) / (mx[2] - mn[2])) + 60
    img = Image.new('RGB', (W, H), 'white')
    d = ImageDraw.Draw(img)
    amostra = Q[:: max(1, len(Q) // 60000)]
    for x, y, z in amostra:
        u = int((z - mn[2]) / (mx[2] - mn[2]) * (W - 40)) + 20
        v = int(H - 30 - (y - mn[1]) / (mx[1] - mn[1]) * (H - 60))
        d.point((u, v), fill='black')
    for k in range(21):
        u = int(k / 20 * (W - 40)) + 20
        d.line([(u, 0), (u, H)], fill=(255, 120, 120))
        d.text((u + 2, 2), f'{k / 20:.2f}', fill='red')
    img.save(os.path.join(saida, f'perfil_{chave}.png'))


def main():
    perfis = sys.argv[sys.argv.index('--perfis') + 1] if '--perfis' in sys.argv else None
    tabela = {}
    for f in sorted(glob.glob(os.path.join(PASTA, '*.glb'))):
        nome = os.path.basename(f)[:-4]
        if nome in ('granada',) or nome.endswith('_alta'):
            continue
        chave = nome.replace('_m_alta', '')
        P = vertices(f)
        tabela[nome] = medir(chave, P)
        if perfis:
            perfil(nome, P, perfis)
        print(nome, {k: (round(v, 4) if isinstance(v, float) else [round(x, 4) for x in v]) for k, v in tabela[nome].items()})
    json.dump(tabela, open(os.path.join(PASTA, 'montagem.json'), 'w'), indent=1)
    print('montagem.json:', len(tabela), 'armas')


if __name__ == '__main__':
    main()
