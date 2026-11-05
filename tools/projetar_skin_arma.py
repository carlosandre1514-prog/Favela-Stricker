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
  "brilho": {"emissivo": 1.6, "aura": [r, g, b, força], "azul_min": 35}: o azul forte da pintura acende sozinho
     (<saida>_emi.jpg) e as bordas da arma ganham um brilho que pulsa (<saida>_efeito.json); "raios": [r, g, b]
     solta raios e faíscas dessa cor do cabo até a boca do cano; "aura_particulas": [r, g, b] envolve a arma toda numa
     aura de fumaça brilhante, brasas e faíscas dessa cor; "aura_raios": [r, g, b] a mesma aura com raios no lugar da fumaça;
     "disparo": {"cor": [r, g, b], "cor2": [r, g, b]} tiro próprio (chamas da cor2 à cor na boca do cano, bala colorida, pintura acende); "cor": "azul" ou "vermelho" (o que acende)

Uso: python3 tools/projetar_skin_arma.py ak47 referencia.png receita.json saida.jpg
"""
import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from molde_arma import ler_glb, materiais_glb  # noqa: E402

TAM = 1024


def main():
    arma, ref_p, receita_p, saida = sys.argv[1:5]
    raiz = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    glb = os.path.join(raiz, 'shaders', 'kit', 'armas', arma + '.glb')
    ref = np.asarray(Image.open(ref_p).convert('RGB'), float)
    receita = json.load(open(receita_p))
    mats = materiais_glb(glb)
    if len(mats) <= 1:
        pos, uv, idx, tex, nor, orm0 = ler_glb(glb, extra=True)
        projetar(pos, uv, idx, tex, ref, receita, receita['partes'], pos.min(0), pos.max(0), saida, True, nor, orm0)
        return
    # arma de várias peças (cada uma com a sua textura): a medida é a da arma inteira; a 1ª peça grava <saida>, as
    # outras <saida>_<peça>.jpg (o jogo pinta cada peça com a sua). Partes por peça em "pecas": {"<peça>": {...}}
    pecas = [(mi, nome.lower(), ler_glb(glb, mi, extra=True)) for mi, nome in mats]
    todos = np.concatenate([d[0] for _, _, d in pecas])
    mn, mx = todos.min(0), todos.max(0)
    raiz_saida = os.path.splitext(saida)[0]
    for k, (mi, nome, (pos, uv, idx, tex, nor, orm0)) in enumerate(pecas):
        partes = receita.get('pecas', {}).get(nome, {}).get('partes', receita.get('partes'))
        out = saida if k == 0 else f'{raiz_saida}_{nome}.jpg'
        projetar(pos, uv, idx, tex, ref, receita, partes, mn, mx, out, k == 0, nor, orm0)


def projetar(pos, uv, idx, tex, ref, receita, partes, mn, mx, saida, efeito, nor=None, orm0=None):
    H, W = ref.shape[:2]
    # "realismo" (opcional): detalhes da textura original por baixo (gravações, riscos, textura do cabo), metal por
    # cor (pintura colorida = metal pintado brilhante; escuro = grafite), arranhões do metal original na rugosidade e
    # faces de cima/baixo (que a foto de lado não mostra) na sombra da cor em vez de faixas esticadas
    real = receita.get('realismo')
    if real:
        from PIL import ImageFilter
        lum0 = tex.resize((TAM, TAM), Image.LANCZOS).convert('L')
        borr = np.asarray(lum0.filter(ImageFilter.GaussianBlur(6)), float)
        detalhe = np.clip((np.asarray(lum0, float) + 8) / (borr + 8), 0.55, 1.5) ** real.get('detalhe', 0.8)
        if orm0 is not None:
            g0 = orm0.resize((TAM, TAM), Image.LANCZOS).split()[1]
            arr = (np.asarray(g0, float) - np.asarray(g0.filter(ImageFilter.GaussianBlur(10)), float)) / 255.0
        else:
            arr = np.zeros((TAM, TAM))
        mc, me = real.get('metal_cor', {'metal': 0.7, 'rugosidade': 0.28}), real.get('metal_escuro', {'metal': 0.9, 'rugosidade': 0.4})
    zonas = receita.get('zonas', {})
    acab = receita.get('acabamento')
    preto = np.array(receita.get('preto_minimo', [0, 0, 0]), float)
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
        iy, ix = gy.astype(int), gx.astype(int)
        if real:
            mxc, mnc = cor.max(1), cor.min(1)
            sat = (mxc - mnc) / np.maximum(mxc, 1)

            def sv(a, b, x):
                k = np.clip((x - a) / (b - a), 0, 1)
                return k * k * (3 - 2 * k)
            colorido = sv(0.3, 0.6, sat) * sv(40, 90, mxc)
            met = me['metal'] + (mc['metal'] - me['metal']) * colorido
            rug = me['rugosidade'] + (mc['rugosidade'] - me['rugosidade']) * colorido
            rug = np.clip(rug + arr[iy, ix] * real.get('arranhoes', 0.6), 0.12, 0.9)
            if nor is not None:  # faces de lado mostram a foto; de cima/baixo ficam na sombra da cor
                nn = w0[:, None] * nor[tri[0]] + w1[:, None] * nor[tri[1]] + w2[:, None] * nor[tri[2]]
                lado = np.abs(nn[:, 0]) / np.maximum(np.linalg.norm(nn, axis=1), 1e-6)
                k = real.get('lados', 0.55)
                cor = cor * (k + (1 - k) * sv(0.2, 0.65, lado))[:, None]
            cor = cor * detalhe[iy, ix][:, None]
        for z in zonas.values():
            m = (tt >= z['t'][0]) & (tt <= z['t'][1]) & (yy >= z['y'][0]) & (yy <= z['y'][1])
            cor[m] = z['cor']
            met[m] = z.get('metal', base_m)
            rug[m] = z.get('rugosidade', base_r)
        out[iy, ix] = cor
        orm[iy, ix, 1] = rug * 255
        orm[iy, ix, 2] = met * 255
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(saida, quality=90)
    brilho = receita.get('brilho')
    if brilho:
        def suave(a, b, x):
            k = np.clip((x - a) / (b - a), 0, 1)
            return k * k * (3 - 2 * k)
        # o que acende: as cores fortes da pintura ("cor": "azul", "vermelho" ou uma lista, ex.: ["azul", "dourado"])
        cores = brilho.get('cor', 'azul')
        cores = [cores] if isinstance(cores, str) else cores
        am = brilho.get('azul_min', 35)
        mask = np.zeros(out.shape[:2])
        for nome in cores:
            if nome == 'dourado':  # vermelho e verde altos, azul baixo
                forca = np.minimum(out[..., 0], out[..., 1]) - out[..., 2]
                m = suave(45, 105, forca) * suave(120, 200, out[..., 0])
            else:
                ch = {'vermelho': 0, 'verde': 1, 'azul': 2}[nome]
                outros = [k for k in range(3) if k != ch]
                forca = out[..., ch] - np.maximum(out[..., outros[0]], out[..., outros[1]])
                m = suave(am, am + 60, forca) * suave(70, 170, out[..., ch])
            mask = np.maximum(mask, m)
        emi = out * mask[..., None]
        base = os.path.splitext(saida)[0]
        Image.fromarray(np.clip(emi, 0, 255).astype(np.uint8)).save(base + '_emi.jpg', quality=88)
        ef = {k: brilho[k] for k in ('emissivo', 'aura', 'raios', 'aura_particulas', 'aura_raios', 'disparo') if k in brilho}
        if efeito:
            json.dump(ef, open(base + '_efeito.json', 'w'))
    if acab or zonas:
        Image.fromarray(np.clip(orm, 0, 255).astype(np.uint8)).save(os.path.splitext(saida)[0] + '_orm.jpg', quality=92)
    contagem = {nm: int((parte_tri == k).sum()) for k, nm in enumerate(nomes)}
    print('feito:', saida, contagem)


if __name__ == '__main__':
    main()
