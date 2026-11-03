#!/usr/bin/env python3
"""Molde de skin do boneco em blocos (estilo Roblox): cada bloco do corpo aberto como uma caixa desmontada.

Gera:
  shaders/kit/personagens/molde.json   onde fica cada face na imagem (0 a 1), lido pelo motor
  docs/skins/molde_personagem.png      o molde para pintar (2048 x 2048), com o nome de cada parte
  docs/skins/molde_limpo.png           o mesmo molde só com as bordas (para colar na IA ou pintar por cima)
  docs/skins/exemplo_pm.png            uma skin de exemplo (policial) pintada no molde
  docs/skins/molde_cabeca.png          só a cabeça, grande, com as linhas do rosto (cabelo, olhos, nariz, boca, queixo)

Cada parte é uma caixa aberta em cruz:  [LADO DIREITO][FRENTE][LADO ESQUERDO][COSTAS], com o TOPO em cima
da FRENTE e a BASE embaixo dela. "Direito/esquerdo" são do boneco (o lado direito dele fica à esquerda
de quem olha de frente). As medidas são as do motor (src/procgen/Character.cpp, buildBlockBody).

Uso: python3 tools/gerar_molde_personagem.py
"""
import json
import os

from PIL import Image, ImageDraw, ImageFont

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
H = 1.75  # altura do boneco (m)

# Partes: largura (x), altura (y), profundidade (z) em metros, escala (px por metro no molde de 1024) e canto.
# Partes com mais de um bloco têm divisões (frações da altura, de cima para baixo) que o motor usa.
PARTES = {
    'tronco':    dict(w=0.24 * H, h=0.34 * H, d=0.12 * H, s=400, o=(4, 4), nome='TRONCO',
                      div=[('peito', 0.16 / 0.34), ('barriga', 0.24 / 0.34), ('cintura', 1.0)]),
    'cabeca':    dict(w=0.15 * H, h=0.15 * H, d=0.14 * H, s=500, o=(514, 4), nome='CABEÇA', div=[]),
    'braco_dir': dict(w=0.085 * H, h=0.4 * H, d=0.085 * H, s=380, o=(4, 418), nome='BRAÇO DIREITO',
                      div=[('ombro', 0.18 / 0.4), ('antebraço', 0.33 / 0.4), ('mão', 1.0)]),
    'braco_esq': dict(w=0.085 * H, h=0.4 * H, d=0.085 * H, s=380, o=(236, 418), nome='BRAÇO ESQUERDO',
                      div=[('ombro', 0.18 / 0.4), ('antebraço', 0.33 / 0.4), ('mão', 1.0)]),
    'perna_dir': dict(w=0.115 * H, h=0.455 * H, d=0.114 * H, s=315, o=(514, 390), nome='PERNA DIREITA',
                      div=[('coxa', 0.24 / 0.455), ('canela', 1.0)]),
    'perna_esq': dict(w=0.115 * H, h=0.455 * H, d=0.114 * H, s=315, o=(768, 390), nome='PERNA ESQUERDA',
                      div=[('coxa', 0.24 / 0.455), ('canela', 1.0)]),
    'pe_dir':    dict(w=0.1127 * H, h=0.06 * H, d=0.138 * H, s=300, o=(4, 806), nome='PÉ DIREITO', div=[]),
    'pe_esq':    dict(w=0.1127 * H, h=0.06 * H, d=0.138 * H, s=300, o=(270, 806), nome='PÉ ESQUERDO', div=[]),
}
FACES = ['frente', 'costas', 'lado_esq', 'lado_dir', 'topo', 'base']  # mesma ordem do motor (+Z, -Z, +X, -X, +Y, -Y)
NOME_FACE = {'frente': 'FRENTE', 'costas': 'COSTAS', 'lado_esq': 'LADO ESQ.', 'lado_dir': 'LADO DIR.', 'topo': 'TOPO', 'base': 'BASE'}
CORES = {'tronco': (110, 160, 230), 'cabeca': (250, 200, 120), 'braco_dir': (130, 210, 150), 'braco_esq': (130, 210, 150),
         'perna_dir': (200, 150, 230), 'perna_esq': (200, 150, 230), 'pe_dir': (240, 140, 140), 'pe_esq': (240, 140, 140)}


