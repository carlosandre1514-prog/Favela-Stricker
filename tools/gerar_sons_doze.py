#!/usr/bin/env python3
"""Sons da Doze, gerados (sem gravação): a bomba indo para trás e voltando, e o cartucho entrando no tubo.

Uso: python3 tools/gerar_sons_doze.py <pasta_dos_sons>   (app/src/main/assets/sons)
Saída: bomba.wav (~0,45 s: "tec" para trás e "tec" para a frente) e cartucho.wav (~0,15 s: clique do cartucho).
"""
import os
import sys
import wave

import numpy as np

TAXA = 44100


def ruido_filtrado(dur, f_baixo, f_alto, rng):
    n = int(TAXA * dur)
    x = rng.standard_normal(n)
    esp = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1 / TAXA)
    esp[(f < f_baixo) | (f > f_alto)] = 0
    return np.fft.irfft(esp, n)


def estalo(dur, f_baixo, f_alto, queda, rng, tom=None, amp_tom=0.0):
    """Batida metálica curta: ruído numa faixa, com queda exponencial, e um tom de ressonância."""
    n = int(TAXA * dur)
    t = np.arange(n) / TAXA
    s = ruido_filtrado(dur, f_baixo, f_alto, rng)
    s /= np.abs(s).max() + 1e-9
    if tom:
        s += amp_tom * np.sin(2 * np.pi * tom * t) * np.exp(-t * queda * 0.6)
    ataque = np.minimum(1.0, t / 0.0015)
    return s * ataque * np.exp(-t * queda)


def colocar(destino, som, em):
    i = int(em * TAXA)
    j = min(len(destino), i + len(som))
    destino[i:j] += som[: j - i]


def gravar(caminho, s):
    s = s / (np.abs(s).max() + 1e-9) * 0.85
    with wave.open(caminho, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(TAXA)
        w.writeframes((s * 32767).astype('<i2').tobytes())


def main():
    pasta = sys.argv[1]
    rng = np.random.default_rng(12)
    # bomba: trás (deslizar + batida no fim do curso) e frente (deslizar + batida mais seca, travando)
    b = np.zeros(int(TAXA * 0.48))
    colocar(b, 0.25 * ruido_filtrado(0.09, 1500, 6000, rng) * np.linspace(0.2, 1, int(TAXA * 0.09)), 0.0)
    colocar(b, estalo(0.12, 900, 5200, 45, rng, tom=1850, amp_tom=0.35), 0.085)
    colocar(b, 0.6 * estalo(0.05, 60, 300, 70, rng), 0.085)  # corpo da batida (grave)
    colocar(b, 0.22 * ruido_filtrado(0.08, 1800, 7000, rng) * np.linspace(1, 0.2, int(TAXA * 0.08)), 0.2)
    colocar(b, estalo(0.14, 1200, 7500, 40, rng, tom=2400, amp_tom=0.4), 0.27)
    colocar(b, 0.5 * estalo(0.05, 60, 320, 75, rng), 0.27)
    gravar(os.path.join(pasta, 'bomba.wav'), b)
    # cartucho: o polegar empurra o cartucho no tubo (clique + mola)
    c = np.zeros(int(TAXA * 0.16))
    colocar(c, estalo(0.06, 2000, 9000, 90, rng, tom=3100, amp_tom=0.3), 0.0)
    colocar(c, 0.55 * estalo(0.09, 500, 2500, 55, rng, tom=820, amp_tom=0.5), 0.045)
    gravar(os.path.join(pasta, 'cartucho.wav'), c)
    print('bomba.wav e cartucho.wav em', pasta)


if __name__ == '__main__':
    main()
