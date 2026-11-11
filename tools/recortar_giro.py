#!/usr/bin/env python3
"""Monta as armas giratórias da loja: recorta os quadros renderizados pelo motor (SO_GIRO no favela_testes) e junta
os ângulos de cada arma numa tira (WebP com fundo transparente). Na loja, arrastar o dedo no card gira a arma.

Uso: python3 tools/recortar_giro.py <pasta_do_teste> <pasta_saida> [altura=160] [qualidade_webp=80]

Entrada: giro_<nome>_fundo.png (sem a arma) e giro_<nome>_NN.png (um por ângulo).
Saída: <nome>.webp (quadros lado a lado, todos do mesmo tamanho: a caixa de todos os ângulos da arma) e giro.json
       ({nome: {quadros, largura, altura}}, juntando com o que já existir lá).
"""
import json
import os
import re
import sys

from PIL import Image, ImageChops, ImageFilter


def alfa_de(img, fundo):
    dif = ImageChops.difference(img.convert('RGB'), fundo.convert('RGB')).convert('L')
    return dif.point(lambda v: 0 if v < 6 else min(255, (v - 6) * 12)).filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(0.7))


def main():
    entrada, saida = sys.argv[1], sys.argv[2]
    altura = int(sys.argv[3]) if len(sys.argv) > 3 else 160
    qualidade = int(sys.argv[4]) if len(sys.argv) > 4 else 80
    os.makedirs(saida, exist_ok=True)
    quadros = {}
    for n in sorted(os.listdir(entrada)):
        m = re.match(r'giro_(.+)_(\d\d)\.png$', n)
        if m:
            quadros.setdefault(m[1], {})[int(m[2])] = os.path.join(entrada, n)
    caminho_json = os.path.join(saida, 'giro.json')
    info = json.load(open(caminho_json)) if os.path.exists(caminho_json) else {}
    for nome, angs in sorted(quadros.items()):
        fundo = Image.open(os.path.join(entrada, f'giro_{nome}_fundo.png'))
        n = len(angs)
        camadas = []
        caixa = None
        for i in range(n):
            img = Image.open(angs[i])
            a = alfa_de(img, fundo)
            camadas.append((img, a))
            # a caixa vem só da arma: os pontinhos soltos (partículas e cristais da skin) somem na erosão
            b = a.point(lambda v: 255 if v > 40 else 0).filter(ImageFilter.MinFilter(7)).getbbox()
            if b:
                caixa = b if caixa is None else (min(caixa[0], b[0]), min(caixa[1], b[1]), max(caixa[2], b[2]), max(caixa[3], b[3]))
        if not caixa:
            print('sem arma:', nome)
            continue
        m = 14
        caixa = (max(0, caixa[0] - m), max(0, caixa[1] - m), min(fundo.width, caixa[2] + m), min(fundo.height, caixa[3] + m))
        esc = altura / (caixa[3] - caixa[1])
        larg = max(1, round((caixa[2] - caixa[0]) * esc))
        if larg > 400:  # arma comprida: limita a largura (a altura acompanha)
            esc = 400 / (caixa[2] - caixa[0])
            larg, alt = 400, max(1, round((caixa[3] - caixa[1]) * esc))
        else:
            alt = altura
        tira = Image.new('RGBA', (larg * n, alt), (0, 0, 0, 0))
        for i, (img, a) in enumerate(camadas):
            q = img.convert('RGBA')
            q.putalpha(a)
            tira.paste(q.crop(caixa).resize((larg, alt), Image.LANCZOS), (i * larg, 0))
        destino = os.path.join(saida, nome + '.webp')
        tira.save(destino, 'WEBP', quality=qualidade, method=6, alpha_quality=90)
        info[nome] = {'quadros': n, 'largura': larg, 'altura': alt}
        print(f'{nome}: {n} ângulos de {larg}x{alt}, {os.path.getsize(destino) // 1024} KB')
    json.dump(info, open(caminho_json, 'w'), separators=(',', ':'), sort_keys=True)


if __name__ == '__main__':
    main()
