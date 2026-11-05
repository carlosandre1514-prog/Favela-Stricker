#!/usr/bin/env python3
"""Molde de skin de uma arma do kit (.glb de uma malha e uma textura): para pintar uma skin que encaixe certinho.

Gera em docs/skins_armas/:
  <arma>_textura_original.png  a textura de cor que a arma usa hoje (é ela que a skin substitui)
  <arma>_molde.png             a mesma textura com o contorno de cada pedaço, colorido pela parte da arma
                               (cano, guarda-mão, corpo, carregador, cabo, coronha) e a legenda
  <arma>_molde_limpo.png       só os contornos e as cores das partes, fundo cinza (bom para mandar para a IA)

Uso: python3 tools/molde_arma.py ak47
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


def ler_glb(p):
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

    prim = js['meshes'][0]['primitives'][0]
    pos = acc(prim['attributes']['POSITION'])
    uv = acc(prim['attributes']['TEXCOORD_0'])
    idx = acc(prim['indices']).reshape(-1, 3).astype(np.int64)
    mat = js['materials'][prim['material']]
    ti = mat['pbrMetallicRoughness']['baseColorTexture']['index']
    img = js['images'][js['textures'][ti]['source']]
    bv = js['bufferViews'][img['bufferView']]
    textura = Image.open(io.BytesIO(binario[bv.get('byteOffset', 0):bv.get('byteOffset', 0) + bv['byteLength']])).convert('RGB')
    return pos, uv, idx, textura


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
    pos, uv, idx, textura = ler_glb(os.path.join(RAIZ, 'shaders', 'kit', 'armas', arma + '.glb'))
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
    tex.save(os.path.join(saida, f'{arma}_textura_original.png'))
    # preenche cada triângulo com a cor da parte (molde limpo) e contorna (molde sobre a textura)
    limpo = Image.new('RGB', (TAM, TAM), (128, 128, 128))
    dl = ImageDraw.Draw(limpo)
    camada = Image.new('RGBA', (TAM, TAM), (0, 0, 0, 0))
    dc = ImageDraw.Draw(camada)
    for tri in idx:
        c = pos[tri].mean(0)
        k = parte_de(c, mn, mx, frente)
        cor = PARTES[k][1]
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
        for nome, cor in PARTES:
            w = int(d.textlength(nome, font=fonte)) + 34
            d.rectangle((x, TAM - 36, x + w, TAM - 6), fill=(20, 20, 24))
            d.rectangle((x + 6, TAM - 28, x + 22, TAM - 14), fill=cor)
            d.text((x + 28, TAM - 34), nome, fill=(240, 240, 240), font=fonte)
            x += w + 6
    molde.save(os.path.join(saida, f'{arma}_molde.png'))
    limpo.save(os.path.join(saida, f'{arma}_molde_limpo.png'))
    print('feito:', saida, '· boca do cano em', '+z' if frente > 0 else '-z', '·', len(idx), 'triângulos')


if __name__ == '__main__':
    main()
