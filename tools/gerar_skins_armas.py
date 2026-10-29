#!/usr/bin/env python3
"""Gera as texturas das skins de fuzil do mercado (shaders/kit/armas/skins/<arma>_<skin>.jpg).

Os padrões não dependem do mapeamento UV de cada arma (camuflagem, escamas, listras, ouro escovado),
então a mesma pintura serve em qualquer modelo. Uso: python3 tools/gerar_skins_armas.py
"""
import math
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter

TAM = 512
SAIDA = os.path.join(os.path.dirname(__file__), '..', 'shaders', 'kit', 'armas', 'skins')

# skin -> armas que a vendem (o catálogo do jogo fica em web/jogo.js)
CATALOGO = {
    'dourada': ['ak47', 'm4colt'],
    'dragao': ['ak47'],
    'neon': ['m4a1'],
    'favela': ['ak74'],
    'selva': ['ak74_tatica'],
    'urbana': ['m4a1'],
    'deserto': ['ak47'],
}


def ruido(semente, escala, oitavas=4):
    """Ruído suave em tons de cinza (soma de camadas de ruído aumentadas)."""
    rnd = random.Random(semente)
    img = Image.new('L', (TAM, TAM), 128)
    peso_total = 0.0
    acc = None
    for o in range(oitavas):
        n = max(2, int(escala * (2 ** o)))
        camada = Image.new('L', (n, n))
        camada.putdata([rnd.randrange(256) for _ in range(n * n)])
        camada = camada.resize((TAM, TAM), Image.BICUBIC)
        peso = 0.5 ** o
        acc = camada if acc is None else Image.blend(acc, camada, peso / (peso_total + peso))
        peso_total += peso
    return acc if acc is not None else img


def camuflagem(semente, cores):
    """Manchas em camadas (a primeira cor é o fundo)."""
    img = Image.new('RGB', (TAM, TAM), cores[0])
    for i, cor in enumerate(cores[1:]):
        n = ruido(semente + i * 17, 3, 3).filter(ImageFilter.GaussianBlur(6))
        limite = 150 - i * 6
        mascara = n.point(lambda v, t=limite: 255 if v > t else 0).filter(ImageFilter.ModeFilter(9))
        img.paste(Image.new('RGB', (TAM, TAM), cor), (0, 0), mascara)
    grao = ruido(semente + 99, 64, 2).point(lambda v: 118 + (v - 128) // 6)
    return ImageChops.overlay(img, Image.merge('RGB', [grao] * 3))


def dourada():
    # ouro escovado: listras finas horizontais + leve variação
    img = Image.new('RGB', (TAM, TAM))
    rnd = random.Random(3)
    px = img.load()
    linhas = [rnd.uniform(-1, 1) for _ in range(TAM)]
    for y in range(TAM):
        for x in range(TAM):
            b = 0.82 + 0.1 * math.sin((x + y * 0.3) / 37.0) + 0.06 * linhas[y]
            px[x, y] = (int(255 * min(1, b * 1.0)), int(200 * b), int(70 * b))
    return img.filter(ImageFilter.GaussianBlur(0.6))


def dragao():
    # escamas vermelhas sobre fundo quase preto
    img = Image.new('RGB', (TAM, TAM), (24, 6, 8))
    d = ImageDraw.Draw(img)
    r = 22
    for linha in range(-1, TAM // (r - 6) + 2):
        y = linha * (r - 6)
        desloc = r if linha % 2 else 0
        for x in range(-r, TAM + r, r * 2):
            cx = x + desloc
            for k in range(r, 4, -3):
                t = k / r
                cor = (int(150 + 90 * (1 - t)), int(20 + 40 * (1 - t)), int(18 + 10 * (1 - t)))
                d.ellipse([cx - k, y - k, cx + k, y + k], fill=cor)
            d.arc([cx - r, y - r, cx + r, y + r], 200, 340, fill=(8, 2, 2), width=3)
    return img.filter(ImageFilter.GaussianBlur(0.8))


def neon():
    # fundo grafite com faixas neon ciano e magenta
    img = Image.new('RGB', (TAM, TAM), (18, 20, 28))
    brilho = Image.new('RGB', (TAM, TAM), (0, 0, 0))
    d = ImageDraw.Draw(brilho)
    rnd = random.Random(11)
    for i in range(14):
        cor = (0, 240, 255) if i % 2 else (255, 40, 200)
        x0 = rnd.randrange(-TAM, TAM)
        w = rnd.randrange(4, 12)
        d.line([(x0, 0), (x0 + TAM, TAM)], fill=cor, width=w)
    halo = brilho.filter(ImageFilter.GaussianBlur(10))
    img = ImageChops.add(img, halo)
    return ImageChops.add(img, brilho)


def favela():
    # retalhos coloridos como as casas do morro, com reboco
    img = Image.new('RGB', (TAM, TAM))
    d = ImageDraw.Draw(img)
    rnd = random.Random(7)
    cores = [(231, 76, 60), (241, 196, 15), (46, 204, 113), (52, 152, 219), (155, 89, 182), (230, 126, 34), (236, 240, 241), (192, 57, 43)]
    y = 0
    while y < TAM:
        h = rnd.randrange(30, 80)
        x = 0
        while x < TAM:
            w = rnd.randrange(40, 120)
            d.rectangle([x, y, x + w, y + h], fill=rnd.choice(cores))
            d.rectangle([x, y, x + w, y + h], outline=(60, 50, 45), width=2)
            x += w
        y += h
    reboco = ruido(5, 40, 3).point(lambda v: 110 + (v - 128) // 3)
    return ImageChops.multiply(img, Image.merge('RGB', [reboco.point(lambda v: min(255, v + 120))] * 3))


GERADORES = {
    'dourada': dourada,
    'dragao': dragao,
    'neon': neon,
    'favela': favela,
    'selva': lambda: camuflagem(21, [(62, 82, 44), (38, 52, 30), (98, 110, 60), (24, 30, 20)]),
    'urbana': lambda: camuflagem(31, [(196, 200, 206), (140, 146, 154), (232, 234, 238), (88, 92, 100)]),
    'deserto': lambda: camuflagem(41, [(196, 168, 120), (160, 128, 86), (220, 200, 160), (120, 92, 60)]),
}


def main():
    os.makedirs(SAIDA, exist_ok=True)
    for skin, armas in CATALOGO.items():
        img = GERADORES[skin]()
        for arma in armas:
            caminho = os.path.join(SAIDA, f'{arma}_{skin}.jpg')
            img.save(caminho, quality=88)
            print(caminho)


if __name__ == '__main__':
    main()
