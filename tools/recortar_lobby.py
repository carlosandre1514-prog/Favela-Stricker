#!/usr/bin/env python3
"""Recorta as camadas do personagem da tela inicial renderizadas pelo motor (SO_LOBBY=1 no favela_testes) e monta
uma tira por camada com todos os ângulos (a pessoa gira o personagem arrastando o dedo).

Uso: python3 tools/recortar_lobby.py <pasta_do_teste> <pasta_saida> [altura_do_quadro] [qualidade_webp]

Entrada (todas com a mesma câmera e a mesma pose; NN = ângulo 00, 01, ...):
  previa_fundo.png                a cena vazia
  previa_p_<roupa>_NN.png         o personagem segurando a arma, com a arma escondida
  lobby_com_<arma>_NN.png         um personagem com a arma <arma>[_<skin>] na mão
  lobby_sem_<arma>_NN.png         o mesmo quadro sem a arma
Saída (WebP com fundo transparente; quadros lado a lado, todos do mesmo tamanho, para empilhar no mesmo lugar):
  p_<roupa>.webp                  o personagem (diferença para o fundo)
  a_<arma>.webp                   só a arma (diferença entre com e sem ela: onde a mão passa por cima, fica a mão
                                  da roupa que estiver embaixo)
  lobby.json                      número de quadros e tamanho de cada um
"""
import json
import os
import re
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
    altura = int(sys.argv[3]) if len(sys.argv) > 3 else 480
    qualidade = int(sys.argv[4]) if len(sys.argv) > 4 else 82
    fundo = Image.open(os.path.join(entrada, 'previa_fundo.png'))
    # camada -> {ângulo: (imagem, alfa)}
    camadas = {}
    for nome in sorted(os.listdir(entrada)):
        m = re.match(r'previa_p_(.+)_(\d\d)\.png$', nome)
        if m:
            img = Image.open(os.path.join(entrada, nome))
            camadas.setdefault('p_' + m[1], {})[int(m[2])] = (img, alfa_de(img, fundo, aparar=True))
            continue
        m = re.match(r'lobby_com_(.+)_(\d\d)\.png$', nome)
        if m and not m[1].startswith('tt33'):
            sem = os.path.join(entrada, f'lobby_sem_{m[1]}_{m[2]}.png')
            if os.path.exists(sem):
                img = Image.open(os.path.join(entrada, nome))
                camadas.setdefault('a_' + m[1], {})[int(m[2])] = (img, alfa_de(img, Image.open(sem), 10))
    quadros = max(len(v) for v in camadas.values())
    camadas = {k: v for k, v in camadas.items() if len(v) == quadros}  # só as que têm todos os ângulos
    # a mesma caixa para todos os quadros de todas as camadas (encaixam umas nas outras em qualquer ângulo)
    caixa = None
    for angulos in camadas.values():
        for _, alfa in angulos.values():
            b = alfa.point(lambda v: 255 if v > 40 else 0).getbbox()
            if b:
                caixa = b if caixa is None else (min(caixa[0], b[0]), min(caixa[1], b[1]), max(caixa[2], b[2]), max(caixa[3], b[3]))
    m = 6
    caixa = (max(0, caixa[0] - m), max(0, caixa[1] - m), min(fundo.width, caixa[2] + m), min(fundo.height, caixa[3] + m))
    escala = altura / (caixa[3] - caixa[1])
    larg = max(1, round((caixa[2] - caixa[0]) * escala))
    os.makedirs(saida, exist_ok=True)
    total = 0
    for nome, angulos in camadas.items():
        tira = Image.new('RGBA', (larg * quadros, altura), (0, 0, 0, 0))
        for i in range(quadros):
            img, alfa = angulos[i]
            q = img.convert('RGBA')
            q.putalpha(alfa)
            tira.paste(q.crop(caixa).resize((larg, altura), Image.LANCZOS), (i * larg, 0))
        destino = os.path.join(saida, nome + '.webp')
        tira.save(destino, 'WEBP', quality=qualidade, method=6, alpha_quality=90)
        total += os.path.getsize(destino)
    with open(os.path.join(saida, 'lobby.json'), 'w') as f:
        json.dump({'quadros': quadros, 'largura': larg, 'altura': altura}, f)
    print(f'{len(camadas)} camadas, {quadros} ângulos de {larg}x{altura}, {total // 1024} KB em {saida}')


if __name__ == '__main__':
    main()
