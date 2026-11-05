#!/usr/bin/env python3
"""Recorta as camadas do personagem da tela inicial renderizadas pelo motor (SO_LOBBY=1 no favela_testes).

Uso: python3 tools/recortar_lobby.py <pasta_do_teste> <pasta_saida> [altura]

Entrada (todas com a mesma câmera e a mesma pose):
  previa_fundo.png            a cena vazia
  previa_p_<roupa>.png        o personagem segurando a arma, com a arma escondida
  lobby_com_<arma>.png        um personagem com a arma <arma>[_<skin>] na mão
  lobby_sem_<arma>.png        o mesmo quadro sem a arma
Saída (PNG com fundo transparente, todas do mesmo tamanho, para empilhar no mesmo lugar):
  p_<roupa>.png               o personagem (diferença para o fundo)
  a_<arma>.png                só a arma (diferença entre com e sem ela: onde a mão passa por cima, fica a mão
                              da roupa que estiver embaixo)
A pistola (tt33) fica de fora: a pose dela é outra.
"""
import os
import sys

from PIL import Image, ImageChops, ImageFilter


def alfa_de(a, b, limiar=6, aparar=False):
    dif = ImageChops.difference(a.convert('RGB'), b.convert('RGB')).convert('L')
    alfa = dif.point(lambda v: 0 if v < limiar else min(255, (v - limiar) * 12)).filter(ImageFilter.MaxFilter(3))
    if aparar:  # tira 2 px da borda: sem o contorno claro do céu misturado na beirada
        alfa = alfa.filter(ImageFilter.MinFilter(5))
    return alfa.filter(ImageFilter.GaussianBlur(0.7))


def main():
    entrada, saida = sys.argv[1], sys.argv[2]
    altura = int(sys.argv[3]) if len(sys.argv) > 3 else 560
    fundo = Image.open(os.path.join(entrada, 'previa_fundo.png'))
    camadas = {}
    for nome in sorted(os.listdir(entrada)):
        caminho = os.path.join(entrada, nome)
        if nome.startswith('previa_p_'):
            img = Image.open(caminho)
            camadas['p_' + nome[len('previa_p_'):]] = (img, alfa_de(img, fundo, aparar=True))
        elif nome.startswith('lobby_com_') and not nome.startswith('lobby_com_tt33'):
            sem = os.path.join(entrada, 'lobby_sem_' + nome[len('lobby_com_'):])
            if not os.path.exists(sem):
                continue
            img = Image.open(caminho)
            camadas['a_' + nome[len('lobby_com_'):]] = (img, alfa_de(img, Image.open(sem), 10))
    # a mesma caixa para todas (as camadas se encaixam umas nas outras)
    caixa = None
    for img, alfa in camadas.values():
        b = alfa.point(lambda v: 255 if v > 40 else 0).getbbox()
        if b:
            caixa = b if caixa is None else (min(caixa[0], b[0]), min(caixa[1], b[1]), max(caixa[2], b[2]), max(caixa[3], b[3]))
    m = 8
    caixa = (max(0, caixa[0] - m), max(0, caixa[1] - m), min(fundo.width, caixa[2] + m), min(fundo.height, caixa[3] + m))
    escala = altura / (caixa[3] - caixa[1])
    tamanho = (max(1, round((caixa[2] - caixa[0]) * escala)), altura)
    os.makedirs(saida, exist_ok=True)
    for nome, (img, alfa) in camadas.items():
        out = img.convert('RGBA')
        out.putalpha(alfa)
        out = out.crop(caixa).resize(tamanho, Image.LANCZOS)
        out.save(os.path.join(saida, nome), optimize=True)
    print(f'{len(camadas)} camadas {tamanho[0]}x{tamanho[1]} em {saida}')


if __name__ == '__main__':
    main()
