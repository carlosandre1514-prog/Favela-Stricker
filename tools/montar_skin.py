#!/usr/bin/env python3
"""Monta uma skin no molde do boneco a partir de uma imagem com outra disposição (ex.: uma folha de skin feita por IA).

A receita (JSON) diz, para cada face do molde, de que retângulo da imagem de origem ela vem:
  {"partes": {"cabeca": {"frente": [x0, y0, x1, y1], ...}, ...}}
Uma face também pode ser uma lista de camadas: [{"de": [x0,y0,x1,y1], "em": [fa, fb]}, ...] — cada camada ocupa
a faixa [fa, fb] da altura da face (0 = em cima, 1 = embaixo), para juntar camisa + calça no tronco, por exemplo.
Prefixo "espelhar" na camada ({"de": [...], "espelhar": true}) vira o recorte de lado.

"limpar_fundo": lista de partes onde o fundo da imagem de origem (preto ou da cor do canto) que entrar no recorte
é apagado e preenchido com a cor do lado (pele, cabelo), para a cabeça e os braços não ficarem com pedaços de fundo.

Uso: python3 tools/montar_skin.py origem.png receita.json saida.png [tamanho=1024]
"""
import json
import os
import sys

import numpy as np
from PIL import Image, ImageOps

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def _dilatar(m):
    d = m.copy()
    d[1:] |= m[:-1]; d[:-1] |= m[1:]; d[:, 1:] |= m[:, :-1]; d[:, :-1] |= m[:, 1:]
    return d


def limpar_fundo(peca, fundo, tol=30):
    """Apaga o fundo que encosta na borda do recorte e preenche com a média dos vizinhos (puxa a cor de dentro)."""
    a = np.asarray(peca, dtype=np.float32).copy()
    perto = np.abs(a - np.array(fundo, dtype=np.float32)).max(axis=2) <= tol
    borda = np.zeros_like(perto)
    borda[-1, :] = True  # a borda de cima fica de fora: ali costuma estar o cabelo, que também é escuro
    borda[:, 0] = borda[:, -1] = True
    m = perto & borda
    while True:  # o fundo ligado à borda
        n = perto & _dilatar(m)
        if n.sum() == m.sum():
            break
        m = n
    if m.all() or not m.any():
        return peca
    conhecido = ~m
    while not conhecido.all():
        soma = np.zeros_like(a)
        cont = np.zeros(m.shape, dtype=np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            k = np.roll(np.roll(conhecido, dy, 0), dx, 1)
            v = np.roll(np.roll(a, dy, 0), dx, 1)
            soma += v * k[..., None]
            cont += k
        novo = (~conhecido) & (cont > 0)
        if not novo.any():
            break
        a[novo] = soma[novo] / cont[novo][:, None]
        conhecido |= novo
    return Image.fromarray(a.clip(0, 255).astype(np.uint8))


def main():
    origem, receita, saida = sys.argv[1:4]
    tam = int(sys.argv[4]) if len(sys.argv) > 4 else 1024
    src = Image.open(origem).convert('RGB')
    molde = json.load(open(os.path.join(RAIZ, 'shaders', 'kit', 'personagens', 'molde.json'), encoding='utf-8'))
    rec = json.load(open(receita, encoding='utf-8'))
    out = Image.new('RGB', (tam, tam), (128, 128, 128))
    limpar = set(rec.get('limpar_fundo', []))
    fundo = src.getpixel((0, 0))  # cor do fundo da folha (canto de cima)
    for parte, faces in rec['partes'].items():
        for face, camadas in faces.items():
            r = molde['partes'][parte][face]
            x0, y0, x1, y1 = [round(v * tam) for v in r]
            if isinstance(camadas, list) and camadas and isinstance(camadas[0], (int, float)):
                camadas = [{'de': camadas}]
            for c in camadas:
                if isinstance(c, list):
                    c = {'de': c}
                fa, fb = c.get('em', [0, 1])
                ya, yb = y0 + round((y1 - y0) * fa), y0 + round((y1 - y0) * fb)
                if x1 - x0 < 1 or yb - ya < 1:
                    continue
                peca = src.crop(tuple(c['de']))
                if parte in limpar and c.get('limpar', True):
                    peca = limpar_fundo(peca, fundo)
                if c.get('espelhar'):
                    peca = ImageOps.mirror(peca)
                out.paste(peca.resize((x1 - x0, yb - ya), Image.LANCZOS), (x0, ya))
    out.save(saida, optimize=True)
    print('ok:', saida)


if __name__ == '__main__':
    main()