def faces_px(p):
    """Retângulos (x0, y0, x1, y1) em pixels do molde de 1024 para cada face da parte."""
    s, (ox, oy) = p['s'], p['o']
    W, Hh, D = p['w'] * s, p['h'] * s, p['d'] * s
    y0, y1 = oy + D, oy + D + Hh
    xr = ox                 # lado direito do boneco (-X), à esquerda de quem olha
    xf = xr + D             # frente (+Z)
    xl = xf + W             # lado esquerdo do boneco (+X)
    xb = xl + D             # costas (-Z)
    return {
        'lado_dir': (xr, y0, xf, y1), 'frente': (xf, y0, xl, y1), 'lado_esq': (xl, y0, xb, y1), 'costas': (xb, y0, xb + W, y1),
        'topo': (xf, oy, xl, y0), 'base': (xf, y1, xl, y1 + D),
    }


def fonte(t, negrito=True):
    nome = 'DejaVuSans-Bold.ttf' if negrito else 'DejaVuSans.ttf'
    return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/' + nome, t)


def desenhar_molde(rotulos=True, tam=2048):
    k = tam / 1024
    img = Image.new('RGB', (tam, tam), (245, 245, 245) if rotulos else (255, 255, 255))
    d = ImageDraw.Draw(img)
    for chave, p in PARTES.items():
        f = faces_px(p)
        cor = CORES[chave]
        for nome, (x0, y0, x1, y1) in f.items():
            r = [x0 * k, y0 * k, x1 * k, y1 * k]
            if rotulos:
                clara = tuple(int(c + (255 - c) * (0.25 if nome == 'frente' else 0.55)) for c in cor)
                d.rectangle(r, fill=clara)
            d.rectangle(r, outline=(30, 30, 30), width=max(2, int(2 * k)))
            if rotulos:
                txt = NOME_FACE[nome]
                ft = fonte(max(11, int(min(x1 - x0, y1 - y0) * k * 0.13)))
                tw = d.textlength(txt, font=ft)
                d.text(((r[0] + r[2] - tw) / 2, r[1] + 4 * k), txt, fill=(20, 20, 20), font=ft)
        # divisões das partes com mais de um bloco (nas laterais, frente e costas)
        topo_y, base_y = f['frente'][1], f['frente'][3]
        ant = 0.0
        for nome_div, fim in p['div']:
            ya = topo_y + (base_y - topo_y) * ant
            yb = topo_y + (base_y - topo_y) * fim
            if fim < 1.0:
                x0, x1 = f['lado_dir'][0], f['costas'][2]
                for x in range(int(x0 * k), int(x1 * k), int(12 * k)):
                    d.line([x, yb * k, min(x + 6 * k, x1 * k), yb * k], fill=(60, 60, 60), width=max(1, int(k)))
            if rotulos:
                ft = fonte(max(10, int(13 * k)), False)
                fr = f['frente']
                d.text((fr[0] * k + 4 * k, ((ya + yb) / 2) * k - 6 * k), nome_div, fill=(40, 40, 40), font=ft)
            ant = fim
        if rotulos:  # nome da parte embaixo do bloco
            fr = f['base']
            ft = fonte(int(16 * k))
            txt = p['nome']
            tw = d.textlength(txt, font=ft)
            cx = (f['lado_dir'][0] + f['costas'][2]) / 2 * k
            d.text((cx - tw / 2, fr[3] * k + 2 * k), txt, fill=(10, 10, 10), font=ft)
    if rotulos:
        ft, ft2 = fonte(int(18 * k)), fonte(int(12 * k), False)
        x, y = 560 * k, 790 * k
        d.text((x, y), 'MOLDE DA SKIN · Favela Stricker', fill=(10, 10, 10), font=ft)
        linhas = ['Pinte cada retângulo como o boneco é visto daquele lado.',
                  'FRENTE = olhando o boneco de frente (rosto, peito).',
                  'LADO DIR./ESQ. = do próprio boneco.',
                  'TOPO fica em cima da FRENTE; BASE embaixo dela.',
                  'Tracejado = onde um bloco acaba e o outro começa.',
                  'Fora dos retângulos não aparece no jogo.',
                  'Salve em PNG quadrado (1024 ou 2048).']
        for i, l in enumerate(linhas):
            d.text((x, y + (34 + i * 22) * k), l, fill=(40, 40, 40), font=ft2)
    return img


