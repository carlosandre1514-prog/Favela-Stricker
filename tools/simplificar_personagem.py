#!/usr/bin/env python3
"""Simplifica um personagem .bin (FSP3) para a partida: menos triângulos, mesmos ossos, UVs e juntas.

Uso: python3 tools/simplificar_personagem.py entrada.bin saida.bin triangulos

Colapso de arestas pelo erro quadrático (Garland-Heckbert), com o ponto final sendo um dos vértices que já existem
(os pesos dos ossos e o UV ficam os do vértice original, sem interpolar). As costuras da textura (o mesmo ponto
repetido com UVs diferentes; nos modelos da Meshy, ~70% dos vértices) andam juntas: o grupo de vértices do mesmo
ponto vai inteiro para o grupo vizinho, cada um para o vizinho do mesmo pedaço da textura; se algum não tem par, a
aresta não colapsa. Não colapsa se virar triângulo do avesso, nem entre vértices de ossos muito diferentes sem pagar
mais caro (o dedo não vai para a palma). A cabeça fica inteira (o rosto borra fácil).
"""
import heapq
import struct
import sys

import numpy as np

DT = np.dtype([('p', '<f4', 3), ('n', '<f4', 3), ('u', '<f4', 2), ('t', '<f4', 4), ('j', '<u2', 4), ('w', '<f4', 4)])


def ler(caminho):
    b = open(caminho, 'rb').read()
    mg = b[:4]
    nv, ni = struct.unpack('<II', b[4:12])
    nj = 37 if mg == b'FSP3' else 17 if mg == b'FSP2' else 0
    off = 12 + nj * 12
    juntas = b[12:off]
    v = np.frombuffer(b, DT, nv, off).copy()
    idx = np.frombuffer(b, '<u4', ni, off + nv * DT.itemsize).reshape(-1, 3).astype(np.int64)
    return mg, juntas, v, idx


def pesos_densos(v):
    """Pesos dos ossos densos (um valor por osso), para comparar vértices."""
    W = np.zeros((len(v), 40))
    for k in range(4):
        np.add.at(W, (np.arange(len(v)), v['j'][:, k].astype(np.int64)), v['w'][:, k])
    return W


