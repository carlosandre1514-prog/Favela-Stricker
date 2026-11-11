#!/usr/bin/env python3
"""Som de chuva do jogo (app/src/main/assets/sons/chuva.ogg): 24 s em loop perfeito, estéreo.

Feito por síntese, sem gravação de terceiros:
  - chiado da chuva: ruído filtrado no espectro de chuva de verdade (sobe até ~3 kHz, cai devagar até 12 kHz), um ruído
    diferente em cada ouvido (som largo, em volta)
  - gotas: milhares de estalinhos curtos com timbre sorteado (ressonância de 1,5 a 7 kHz), umas perto e outras longe
  - pingos grandes caindo em telhado/poça (poucos, mais graves, "plic")
  - fundo grave de chuva longe (abaixo de 250 Hz) e rajadas lentas (o volume respira devagar)
Tudo é circular (filtros por FFT no buffer inteiro, gotas com volta pelo fim, rajadas com período que divide o
tamanho): o fim emenda no começo sem estalo.

Uso: python3 tools/gerar_chuva.py app/src/main/assets/sons/chuva.ogg   (precisa do ffmpeg)
"""
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

SR = 32000
DUR = 24.0
N = int(SR * DUR)
rng = np.random.default_rng(20261111)


def ruido_filtrado(forma):
    """Ruído branco com o espectro `forma(f)` (filtro circular por FFT)."""
    x = rng.standard_normal(N)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(N, 1 / SR)
    X *= forma(f)
    y = np.fft.irfft(X, N)
    return y / (np.abs(y).max() + 1e-9)


def espectro_chuva(f):
    sobe = 1 / (1 + (1200 / np.maximum(f, 1)) ** 2)          # passa-alta suave
    cai = 1 / (1 + (f / 5500) ** 1.6)                         # cai devagar no agudo
    corte = 1 / (1 + (f / 13000) ** 8)
    pico = 1 + 0.6 * np.exp(-((f - 2800) / 1600) ** 2)        # "corpo" da chuva
    return sobe * cai * corte * pico


def gotas(densidade, f_lo, f_hi, dec_lo, dec_hi, amp_lo, amp_hi):
    """Estalinhos (seno amortecido com ataque curtinho) espalhados no tempo, com volta pelo fim."""
    out = np.zeros(N)
    n = int(densidade * DUR)
    t0 = rng.integers(0, N, n)
    fr = np.exp(rng.uniform(np.log(f_lo), np.log(f_hi), n))
    dec = rng.uniform(dec_lo, dec_hi, n)
    amp = np.exp(rng.uniform(np.log(amp_lo), np.log(amp_hi), n))
    L = int(SR * dec_hi * 6)
    tt = np.arange(L) / SR
    for i in range(n):
        env = (1 - np.exp(-tt / 0.0004)) * np.exp(-tt / dec[i])
        g = amp[i] * env * np.sin(2 * np.pi * fr[i] * tt * (1 - 0.15 * tt / (dec[i] * 6)))  # o tom cai um pouco
        idx = (t0[i] + np.arange(L)) % N
        out[idx] += g
    return out


def canal(semente_extra):
    chiado = ruido_filtrado(espectro_chuva) * 0.55
    longe = ruido_filtrado(lambda f: 1 / (1 + (f / 220) ** 4) * (f > 40)) * 0.1
    finas = gotas(1400, 1500, 7000, 0.0015, 0.004, 0.01, 0.06)
    perto = gotas(55, 900, 3500, 0.004, 0.012, 0.05, 0.22)
    plic = gotas(6, 500, 1400, 0.01, 0.03, 0.08, 0.28)
    return chiado + longe + finas + perto + plic


def main():
    saida = sys.argv[1]
    t = np.arange(N) / SR
    # rajadas: 3 senos lentos com período que divide 24 s (o loop emenda)
    rajada = 0.82 + 0.1 * np.sin(2 * np.pi * t / 24 + 0.3) + 0.06 * np.sin(2 * np.pi * t / 8 + 1.1) + 0.03 * np.sin(2 * np.pi * t / 4.8)
    esq, dir_ = canal(1) * rajada, canal(2) * rajada
    # um pouco de cada lado no outro (não fica "dentro da cabeça")
    L = esq * 0.85 + dir_ * 0.15
    R = dir_ * 0.85 + esq * 0.15
    pico = max(np.abs(L).max(), np.abs(R).max())
    L, R = L / pico * 0.7, R / pico * 0.7
    pcm = (np.stack([L, R], 1) * 32767).astype('<i2')
    with tempfile.TemporaryDirectory() as tmp:
        w = os.path.join(tmp, 'chuva.wav')
        with wave.open(w, 'wb') as f:
            f.setnchannels(2)
            f.setsampwidth(2)
            f.setframerate(SR)
            f.writeframes(pcm.tobytes())
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', w, '-c:a', 'libvorbis', '-q:a', '4', saida], check=True)
    print(saida, os.path.getsize(saida) // 1024, 'KB')


if __name__ == '__main__':
    main()
