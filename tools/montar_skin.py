#!/usr/bin/env python3
"""Monta uma skin no molde do boneco a partir de uma imagem com outra disposição (ex.: uma folha de skin feita por IA).

A receita (JSON) diz, para cada face do molde, de que retângulo da imagem de origem ela vem:
  {"partes": {"cabeca": {"frente": [x0, y0, x1, y1], ...}, ...}}
Uma face também pode ser uma lista de camadas: [{"de": [x0,y0,x1,y1], "em": [fa, fb]}, ...] — cada camada ocupa
a faixa [fa, fb] da altura da face (0 = em cima, 1 = embaixo), para juntar camisa + calça no tronco, por exemplo.
Prefixo "espelhar" na camada ({"de": [...], "espelhar": true}) vira o recorte de lado.

Uso: python3 tools/montar_skin.py origem.png receita.json saida.png [tamanho=1024]
"""
import json
import os
import sys

from PIL import Image, ImageOps

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def main():
    origem, receita, saida = sys.argv[1:4]
    tam = int(sys.argv[4]) if len(sys.argv) > 4 else 1024
    src = Image.open(origem).convert('RGB')
    molde = json.load(open(os.path.join(RAIZ, 'shaders', 'kit', 'personagens', 'molde.json'), encoding='utf-8'))
    rec = json.load(open(receita, encoding='utf-8'))
    out = Image.new('RGB', (tam, tam), (128, 128, 128))
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
                if c.get('espelhar'):
                    peca = ImageOps.mirror(peca)
                out.paste(peca.resize((x1 - x0, yb - ya), Image.LANCZOS), (x0, ya))
    out.save(saida, optimize=True)
    print('ok:', saida)


if __name__ == '__main__':
    main()
