#!/usr/bin/env python3
"""Skin de arma a partir de uma imagem de referência (a arma de lado): projeta a foto sobre o modelo 3D.

Cada parte da arma (coronha, cabo, corpo, carregador, guarda-mão, cano) é ligada a um retângulo da foto; cada
pixel da textura da arma descobre onde fica no 3D (pelo mapeamento UV) e pega a cor do ponto correspondente na
foto, vista de lado. Assim a skin sai como na referência, sem depender de uma IA pintar o molde certinho.

A receita (JSON) diz, para cada parte, o pedaço do modelo (t = 0 na coronha até 1 na boca do cano; y em metros
do modelo) e o retângulo da foto:
  {"partes": {"coronha": {"t": [0, 0.21], "y": [-0.043, 0.082], "foto": [x_coronha, y_baixo, x_frente, y_cima]}, ...}}
O retângulo da foto vai do lado da coronha (t menor) para o lado do cano, e de baixo (y menor) para cima.

Opcional na receita:
  "zonas": pedaços de cor fixa por cima da foto (ex.: gatilho preto metálico, boca do cano dourada):
     {"gatilho": {"t": [..], "y": [..], "cor": [r, g, b], "metal": 0.9, "rugosidade": 0.35}}
  "acabamento": metal/rugosidade do resto da arma ({"metal": 0.85, "rugosidade": 0.3}) e "preto_minimo" (o preto da
     foto vira preto metal: nenhum canal abaixo disso, senão o metal some no escuro)
Com acabamento ou zonas, grava também <saida>_orm.jpg (G = rugosidade, B = metal), que o jogo usa sozinho.

Uso: python3 tools/projetar_skin_arma.py ak47 referencia.png receita.json saida.jpg
"""
import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from molde_arma import ler_glb  # noqa: E402

TAM = 1024


def main():
    arma, ref_p, receita_p, saida = sys.argv[1:5]
    raiz = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    pos, uv, idx, tex = ler_glb(os.path.join(raiz, 'shaders', 'kit', 'armas', arma + '.glb'))
    ref = np.asarray(Image.open(ref_p).convert('RGB'), float)
    H, W = ref.shape[:2]
    receita = json.load(open(receita_p))
    partes = receita['partes']
    zonas = receita.get('zonas', {})
    acab = receita.get('acabamento')
    preto = np.array(receita.get('preto_minimo', [0, 0, 0]), float)
    mn, mx = pos.min(0), pos.max(0)
    t = (pos[:, 2] - mn[2]) / (mx[2] - mn[2])
    # parte de cada triângulo: a primeira da receita que contém o centro (ordem da receita = prioridade)
    nomes = list(partes)
    tc = t[idx].mean(1)
    yc = pos[idx, 1].mean(1)
    parte_tri = np.full(len(idx), -1)
    for k, nm in enumerate(nomes):
        p = partes[nm]
        m = (parte_tri < 0) & (tc >= p['t'][0]) & (tc <= p['t'][1]) & (yc >= p['y'][0]) & (yc <= p['y'][1])
        parte_tri[m] = k
    # o que sobrou: a parte de centro mais perto
    for i in np.where(parte_tri < 0)[0]:
        d = [abs(tc[i] - np.clip(tc[i], *partes[nm]['t'])) + abs(yc[i] - np.clip(yc[i], *partes[nm]['y'])) for nm in nomes]
        parte_tri[i] = int(np.argmin(d))

    def para_foto(k, tt, yy):
        p = partes[nomes[k]]
        (t0, t1), (y0, y1), (fx0, fy0, fx1, fy1) = p['t'], p['y'], p['foto']
        u = (tt - t0) / (t1 - t0)
        v = (yy - y0) / (y1 - y0)
        return fx0 + u * (fx1 - fx0), fy0 + v * (fy1 - fy0)

    def amostra(x, y):  # bilinear
        x = np.clip(x, 0, W - 1.001)
        y = np.clip(y, 0, H - 1.001)
        x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
        fx, fy = (x - x0)[:, None], (y - y0)[:, None]
        return (ref[y0, x0] * (1 - fx) * (1 - fy) + ref[y0, x0 + 1] * fx * (1 - fy) +
                ref[y0 + 1, x0] * (1 - fx) * fy + ref[y0 + 1, x0 + 1] * fx * fy)

    # começa da textura original escurecida (o que nenhum triângulo cobrir fica neutro)
    out = np.asarray(tex.resize((TAM, TAM), Image.LANCZOS), float) * 0.25
    base_m = acab['metal'] if acab else 0.0
    base_r = acab['rugosidade'] if acab else 0.6
    orm = np.zeros((TAM, TAM, 3), float)
    orm[..., 0] = 255
    orm[..., 1] = base_r * 255
    orm[..., 2] = base_m * 255
    UV = np.stack([(uv[:, 0] % 1.0) * TAM, (uv[:, 1] % 1.0) * TAM], 1)
    for i, tri in enumerate(idx):
        a, b, c = UV[tri]
        x0, x1 = int(max(0, np.floor(min(a[0], b[0], c[0])) - 1)), int(min(TAM - 1, np.ceil(max(a[0], b[0], c[0])) + 1))
        y0, y1 = int(max(0, np.floor(min(a[1], b[1], c[1])) - 1)), int(min(TAM - 1, np.ceil(max(a[1], b[1], c[1])) + 1))
        if x1 < x0 or y1 < y0:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        gx, gy = gx.ravel(), gy.ravel()
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-9:
            continue
        w0 = ((b[1] - c[1]) * (gx - c[0]) + (c[0] - b[0]) * (gy - c[1])) / den
        w1 = ((c[1] - a[1]) * (gx - c[0]) + (a[0] - c[0]) * (gy - c[1])) / den
        w2 = 1 - w0 - w1
        dentro = (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)  # um pouco de sobra: sem frestas entre os pedaços
        if not dentro.any():
            continue
        w0, w1, w2, gx, gy = w0[dentro], w1[dentro], w2[dentro], gx[dentro], gy[dentro]
        tt = w0 * t[tri[0]] + w1 * t[tri[1]] + w2 * t[tri[2]]
        yy = w0 * pos[tri[0], 1] + w1 * pos[tri[1], 1] + w2 * pos[tri[2], 1]
        fx, fy = para_foto(parte_tri[i], tt, yy)
        cor = amostra(fx, fy)
        # preto metal: o escuro da foto sobe até o preto mínimo (mantendo o tom)
        lum = cor.mean(1, keepdims=True)
        cor = np.where(lum < preto.mean(), cor + (preto - lum), cor)
        met = np.full(len(cor), base_m)
        rug = np.full(len(cor), base_r)
        for z in zonas.values():
            m = (tt >= z['t'][0]) & (tt <= z['t'][1]) & (yy >= z['y'][0]) & (yy <= z['y'][1])
            cor[m] = z['cor']
            met[m] = z.get('metal', base_m)
            rug[m] = z.get('rugosidade', base_r)
        iy, ix = gy.astype(int), gx.astype(int)
        out[iy, ix] = cor
        orm[iy, ix, 1] = rug * 255
        orm[iy, ix, 2] = met * 255
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(saida, quality=90)
    if acab or zonas:
        Image.fromarray(np.clip(orm, 0, 255).astype(np.uint8)).save(os.path.splitext(saida)[0] + '_orm.jpg', quality=92)
    contagem = {nm: int((parte_tri == k).sum()) for k, nm in enumerate(nomes)}
    print('feito:', saida, contagem)


if __name__ == '__main__':
    main()
