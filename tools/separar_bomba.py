#!/usr/bin/env python3
"""Separa a bomba (a peça que desliza) de uma escopeta .glb do converter, para o motor animar depois do tiro.

Uso: python3 tools/separar_bomba.py entrada.glb saida.glb z0 z1 y0 y1
(caixa da bomba no espaço da arma: cano em +Z; os triângulos com os 3 vértices dentro dela viram a malha "bomba",
o resto fica em "corpo"; as duas usam os mesmos vértices e o mesmo material)
O motor acha as peças pelo nome (motor/engine/Project.cpp: modelParts marca "bomba"; Engine.cpp: atualizarBombas).
"""
import json
import struct
import sys

import numpy as np


def main():
    ent, sai = sys.argv[1], sys.argv[2]
    z0, z1, y0, y1 = map(float, sys.argv[3:7])
    b = open(ent, 'rb').read()
    ln = struct.unpack('<I', b[12:16])[0]
    j = json.loads(b[20:20 + ln])
    binario = bytearray(b[20 + ln + 8:20 + ln + 8 + struct.unpack('<I', b[20 + ln:24 + ln])[0]])
    prim = j['meshes'][0]['primitives'][0]
    pa = j['accessors'][prim['attributes']['POSITION']]
    pv = j['bufferViews'][pa['bufferView']]
    P = np.frombuffer(bytes(binario), '<f4', pa['count'] * 3, pv.get('byteOffset', 0) + pa.get('byteOffset', 0)).reshape(-1, 3)
    ia = j['accessors'][prim['indices']]
    iv = j['bufferViews'][ia['bufferView']]
    tipo = {5123: '<u2', 5125: '<u4'}[ia['componentType']]
    idx = np.frombuffer(bytes(binario), tipo, ia['count'], iv.get('byteOffset', 0) + ia.get('byteOffset', 0)).reshape(-1, 3)
    dentro = (P[:, 2] >= z0) & (P[:, 2] <= z1) & (P[:, 1] >= y0) & (P[:, 1] <= y1)
    eh = dentro[idx].all(1)
    partes = {'corpo': idx[~eh], 'bomba': idx[eh]}
    j['meshes'] = []
    j['nodes'] = []
    for nome, tri in partes.items():
        while len(binario) % 4:
            binario.append(0)
        dados = tri.astype(tipo).tobytes()
        j['bufferViews'].append({'buffer': 0, 'byteOffset': len(binario), 'byteLength': len(dados), 'target': 34963})
        binario.extend(dados)
        j['accessors'].append({'bufferView': len(j['bufferViews']) - 1, 'componentType': ia['componentType'], 'count': int(tri.size), 'type': 'SCALAR'})
        p2 = dict(prim)
        p2['indices'] = len(j['accessors']) - 1
        j['meshes'].append({'name': nome, 'primitives': [p2]})
        j['nodes'].append({'name': nome, 'mesh': len(j['meshes']) - 1})
    j['scenes'] = [{'nodes': [0, 1]}]
    j['scene'] = 0
    while len(binario) % 4:
        binario.append(0)
    j['buffers'] = [{'byteLength': len(binario)}]
    txt = json.dumps(j, separators=(',', ':')).encode()
    while len(txt) % 4:
        txt += b' '
    with open(sai, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(txt) + 8 + len(binario)))
        f.write(struct.pack('<II', len(txt), 0x4E4F534A))
        f.write(txt)
        f.write(struct.pack('<II', len(binario), 0x004E4942))
        f.write(bytes(binario))
    print(f'{sai}: corpo {len(partes["corpo"])} triângulos, bomba {len(partes["bomba"])}')


if __name__ == '__main__':
    main()
