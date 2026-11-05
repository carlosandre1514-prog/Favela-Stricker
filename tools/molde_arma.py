#!/usr/bin/env python3
"""Molde de skin de uma arma do kit (.glb de uma malha e uma textura): para pintar uma skin que encaixe certinho.

Gera em docs/skins_armas/:
  <arma>_textura_original.png  a textura de cor que a arma usa hoje (é ela que a skin substitui)
  <arma>_molde.png             a mesma textura com o contorno de cada pedaço, colorido pela parte da arma
                               (cano, guarda-mão, corpo, carregador, cabo, coronha) e a legenda
  <arma>_molde_limpo.png       só os contornos e as cores das partes, fundo cinza (bom para mandar para a IA)

Uso: python3 tools/molde_arma.py ak47   (arma com várias texturas: um molde por peça, ex.: m4colt_visier_molde.png)
"""
import io
import json
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
TAM = 1024
# cor de cada parte (RGB) e nome
PARTES = [('cano', (230, 60, 60)), ('guarda-mão', (245, 160, 40)), ('corpo', (60, 170, 240)),
          ('carregador', (170, 90, 230)), ('cabo', (60, 200, 110)), ('coronha', (240, 220, 60))]


def ler_glb(p, material=None, extra=False):
    """material: índice do material (armas com várias texturas, ex.: corpo, carregador e luneta); None = o primeiro."""
    d = open(p, 'rb').read()
    l = struct.unpack('<I', d[12:16])[0]
    js = json.loads(d[20:20 + l])
    off = 20 + l
    bl = struct.unpack('<I', d[off:off + 4])[0]
    binario = d[off + 8:off + 8 + bl]

    def acc(i):
        a = js['accessors'][i]
        bv = js['bufferViews'][a['bufferView']]
        o = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        n = a['count']
        comp = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        tipo = {5126: '<f4', 5125: '<u4', 5123: '<u2', 5121: 'u1'}[a['componentType']]
        st = bv.get('byteStride', 0) or np.dtype(tipo).itemsize * comp
        raw = np.frombuffer(binario, dtype=np.uint8, count=st * (n - 1) + np.dtype(tipo).itemsize * comp, offset=o)
        return np.lib.stride_tricks.as_strided(raw.view(tipo), shape=(n, comp), strides=(st, np.dtype(tipo).itemsize)).copy()

    # todas as malhas da cena, com as transformações dos nós (algumas armas vêm em várias peças e giradas na raiz);
    # só as partes com o material principal (o da textura que a skin troca)
    def mat_no(n):
        if 'matrix' in n:
            return np.array(n['matrix'], float).reshape(4, 4).T
        m = np.eye(4)
        if 'scale' in n:
            m = np.diag(list(n['scale']) + [1.0]) @ m
        if 'rotation' in n:
            x, y, z, w = n['rotation']
            r = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                          [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                          [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
            rm = np.eye(4)
            rm[:3, :3] = r
            m = rm @ m
        if 'translation' in n:
            t = np.eye(4)
            t[:3, 3] = n['translation']
            m = t @ m
        return m

    P, U, I, N, mat_principal = [], [], [], [], material
    base = 0

    def visita(ni, pai):
        nonlocal base, mat_principal
        n = js['nodes'][ni]
        m = pai @ mat_no(n)
        if 'mesh' in n:
            for prim in js['meshes'][n['mesh']]['primitives']:
                if mat_principal is None:
                    mat_principal = prim.get('material', 0)
                if prim.get('material', 0) != mat_principal:
                    continue
                pp = acc(prim['attributes']['POSITION']).astype(float)
                pp = (np.c_[pp, np.ones(len(pp))] @ m.T)[:, :3]
                P.append(pp)
                if 'NORMAL' in prim['attributes']:
                    nn = acc(prim['attributes']['NORMAL']).astype(float) @ m[:3, :3].T
                    N.append(nn / np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-9))
                else:
                    N.append(np.zeros_like(pp))
                U.append(acc(prim['attributes']['TEXCOORD_0']))
                I.append(acc(prim['indices']).reshape(-1, 3).astype(np.int64) + base)
                base += len(pp)
        for c in n.get('children', []):
            visita(c, m)

    cena = js.get('scenes', [{'nodes': [0]}])[js.get('scene', 0)]
    for r in cena['nodes']:
        visita(r, np.eye(4))
    pos, uv, idx = np.concatenate(P), np.concatenate(U), np.concatenate(I)
    mat = js['materials'][mat_principal]
    ti = mat['pbrMetallicRoughness']['baseColorTexture']['index']
    img = js['images'][js['textures'][ti]['source']]
    bv = js['bufferViews'][img['bufferView']]
    textura = Image.open(io.BytesIO(binario[bv.get('byteOffset', 0):bv.get('byteOffset', 0) + bv['byteLength']])).convert('RGB')
    if not extra:
        return pos, uv, idx, textura
    # também as normais e o mapa de metal/rugosidade original (G = rugosidade, B = metal), se tiver
    orm = None
    mr = mat['pbrMetallicRoughness'].get('metallicRoughnessTexture')
    if mr:
        im2 = js['images'][js['textures'][mr['index']]['source']]
        bv2 = js['bufferViews'][im2['bufferView']]
        orm = Image.open(io.BytesIO(binario[bv2.get('byteOffset', 0):bv2.get('byteOffset', 0) + bv2['byteLength']])).convert('RGB')
    return pos, uv, idx, textura, np.concatenate(N), orm


def materiais_glb(p):
    """Nomes dos materiais com textura de cor (cada um é uma peça com o seu molde)."""
    d = open(p, 'rb').read()
    l = struct.unpack('<I', d[12:16])[0]
    js = json.loads(d[20:20 + l])
    return [(i, m.get('name', str(i))) for i, m in enumerate(js.get('materials', []))
            if 'baseColorTexture' in m.get('pbrMetallicRoughness', {})]


def parte_de(c, mn, mx, frente):
    """Parte da arma pelo centro do triângulo: ao longo do comprimento (z) e da altura (y)."""
    t = (c[2] - mn[2]) / (mx[2] - mn[2])
    if frente < 0: t = 1 - t                    # t = 0 na coronha, 1 na boca do cano
    h = (c[1] - mn[1]) / (mx[1] - mn[1])        # 0 embaixo, 1 em cima
    if t > 0.78: return 0                        # cano
    if t > 0.55: return 1 if h > 0.45 else 3     # guarda-mão (embaixo: carregador)
    if t > 0.30:
        if h < 0.42: return 3 if t > 0.40 else 4  # carregador / cabo
        return 2                                  # corpo
    return 5                                      # coronha


def main():
    arma = sys.argv[1] if len(sys.argv) > 1 else 'ak47'
    glb = os.path.join(RAIZ, 'shaders', 'kit', 'armas', arma + '.glb')
    mats = materiais_glb(glb)
    # arma de uma textura: <arma>_molde.png; de várias (ex.: M4 Colt: corpo, carregador, luneta): <arma>_<peça>_molde.png
    for mi, nome in (mats if len(mats) > 1 else [(None, '')]):
        molde_de(arma, glb, mi, nome.lower())


def molde_de(arma, glb, mi, peca):
    pos, uv, idx, textura = ler_glb(glb, mi)
    base = arma + ('_' + peca if peca else '')
    mn, mx = pos.min(0), pos.max(0)
    # de que lado está a boca do cano: a ponta mais fina (menos altura) do comprimento
    zz = pos[:, 2]
    fat = (mx[2] - mn[2]) * 0.08
    alt_mais = np.ptp(pos[zz > mx[2] - fat, 1]) if (zz > mx[2] - fat).any() else 1
    alt_menos = np.ptp(pos[zz < mn[2] + fat, 1]) if (zz < mn[2] + fat).any() else 1
    frente = 1 if alt_mais < alt_menos else -1
    saida = os.path.join(RAIZ, 'docs', 'skins_armas')
    os.makedirs(saida, exist_ok=True)
    tex = textura.resize((TAM, TAM), Image.LANCZOS)
    tex.save(os.path.join(saida, f'{base}_textura_original.png'))
    # preenche cada triângulo com a cor da parte (molde limpo) e contorna (molde sobre a textura)
    limpo = Image.new('RGB', (TAM, TAM), (128, 128, 128))
    dl = ImageDraw.Draw(limpo)
    camada = Image.new('RGBA', (TAM, TAM), (0, 0, 0, 0))
    dc = ImageDraw.Draw(camada)
    # peça separada (carregador, luneta): uma cor só, com o nome da peça na legenda
    NOMES_PECA = {'magazin': 'carregador', 'visier': 'luneta', 'scope': 'luneta', 'mag': 'carregador'}
    so_uma = NOMES_PECA.get(peca) if peca else None
    legenda = [(so_uma, (170, 90, 230) if so_uma == 'carregador' else (60, 200, 220))] if so_uma else PARTES
    for tri in idx:
        c = pos[tri].mean(0)
        cor = legenda[0][1] if so_uma else PARTES[parte_de(c, mn, mx, frente)][1]
        pts = [(float(u % 1.0) * TAM, float(v % 1.0) * TAM) for u, v in uv[tri]]
        dl.polygon(pts, fill=cor)
        dc.polygon(pts, fill=cor + (70,), outline=cor + (230,))
    molde = Image.alpha_composite(tex.convert('RGBA'), camada).convert('RGB')
    # legenda
    for img in (molde, limpo):
        d = ImageDraw.Draw(img)
        try:
            fonte = ImageFont.truetype('DejaVuSans-Bold.ttf', 22)
        except OSError:
            fonte = ImageFont.load_default()
        x = 10
        for nome, cor in legenda:
            w = int(d.textlength(nome, font=fonte)) + 34
            d.rectangle((x, TAM - 36, x + w, TAM - 6), fill=(20, 20, 24))
            d.rectangle((x + 6, TAM - 28, x + 22, TAM - 14), fill=cor)
            d.text((x + 28, TAM - 34), nome, fill=(240, 240, 240), font=fonte)
            x += w + 6
    molde.save(os.path.join(saida, f'{base}_molde.png'))
    limpo.save(os.path.join(saida, f'{base}_molde_limpo.png'))
    print('feito:', base, '· boca do cano em', '+z' if frente > 0 else '-z', '·', len(idx), 'triângulos')


if __name__ == '__main__':
    main()
