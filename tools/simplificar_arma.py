#!/usr/bin/env python3
"""Versão leve de um modelo .glb (armas): menos triângulos, mesmas texturas e materiais.

Uso: python3 tools/simplificar_arma.py entrada.glb saida.glb triangulos

Cada malha passa pelo mesmo simplificador dos personagens (tools/simplificar_personagem.py: erro quadrático, costuras da
textura juntas, sem virar triângulo do avesso); os vértices que sobram são os originais (UV, normal e tangente sem
mudar). As imagens e o resto do arquivo são copiados.
"""
import json
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from simplificar_personagem import simplificar  # noqa: E402

TIPO = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
COMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def ler_glb(caminho):
    b = open(caminho, 'rb').read()
    ln = struct.unpack('<I', b[12:16])[0]
    j = json.loads(b[20:20 + ln])
    off = 20 + ln
    blen = struct.unpack('<I', b[off:off + 4])[0]
    return j, b[off + 8:off + 8 + blen]


def ler_acessor(j, binario, i):
    a = j['accessors'][i]
    bv = j['bufferViews'][a['bufferView']]
    dt = np.dtype(TIPO[a['componentType']]).newbyteorder('<')
    n, c = a['count'], COMP[a['type']]
    ini = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    passo = bv.get('byteStride', 0) or dt.itemsize * c
    if passo == dt.itemsize * c:
        return np.frombuffer(binario, dt, n * c, ini).reshape(n, c).copy()
    linhas = [np.frombuffer(binario, dt, c, ini + k * passo) for k in range(n)]
    return np.array(linhas)


def main():
    ent, sai, alvo_total = sys.argv[1], sys.argv[2], int(sys.argv[3])
    j, binario = ler_glb(ent)
    prims = [p for m in j['meshes'] for p in m['primitives'] if p.get('mode', 4) == 4 and 'indices' in p]
    total = sum(j['accessors'][p['indices']]['count'] // 3 for p in prims)
    usados_mesh = set()
    for p in prims:
        usados_mesh.add(p['indices'])
        usados_mesh.update(p['attributes'].values())
    novo_bin = bytearray()
    novas_views = []

    def anexar(dados, alvo=None):
        while len(novo_bin) % 4:
            novo_bin.append(0)
        v = {'buffer': 0, 'byteOffset': len(novo_bin), 'byteLength': len(dados)}
        if alvo:
            v['target'] = alvo
        novo_bin.extend(dados)
        novas_views.append(v)
        return len(novas_views) - 1

    # views que não são das malhas (imagens etc.): copiadas
    mapa_view = {}
    views_mesh = {j['accessors'][i]['bufferView'] for i in usados_mesh}
    for k, bv in enumerate(j['bufferViews']):
        if k in views_mesh:
            continue
        ini = bv.get('byteOffset', 0)
        nv_ = dict(bv)
        nv_.pop('byteOffset', None)
        idx_novo = anexar(binario[ini:ini + bv['byteLength']])
        novas_views[idx_novo].update({k2: v2 for k2, v2 in nv_.items() if k2 not in ('buffer', 'byteLength')})
        mapa_view[k] = idx_novo
    for a in j['accessors']:
        if a.get('bufferView') in mapa_view:
            a['bufferView'] = mapa_view[a['bufferView']]
    for im in j.get('images', []):
        if 'bufferView' in im:
            im['bufferView'] = mapa_view[im['bufferView']]

    antes = depois = 0
    for p in prims:
        idx = ler_acessor(j, binario, p['indices']).reshape(-1, 3).astype(np.int64)
        P = ler_acessor(j, binario, p['attributes']['POSITION']).astype(np.float64)
        alvo = max(12, int(alvo_total * len(idx) / max(total, 1)))
        tris = simplificar(P, idx, alvo) if len(idx) > alvo else idx
        usados = np.unique(tris.ravel())
        mapa = np.full(len(P), -1, np.int64)
        mapa[usados] = np.arange(len(usados))
        antes += len(idx)
        depois += len(tris)
        for nome, ai in list(p['attributes'].items()):
            a = j['accessors'][ai]
            dados = ler_acessor(j, binario, ai)[usados]
            vi = anexar(dados.astype(dados.dtype.newbyteorder('<')).tobytes(), 34962)
            a.update({'bufferView': vi, 'byteOffset': 0, 'count': int(len(usados))})
            if 'min' in a:
                a['min'] = [float(x) for x in dados.min(0)]
                a['max'] = [float(x) for x in dados.max(0)]
        novo_idx = mapa[tris].ravel()
        grande = len(usados) > 65535
        dados = novo_idx.astype('<u4' if grande else '<u2').tobytes()
        a = j['accessors'][p['indices']]
        a.update({'bufferView': anexar(dados, 34963), 'byteOffset': 0, 'count': int(len(novo_idx)), 'componentType': 5125 if grande else 5123})
        a.pop('min', None); a.pop('max', None)
    while len(novo_bin) % 4:
        novo_bin.append(0)
    j['bufferViews'] = novas_views
    j['buffers'] = [{'byteLength': len(novo_bin)}]
    txt = json.dumps(j, separators=(',', ':')).encode()
    while len(txt) % 4:
        txt += b' '
    with open(sai, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(txt) + 8 + len(novo_bin)))
        f.write(struct.pack('<II', len(txt), 0x4E4F534A))
        f.write(txt)
        f.write(struct.pack('<II', len(novo_bin), 0x004E4942))
        f.write(novo_bin)
    print(f'{ent}: {antes} -> {depois} triângulos ({os.path.getsize(sai) // 1024} KB)')


if __name__ == '__main__':
    main()