def simplificar(P, idx, alvo, W=None, travar_acima=None):
    """Índices novos (só triângulos; os vértices são os mesmos). P: posições (n, 3); W: pesos dos ossos densos
    (ou None); travar_acima: altura acima da qual nada se mexe (ou None)."""
    nv = len(P)
    P = np.asarray(P, np.float64)
    if W is None:
        W = np.zeros((nv, 1))
    # grupos: vértices no mesmo ponto (costuras)
    _, G = np.unique(np.round(P * 1e5).astype(np.int64), axis=0, return_inverse=True)
    G = G.ravel()
    ng = int(G.max()) + 1
    gpos = np.zeros((ng, 3))
    gpos[G] = P
    tris = idx.copy()
    vivo = np.ones(len(tris), bool)
    vtris = [set() for _ in range(nv)]
    for t, (a, b, c) in enumerate(tris):
        vtris[a].add(t); vtris[b].add(t); vtris[c].add(t)
    gverts = [set() for _ in range(ng)]
    for a in range(nv):
        gverts[G[a]].add(a)
    # quádricas por grupo (cada triângulo conta uma vez para cada grupo dele), ponderadas pela área
    Q = np.zeros((ng, 4, 4))
    p0, p1, p2 = P[tris[:, 0]], P[tris[:, 1]], P[tris[:, 2]]
    n = np.cross(p1 - p0, p2 - p0)
    area = np.linalg.norm(n, axis=1)
    nn = n / np.maximum(area, 1e-12)[:, None]
    plano = np.concatenate([nn, -(nn * p0).sum(1, keepdims=True)], axis=1)
    K = (area / 2)[:, None, None] * plano[:, :, None] * plano[:, None, :]
    for t in range(len(tris)):
        gs = {G[tris[t, 0]], G[tris[t, 1]], G[tris[t, 2]]}
        for g in gs:
            Q[g] += K[t]
    # bordas de verdade (abertas no modelo soldado): ficam paradas
    arestas = {}
    for a, b, c in tris:
        for x, y in ((a, b), (b, c), (c, a)):
            gx, gy = G[x], G[y]
            k = (gx, gy) if gx < gy else (gy, gx)
            arestas[k] = arestas.get(k, 0) + 1
    travado = np.zeros(ng, bool)
    for (gx, gy), c in arestas.items():
        if c == 1:
            travado[gx] = travado[gy] = True
    escala = float(np.median(area)) * 1e-4  # erro mínimo (para o peso dos ossos contar mesmo no plano)
    alturaPescoco = float('inf') if travar_acima is None else travar_acima

    def normal(a, b, c):
        return np.cross(gpos[G[b]] - gpos[G[a]], gpos[G[c]] - gpos[G[a]])

    def custo(g, h):
        """Erro de levar o grupo g para o ponto do grupo h (inf = não pode)."""
        # a cabeça (acima do pescoço) fica inteira: o rosto é o que mais aparece de perto e borra fácil
        if travado[g] or gpos[g][1] > alturaPescoco or not gverts[g] or not gverts[h]:
            return None
        par = {}
        for a in gverts[g]:
            melhor, nb = None, 0
            for t in vtris[a]:
                for b in tris[t]:
                    if G[b] == h:
                        cnt = sum(1 for t2 in vtris[a] if b in tris[t2])
                        if cnt > nb:
                            melhor, nb = b, cnt
            if melhor is None:
                return None
            par[a] = melhor
        # triângulos que ficam: não podem virar do avesso nem achatar
        for a, b in par.items():
            for t in vtris[a]:
                tri = tris[t]
                gs = [G[x] for x in tri]
                if h in gs:
                    continue
                antes = normal(*tri)
                novo = [b if x == a else x for x in tri]
                depois = normal(*novo)
                la, ld = np.linalg.norm(antes), np.linalg.norm(depois)
                if ld < 1e-12 or la < 1e-12 or np.dot(antes, depois) < 0.35 * la * ld:
                    return None
        q = Q[g] + Q[h]
        x = np.append(gpos[h], 1.0)
        erro = max(float(x @ q @ x), 0.0)
        dif = max(float(np.abs(W[a] - W[b]).sum()) for a, b in par.items())
        return (erro + escala) * (1.0 + 8.0 * dif), par

    def vizinhos(g):
        s = set()
        for a in gverts[g]:
            for t in vtris[a]:
                for b in tris[t]:
                    if G[b] != g:
                        s.add(G[b])
        return s

    heap = []
    for (gx, gy) in arestas:
        for g, h in ((gx, gy), (gy, gx)):
            r = custo(g, h)
            if r:
                heap.append((r[0], g, h))
    heapq.heapify(heap)
    ntri = len(tris)
    while ntri > alvo and heap:
        c, g, h = heapq.heappop(heap)
        r = custo(g, h)
        if not r:
            continue
        if r[0] > c * 1.0001 + 1e-30:
            heapq.heappush(heap, (r[0], g, h))
            continue
        par = r[1]
        for a, b in par.items():
            for t in list(vtris[a]):
                if not vivo[t]:
                    continue
                tri = [b if x == a else x for x in tris[t]]
                gs = {G[x] for x in tri}
                if len(gs) < 3:  # achatou: sai
                    vivo[t] = False
                    ntri -= 1
                    for x in tris[t]:
                        vtris[x].discard(t)
                    continue
                tris[t] = tri
                vtris[a].discard(t)
                vtris[b].add(t)
            vtris[a].clear()
        gverts[g].clear()
        Q[h] += Q[g]
        for n_ in vizinhos(h):
            for x, y in ((h, n_), (n_, h)):
                r2 = custo(x, y)
                if r2:
                    heapq.heappush(heap, (r2[0], x, y))
    return tris[vivo]


def gravar(caminho, mg, juntas, v, tris):
    usados = np.unique(tris.ravel())
    novo = np.full(len(v), -1, np.int64)
    novo[usados] = np.arange(len(usados))
    v2 = v[usados]
    idx = novo[tris].astype('<u4')
    with open(caminho, 'wb') as f:
        f.write(mg)
        f.write(struct.pack('<II', len(v2), idx.size))
        f.write(juntas)
        f.write(v2.tobytes())
        f.write(idx.tobytes())
    return len(v2), len(idx)


def main():
    ent, sai, alvo = sys.argv[1], sys.argv[2], int(sys.argv[3])
    mg, juntas, v, idx = ler(ent)
    P = v['p'].astype(np.float64)
    tris = simplificar(P, idx, alvo, pesos_densos(v), float(P[:, 1].max()) * 0.83)  # cabeça inteira (pés no zero)
    nv2, nt2 = gravar(sai, mg, juntas, v, tris)
    print(f'{ent}: {len(idx)} -> {nt2} triângulos, {len(v)} -> {nv2} vértices')


if __name__ == '__main__':
    main()
