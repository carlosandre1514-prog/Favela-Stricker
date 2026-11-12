#!/usr/bin/env python3
"""Aplica a transformação de cada nó direto nos vértices de um .glb e grava um .glb limpo (só posição, normal,
índices e o material de cor/metal/rugosidade de cada peça), com os nós sem matriz.

Usado nas cápsulas feitas no MiniBlender: ele guarda o tamanho de cada peça (o cilindro-base de 1 x 1,4 m) como
escala do nó, e a peça de 4 cm virava 1 m no jogo. Assado, o arquivo já tem as medidas reais.

Uso: python3 tools/assar_glb.py entrada.glb saida.glb
"""
import json
import struct
import sys

import numpy as np

COMP = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def ler(caminho):
    d = open(caminho, 'rb').read()
    n = struct.unpack('<I', d[12:16])[0]
    js = json.loads(d[20:20 + n])
    off = 20 + n
    nb = struct.unpack('<I', d[off:off + 4])[0]
    return js, d[off + 8:off + 8 + nb]


def acessor(js, binario, i):
    a = js['accessors'][i]
    bv = js['bufferViews'][a['bufferView']]
    tipo, k = COMP[a['componentType']], NCOMP[a['type']]
    passo = bv.get('byteStride', np.dtype(tipo).itemsize * k)
    ini = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    linhas = [np.frombuffer(binario, tipo, k, ini + j * passo) for j in range(a['count'])]
    return np.array(linhas)


def matriz(no):
    if 'matrix' in no:
        return np.array(no['matrix'], dtype=np.float64).reshape(4, 4).T
    T = np.eye(4)
    T[:3, 3] = no.get('translation', [0, 0, 0])
    x, y, z, w = no.get('rotation', [0, 0, 0, 1])
    R = np.eye(4)
    R[:3, :3] = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                 [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                 [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    S = np.diag(list(no.get('scale', [1, 1, 1])) + [1])
    return T @ R @ S


def main():
    ent, sai = sys.argv[1], sys.argv[2]
    js, binario = ler(ent)
    pecas = []  # (posições, normais, índices, material)

    def andar(i, pai):
        no = js['nodes'][i]
        M = pai @ matriz(no)
        if 'mesh' in no:
            N = np.linalg.inv(M[:3, :3]).T
            for pr in js['meshes'][no['mesh']]['primitives']:
                P = acessor(js, binario, pr['attributes']['POSITION']).astype(np.float64)
                P = (M @ np.c_[P, np.ones(len(P))].T).T[:, :3]
                if 'NORMAL' in pr['attributes']:
                    Nn = (N @ acessor(js, binario, pr['attributes']['NORMAL']).astype(np.float64).T).T
                    Nn /= np.maximum(np.linalg.norm(Nn, axis=1, keepdims=True), 1e-12)
                else:
                    Nn = np.tile([0.0, 1.0, 0.0], (len(P), 1))
                idx = acessor(js, binario, pr['indices']).ravel() if 'indices' in pr else np.arange(len(P))
                if np.linalg.det(M[:3, :3]) < 0:  # espelhado: inverte a ordem dos triângulos
                    idx = idx.reshape(-1, 3)[:, ::-1].ravel()
                mat = js['materials'][pr['material']] if 'material' in pr else {}
                pecas.append((P.astype(np.float32), Nn.astype(np.float32), idx.astype(np.uint32), mat, no.get('name', 'peca')))
        for c in no.get('children', []):
            andar(c, M)

    for r in js['scenes'][js.get('scene', 0)]['nodes']:
        andar(r, np.eye(4))

    blob = bytearray()
    out = {'asset': {'version': '2.0', 'generator': 'assar_glb (Favela Strike)'}, 'scene': 0, 'scenes': [{'nodes': []}],
           'nodes': [], 'meshes': [], 'materials': [], 'accessors': [], 'bufferViews': []}

    def vista(dados, alvo):
        while len(blob) % 4:
            blob.append(0)
        out['bufferViews'].append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(dados), 'target': alvo})
        blob.extend(dados)
        return len(out['bufferViews']) - 1

    for P, Nn, idx, mat, nome in pecas:
        pbr = mat.get('pbrMetallicRoughness', {})
        out['materials'].append({'name': nome, 'pbrMetallicRoughness': {
            'baseColorFactor': pbr.get('baseColorFactor', [1, 1, 1, 1]),
            'metallicFactor': pbr.get('metallicFactor', 0.0), 'roughnessFactor': pbr.get('roughnessFactor', 0.5)}})
        a0 = len(out['accessors'])
        out['accessors'] += [
            {'bufferView': vista(P.tobytes(), 34962), 'componentType': 5126, 'count': len(P), 'type': 'VEC3',
             'min': P.min(0).tolist(), 'max': P.max(0).tolist()},
            {'bufferView': vista(Nn.tobytes(), 34962), 'componentType': 5126, 'count': len(Nn), 'type': 'VEC3'},
            {'bufferView': vista(idx.tobytes(), 34963), 'componentType': 5125, 'count': len(idx), 'type': 'SCALAR'}]
        out['meshes'].append({'name': nome, 'primitives': [{'attributes': {'POSITION': a0, 'NORMAL': a0 + 1}, 'indices': a0 + 2,
                                                             'material': len(out['materials']) - 1}]})
        out['nodes'].append({'name': nome, 'mesh': len(out['meshes']) - 1})
        out['scenes'][0]['nodes'].append(len(out['nodes']) - 1)
    while len(blob) % 4:
        blob.append(0)
    out['buffers'] = [{'byteLength': len(blob)}]
    j = json.dumps(out, separators=(',', ':')).encode()
    while len(j) % 4:
        j += b' '
    total = 12 + 8 + len(j) + 8 + len(blob)
    with open(sai, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, total))
        f.write(struct.pack('<II', len(j), 0x4E4F534A) + j)
        f.write(struct.pack('<II', len(blob), 0x004E4942) + bytes(blob))
    tudo = np.concatenate([p[0] for p in pecas])
    print(f'{ent}: {len(pecas)} peças, {sum(len(p[2]) // 3 for p in pecas)} triângulos, '
          f'{(tudo.max(0) - tudo.min(0)).round(4).tolist()} m -> {sai} ({total} bytes)')


if __name__ == '__main__':
    main()