def pintar_exemplo(tam=1024):
    """Skin de exemplo: policial com farda azul, colete preto, rosto e boné (mostra o encaixe das partes)."""
    k = tam / 1024
    img = Image.new('RGB', (tam, tam), (90, 110, 140))
    d = ImageDraw.Draw(img)
    R = lambda r: [r[0] * k, r[1] * k, r[2] * k, r[3] * k]
    pele, farda, calca, colete, preto = (198, 140, 105), (100, 125, 165), (35, 45, 70), (28, 28, 32), (15, 15, 18)
    for chave in ('tronco', 'braco_dir', 'braco_esq'):
        for n, r in faces_px(PARTES[chave]).items():
            d.rectangle(R(r), fill=farda)
    # mãos de pele (último bloco do braço)
    for chave in ('braco_dir', 'braco_esq'):
        f = faces_px(PARTES[chave])
        for n in ('frente', 'costas', 'lado_esq', 'lado_dir'):
            x0, y0, x1, y1 = f[n]
            d.rectangle(R((x0, y0 + (y1 - y0) * 0.825, x1, y1)), fill=pele)
        d.rectangle(R(f['base']), fill=pele)
    # tronco: colete no peito e na barriga, cinto na cintura, calça embaixo
    f = faces_px(PARTES['tronco'])
    for n in ('frente', 'costas', 'lado_esq', 'lado_dir'):
        x0, y0, x1, y1 = f[n]
        h = y1 - y0
        d.rectangle(R((x0, y0 + h * 0.05, x1, y0 + h * 0.68)), fill=colete)
        d.rectangle(R((x0, y0 + h * 0.70, x1, y0 + h * 0.76)), fill=preto)
        d.rectangle(R((x0, y0 + h * 0.76, x1, y1)), fill=calca)
    x0, y0, x1, y1 = f['frente']
    ft = fonte(int(26 * k))
    tw = d.textlength('POLÍCIA', font=ft)
    d.text(((x0 + x1) / 2 * k - tw / 2, (y0 + (y1 - y0) * 0.25) * k), 'POLÍCIA', fill=(240, 240, 240), font=ft)
    x0, y0, x1, y1 = f['costas']
    tw = d.textlength('POLÍCIA', font=ft)
    d.text(((x0 + x1) / 2 * k - tw / 2, (y0 + (y1 - y0) * 0.25) * k), 'POLÍCIA', fill=(240, 240, 240), font=ft)
    # pernas e pés
    for chave in ('perna_dir', 'perna_esq'):
        for n, r in faces_px(PARTES[chave]).items():
            d.rectangle(R(r), fill=calca)
    for chave in ('pe_dir', 'pe_esq'):
        for n, r in faces_px(PARTES[chave]).items():
            d.rectangle(R(r), fill=preto)
    # cabeça: pele, rosto na frente, boné em cima
    f = faces_px(PARTES['cabeca'])
    for n, r in f.items():
        d.rectangle(R(r), fill=pele)
    for n in ('frente', 'costas', 'lado_esq', 'lado_dir'):
        x0, y0, x1, y1 = f[n]
        d.rectangle(R((x0, y0, x1, y0 + (y1 - y0) * 0.24)), fill=calca)
    d.rectangle(R(f['topo']), fill=calca)
    x0, y0, x1, y1 = f['frente']
    w, h = x1 - x0, y1 - y0
    for ex in (0.32, 0.68):
        d.ellipse(R((x0 + w * ex - w * 0.06, y0 + h * 0.4, x0 + w * ex + w * 0.06, y0 + h * 0.58)), fill=preto)
    d.arc(R((x0 + w * 0.3, y0 + h * 0.5, x0 + w * 0.7, y0 + h * 0.8)), 20, 160, fill=preto, width=int(6 * k))
    return img


# Proporção do rosto na FRENTE da cabeça (fração da altura, de cima para baixo)
ROSTO = [('linha do cabelo', 0.16), ('sobrancelhas', 0.36), ('olhos', 0.45), ('ponta do nariz', 0.62), ('boca', 0.74),
         ('queixo', 0.92)]


