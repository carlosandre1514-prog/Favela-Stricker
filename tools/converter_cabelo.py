#!/usr/bin/env python3
"""Converte um cabelo 3D (.obj com cor por vértice, Z para cima, frente em -Y, domo sobre a cabeça com a base
em Z=0) para o formato do jogo (shaders/kit/personagens/cabelos/<nome>.bin).

A cabeça do boneco é um cubo (meia-largura 0,75 nesta unidade, centro na origem). O domo do modelo é redondo e
deixaria os cantos do cubo para fora, então ele NÃO vai para o jogo: o jogo desenha uma meia touca arredondada
(superelipsoide, centro TOUCA_C, raios TOUCA_R; os mesmos números em buildModelHair) com a cor do domo, e o modelo
inteiro é esticado do domo para essa touca (cada vértice na sua direção a partir da base do domo).
As peças vão para a camada de detalhe (só aparecem de perto).

Formato (little-endian): b'CAB2', cor da touca RGBA 4×u8 (sRGB), u32 nVert, u32 nIdx,
vértices (pos 3×f32, normal 3×f32, cor RGBA 4×u8 sRGB) e índices (u32).
Eixos do jogo: X direita, Y para cima, Z para a frente; unidade = 0,1 × altura do boneco, origem no centro da cabeça.

Uso: python3 tools/converter_cabelo.py entrada.obj saida.bin
"""
import math
import struct
import sys

import numpy as np


def ler_obj(caminho):
    objs, V, C = [], [], []
    atual = None
    for linha in open(caminho):
        if linha.startswith('o '):
            atual = []
            objs.append(atual)
        elif linha.startswith('v '):
            a = linha.split()
            V.append([float(x) for x in a[1:4]])
            C.append([float(x) for x in a[4:7]] if len(a) >= 7 else [0.1, 0.1, 0.1])
        elif linha.startswith('f '):
            if atual is None:
                atual = []
                objs.append(atual)
            ids = [int(t.split('/')[0]) - 1 for t in linha.split()[1:]]
            for k in range(1, len(ids) - 1):
                atual.append([ids[0], ids[k], ids[k + 1]])
    V = np.array(V, dtype=np.float64)
    # Z para cima, frente em -Y  ->  Y para cima, frente em +Z
    V = np.stack([V[:, 0], V[:, 2], -V[:, 1]], 1)
    return V, np.array(C), [np.array(f, dtype=np.int64) for f in objs if f]


def normais(V, F):
    n = np.zeros_like(V)
    fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    for k in range(3):
        np.add.at(n, F[:, k], fn)
    l = np.linalg.norm(n, axis=1, keepdims=True)
    l[l < 1e-12] = 1.0
    return n / l


def peca(V, C, F, para_fora=None):
    """Recorta os vértices usados por F, confere o sentido dos triângulos e calcula as normais."""
    usados = np.unique(F)
    mapa = -np.ones(len(V), dtype=np.int64)
    mapa[usados] = np.arange(len(usados))
    v, c, f = V[usados], C[usados], mapa[F]
    centro = v.mean(0) if para_fora is None else para_fora
    fn = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
    fora = (fn * (v[f].mean(1) - centro)).sum(1)
    if (fora < 0).sum() > (fora > 0).sum():  # a maioria virada para dentro: inverte tudo
        f = f[:, [0, 2, 1]]
    return v, normais(v, f), c, f


TOUCA_C = np.array([0.0, 0.3, 0.0])     # a base do domo do modelo vai para esta altura (testa livre)
TOUCA_R = np.array([0.93, 0.78, 0.95])  # meia touca de superelipsoide: cobre os cantos de cima do cubo


def raio_super(d):
    """Distância do centro da touca até a superfície dela na direção d (superelipsoide de expoente 4)."""
    return (((d / TOUCA_R) ** 4).sum()) ** -0.25


def raio_domo(O, R, p0, d):
    """Distância de p0 até o domo (elipsoide centro O, raios R) na direção d."""
    q, dd = (p0 - O) / R, d / R
    a, b, c = (dd * dd).sum(), 2 * (q * dd).sum(), (q * q).sum() - 1
    disc = max(b * b - 4 * a * c, 0.0)
    return (-b + math.sqrt(disc)) / (2 * a)


def domo(V, C, F):
    usados = np.unique(F)
    v = V[usados]
    base = v[:, 1].min()
    O = np.array([(v[:, 0].max() + v[:, 0].min()) / 2, base, (v[:, 2].max() + v[:, 2].min()) / 2])
    R = np.array([(v[:, 0].max() - v[:, 0].min()) / 2, v[:, 1].max() - base, (v[:, 2].max() - v[:, 2].min()) / 2])
    return O, R, C[usados].mean(0)


def mover(p, O, R):
    """Estica cada vértice do domo redondo para a touca (mesma direção a partir da base do domo): as peças
    acompanham a superfície sem se soltar e a franja fica na testa."""
    v, n, c, f = p
    rel = v - O
    dist = np.linalg.norm(rel, axis=1, keepdims=True)
    d = rel / np.maximum(dist, 1e-9)
    rE = 1.0 / np.sqrt(((d / R) ** 2).sum(1, keepdims=True))
    rS = (((d / TOUCA_R) ** 4).sum(1, keepdims=True)) ** -0.25
    v2 = TOUCA_C + d * dist * (rS / rE)
    # normais: recalculadas depois da deformação
    return v2, normais(v2, f), c, f


def juntar(pecas):
    Vs, Ns, Cs, Fs, off = [], [], [], [], 0
    for v, n, c, f in pecas:
        Vs.append(v); Ns.append(n); Cs.append(c); Fs.append(f + off); off += len(v)
    return np.concatenate(Vs), np.concatenate(Ns), np.concatenate(Cs), np.concatenate(Fs)


def main():
    V, C, objs = ler_obj(sys.argv[1])
    O, R, cor_domo = domo(V, C, objs[0])
    v, n, c, f = juntar([mover(peca(V, C, F), O, R) for F in objs[1:]])
    out = bytearray(b'CAB2')
    out += struct.pack('<4B', *np.clip(np.round(cor_domo * 255), 0, 255).astype(int), 255)
    out += struct.pack('<2I', len(v), f.size)
    cor = np.clip(np.round(c * 255), 0, 255).astype(np.uint8)
    for k in range(len(v)):
        out += struct.pack('<6f4B', *v[k], *n[k], *cor[k], 255)
    out += f.astype('<u4').tobytes()
    open(sys.argv[2], 'wb').write(out)
    print(sys.argv[2], len(f), 'triângulos de detalhe')


if __name__ == '__main__':
    main()
