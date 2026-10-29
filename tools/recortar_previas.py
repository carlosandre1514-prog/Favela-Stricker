#!/usr/bin/env python3
"""Recorta as prévias de arma renderizadas pelo motor (SO_PREVIAS) em PNG com fundo transparente.

Uso: python3 tools/recortar_previas.py <pasta_com_previa_*.png> <pasta_saida>
O fundo é a imagem previa_fundo.png (a mesma cena sem a arma): o que difere dela é a arma.
"""
import os
import sys

from PIL import Image, ImageChops, ImageFilter


def recortar(img, fundo):
    dif = ImageChops.difference(img.convert('RGB'), fundo.convert('RGB')).convert('L')
    alfa = dif.point(lambda v: 0 if v < 6 else min(255, (v - 6) * 12)).filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(0.7))
    out = img.convert('RGBA')
    out.putalpha(alfa)
    caixa = alfa.point(lambda v: 255 if v > 40 else 0).getbbox()
    if not caixa:
        return None
    m = 6
    caixa = (max(0, caixa[0] - m), max(0, caixa[1] - m), min(img.width, caixa[2] + m), min(img.height, caixa[3] + m))
    out = out.crop(caixa)
    escala = min(360 / out.width, 140 / out.height)
    return out.resize((max(1, int(out.width * escala)), max(1, int(out.height * escala))), Image.LANCZOS)


def main():
    entrada, saida = sys.argv[1], sys.argv[2]
    fundo = Image.open(os.path.join(entrada, 'previa_fundo.png'))
    os.makedirs(saida, exist_ok=True)
    for nome in sorted(os.listdir(entrada)):
        if not nome.startswith('previa_') or nome == 'previa_fundo.png':
            continue
        r = recortar(Image.open(os.path.join(entrada, nome)), fundo)
        if r:
            destino = os.path.join(saida, nome[len('previa_'):])
            r.save(destino, optimize=True)
            print(destino, r.size)


if __name__ == '__main__':
    main()
