#!/usr/bin/env python3
"""Põe o som do jogo num vídeo gravado pelo motor (SO_VIDEO no favela_testes).

Uso: python3 tools/mixar_sons_video.py <video.mp4> <sons.txt> <pasta_dos_sons> <saida.mp4> [titulo]

sons.txt: uma linha por som pedido pelo jogo: "quadro<TAB>nome<TAB>volume<TAB>pan" (30 quadros por segundo; o
volume já vem com a distância e o pan -1..1 com o lado em relação à câmera, como no app). Cada som entra no tempo do
quadro, com o volume e o lado certos; o resultado vai junto do vídeo. Com título, os 2,5 s do começo mostram o texto.
"""
import os
import subprocess
import sys
import wave

import numpy as np

FPS, TAXA = 30, 44100


def ler_wav(caminho):
    with wave.open(caminho) as w:
        dados = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
        if w.getnchannels() == 2:
            dados = dados.reshape(-1, 2).mean(axis=1)
        return dados


def main():
    video, sons_txt, pasta, saida = sys.argv[1:5]
    titulo = sys.argv[5] if len(sys.argv) > 5 else ''
    dur = float(subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', video]).decode())
    mix = np.zeros((int(dur * TAXA) + TAXA, 2), dtype=np.float32)
    cache = {}
    n = 0
    for linha in open(sons_txt, encoding='utf-8'):
        partes = linha.rstrip('\n').split('\t')
        if len(partes) < 4:
            continue
        quadro, nome, vol, pan = int(partes[0]), partes[1], float(partes[2]), float(partes[3])
        if nome not in cache:
            arq = os.path.join(pasta, nome + '.wav')
            if not os.path.exists(arq) and nome.endswith('_longe'):
                arq = os.path.join(pasta, nome[:-6] + '.wav')
            cache[nome] = ler_wav(arq) if os.path.exists(arq) else None
        s = cache[nome]
        if s is None:
            continue
        i = int(quadro / FPS * TAXA)
        trecho = s[: max(0, len(mix) - i)] * vol
        esq, dir_ = np.sqrt(0.5 * (1 - pan)), np.sqrt(0.5 * (1 + pan))  # pan de potência constante
        mix[i:i + len(trecho), 0] += trecho * esq
        mix[i:i + len(trecho), 1] += trecho * dir_
        n += 1
    pico = np.abs(mix).max()
    if pico > 0.95:  # limita sem distorcer: comprime só o que passa do teto
        mix = np.tanh(mix / pico * 1.6) / np.tanh(1.6) * 0.95
    pcm = (np.clip(mix, -1, 1) * 32767).astype(np.int16)
    wav_tmp = saida + '.som.wav'
    with wave.open(wav_tmp, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(TAXA)
        w.writeframes(pcm.tobytes())
    filtro = []
    if titulo:
        fonte = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
        texto = titulo.replace(':', r'\:').replace("'", '')
        filtro = ['-vf', f"drawbox=x=0:y=ih*0.38:w=iw:h=ih*0.24:color=black@0.55:t=fill:enable='lt(t,2.5)',"
                         f"drawtext=fontfile={fonte}:text='{texto}':fontcolor=white:fontsize=h/14:x=(w-tw)/2:y=(h-th)/2:enable='lt(t,2.5)'"]
    cmd = ['ffmpeg', '-loglevel', 'error', '-y', '-i', video, '-i', wav_tmp, *filtro,
           '-c:v', 'libx264' if filtro else 'copy', '-preset', 'veryfast', '-crf', '25', '-c:a', 'aac', '-b:a', '160k', '-shortest', saida]
    if not filtro:
        cmd = [c for c in cmd if c not in ('-preset', 'veryfast', '-crf', '25')]
    subprocess.check_call(cmd)
    os.remove(wav_tmp)
    print(f'{n} sons mixados em {saida}')


if __name__ == '__main__':
    main()