def desenhar_molde_cabeca(tam=1024):
    """A cabeça aberta em cruz, grande, com as linhas de proporção do rosto na FRENTE e das orelhas nos lados."""
    p = PARTES['cabeca']
    W, Hh, D = p['w'], p['h'], p['d']
    s = (tam - 40) / (2 * W + 2 * D)
    ox, oy = 20, (tam - (Hh + 2 * D) * s) / 2
    img = Image.new('RGB', (tam, tam), (245, 245, 245))
    d = ImageDraw.Draw(img)
    xr, xf = ox, ox + D * s
    xl, xb = xf + W * s, xf + W * s + D * s
    y0, y1 = oy + D * s, oy + D * s + Hh * s
    caixas = {'LADO DIR.': (xr, y0, xf, y1), 'FRENTE (rosto)': (xf, y0, xl, y1), 'LADO ESQ.': (xl, y0, xb, y1),
              'COSTAS (nuca)': (xb, y0, xb + W * s, y1), 'TOPO (cabelo)': (xf, oy, xl, y0), 'BASE (pescoço)': (xf, y1, xl, y1 + D * s)}
    for nome, r in caixas.items():
        d.rectangle(r, fill=(252, 226, 190) if 'FRENTE' in nome else (250, 236, 214), outline=(30, 30, 30), width=3)
        ft = fonte(18)
        d.text((r[0] + 6, r[1] + 4), nome, fill=(20, 20, 20), font=ft)
    fx0, fy0, fx1, fy1 = caixas['FRENTE (rosto)']
    fw, fh = fx1 - fx0, fy1 - fy0
    ft = fonte(14, False)
    for nome, fr in ROSTO:
        y = fy0 + fh * fr
        for x in range(int(fx0), int(fx1), 12):
            d.line([x, y, min(x + 6, fx1), y], fill=(180, 40, 40), width=2)
        d.text((fx1 - d.textlength(nome, font=ft) - 4, y - 17), nome, fill=(150, 30, 30), font=ft)
    # olhos: dois quadros entre 25-42% e 58-75% da largura; boca entre 35% e 65%
    for a, b in ((0.25, 0.42), (0.58, 0.75)):
        d.rectangle([fx0 + fw * a, fy0 + fh * 0.4, fx0 + fw * b, fy0 + fh * 0.5], outline=(40, 80, 180), width=2)
    d.rectangle([fx0 + fw * 0.35, fy0 + fh * 0.7, fx0 + fw * 0.65, fy0 + fh * 0.78], outline=(40, 80, 180), width=2)
    # orelhas nos lados (perto da nuca), na altura dos olhos até o nariz
    for (x0, _, x1, _), perto in ((caixas['LADO DIR.'], 0.25), (caixas['LADO ESQ.'], 0.75)):
        cx = x0 + (x1 - x0) * perto
        d.ellipse([cx - (x1 - x0) * 0.1, fy0 + fh * 0.42, cx + (x1 - x0) * 0.1, fy0 + fh * 0.64], outline=(40, 80, 180), width=2)
    for nome in ('LADO DIR.', 'LADO ESQ.', 'COSTAS (nuca)'):
        r = caixas[nome]
        for x in range(int(r[0]), int(r[2]), 12):
            d.line([x, fy0 + fh * 0.16, min(x + 6, r[2]), fy0 + fh * 0.16], fill=(180, 40, 40), width=2)
    ft = fonte(16, False)
    txt = ['Rosto inteiro dentro da FRENTE, do tamanho do quadrado: queixo encostando embaixo,',
           'cabelo só no alto (até a linha do cabelo). Orelhas nos LADOS, não na frente.',
           'Pescoço na BASE; cabelo no TOPO e nas COSTAS. Sem fundo preto em volta.']
    for i, l in enumerate(txt):
        d.text((20, tam - 90 + i * 24), l, fill=(30, 30, 30), font=ft)
    return img


def main():
    rects = {}
    for chave, p in PARTES.items():
        rects[chave] = {n: [round(v / 1024, 6) for v in r] for n, r in faces_px(p).items()}
        rects[chave]['divisoes'] = [[n, round(f, 6)] for n, f in p['div']]
    saida_json = os.path.join(RAIZ, 'shaders', 'kit', 'personagens', 'molde.json')
    with open(saida_json, 'w', encoding='utf-8') as f:
        json.dump({'faces': FACES, 'partes': rects}, f, ensure_ascii=False, indent=1)
    pasta = os.path.join(RAIZ, 'docs', 'skins')
    os.makedirs(pasta, exist_ok=True)
    desenhar_molde(True).save(os.path.join(pasta, 'molde_personagem.png'), optimize=True)
    desenhar_molde(False).save(os.path.join(pasta, 'molde_limpo.png'), optimize=True)
    desenhar_molde_cabeca().save(os.path.join(pasta, 'molde_cabeca.png'), optimize=True)
    pintar_exemplo().save(os.path.join(pasta, 'exemplo_pm.png'), optimize=True)
    print('ok:', saida_json, pasta)


if __name__ == '__main__':
    main()
