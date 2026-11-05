#!/usr/bin/env python3
"""Encaixa o colete (OBJ do tools/converter_colete.cpp) no tronco do boneco de blocos e grava o formato do jogo.

O colete original ("Armor vest" de 3DWorkbench, CC BY 4.0) foi feito para um tronco humano arredondado; o boneco tem o
tronco quadrado. Cada vértice é levado da elipse do colete (vista de cima) para uma superelipse que cobre os cantos do
tronco, e a altura vai da cintura ao ombro. Unidades de saída: fração da altura do boneco (o jogo multiplica por H).

Também monta a textura única (colete à esquerda, bolsos à direita), clareada para o jogo tingir com a cor da roupa
(cor do vértice) e com o relevo das costuras do mapa normal impresso na cor.

Formato (little-endian): b'COL1', u32 nVert, u32 nIdx, vértices (pos 3×f32, normal 3×f32, uv 2×f32), índices u32.

Uso: python3 tools/encaixar_colete.py colete.obj pasta_texturas saida.bin saida.jpg
"""
import struct
import sys

import numpy as np
from PIL import Image

# onde o colete fica no boneco (fração da altura): tronco do boneco de blocos tem 0,24 × 0,12 de largura × fundo
LARG, FUNDO = 0.146, 0.093       # meia largura e meia profundidade da superelipse (cobre os cantos do tronco)
Y0, Y1 = 0.555, 0.862            # da cintura até um pouco acima do ombro (alças)


def ler_obj(p):
    V, N, T, grupos, g = [], [], [], {}, None
    for l in open(p):
        if l.startswith('g '):
            g = l.split()[1]
            grupos.setdefault(g, [])
        elif l.startswith('v '):
            V.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('vn'):
            N.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('vt'):
            T.append([float(x) for x in l.split()[1:3]])
        elif l.startswith('f '):
            grupos[g].append([int(x.split('/')[0]) - 1 for x in l.split()[1:4]])
    return np.array(V), np.array(N), np.array(T), {k: np.array(v) for k, v in grupos.items()}


def main():
    V, N, T, G = ler_obj(sys.argv[1])
    corpo = np.unique(G['colete'])
    vb = V[corpo]
    a = (vb[:, 0].max() - vb[:, 0].min()) / 2
    z0 = (vb[:, 2].max() + vb[:, 2].min()) / 2
    b = (vb[:, 2].max() - vb[:, 2].min()) / 2
    ymin, ymax = vb[:, 1].min(), vb[:, 1].max()
    # horizontal: elipse do colete -> superelipse (expoente 4) do tronco quadrado
    u = np.stack([V[:, 0] / a, (V[:, 2] - z0) / b], 1)
    r = np.linalg.norm(u, axis=1, keepdims=True)
    d = u / np.maximum(r, 1e-9)
    rs = ((np.abs(d) ** 4).sum(1, keepdims=True)) ** -0.25
    w = u * rs
    out = np.zeros_like(V)
    out[:, 0] = w[:, 0] * LARG
    out[:, 2] = w[:, 1] * FUNDO
    out[:, 1] = Y0 + (V[:, 1] - ymin) / (ymax - ymin) * (Y1 - Y0)
    # normais: escala inversa (a deformação suave da superelipse mexe pouco nelas)
    n = N / np.array([LARG / a, (Y1 - Y0) / (ymax - ymin), FUNDO / b])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    idx = np.concatenate([G[k] for k in G]).astype('<u4')
    with open(sys.argv[3], 'wb') as f:
        f.write(b'COL1' + struct.pack('<2I', len(V), idx.size))
        f.write(np.concatenate([out, n, T], 1).astype('<f4').tobytes())
        f.write(idx.tobytes())
    print(sys.argv[3], len(V), 'vértices,', idx.size // 3, 'triângulos')

    # textura: colete | bolsos, 512 cada; clareia (o jogo multiplica pela cor da roupa) e imprime o relevo
    pasta = sys.argv[2]
    partes = []
    for nome in ('bbM2', 'pouch'):
        cor = np.asarray(Image.open(f'{pasta}/avandpouchUV_{nome}_BaseColor.png').convert('L').resize((512, 512), Image.LANCZOS), float) / 255
        nm = np.asarray(Image.open(f'{pasta}/avandpouchUV_{nome}_Normal.png').convert('RGB').resize((512, 512), Image.LANCZOS), float) / 255
        luz = np.clip(((nm[..., 0] - .5) * -0.5 + (nm[..., 1] - .5) * 0.8) * 1.6 + (nm[..., 2] - .5) * .4, -0.5, 0.5)  # relevo (luz de cima)
        lum = 0.62 + (cor - cor.mean()) * 1.1 + luz * 0.55
        partes.append(np.clip(lum, 0.12, 1.0))
    img = (np.concatenate(partes, 1) * 255).astype(np.uint8)
    Image.fromarray(img, 'L').convert('RGB').save(sys.argv[4], quality=85, optimize=True)
    print(sys.argv[4])


if __name__ == '__main__':
    main()
