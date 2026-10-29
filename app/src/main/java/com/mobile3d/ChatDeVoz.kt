package com.mobile3d

import android.annotation.SuppressLint
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.media.audiofx.AcousticEchoCanceler
import android.media.audiofx.NoiseSuppressor
import java.util.ArrayDeque

// Chat de voz do time: 8 kHz, mono, μ-law (o mesmo do telefone), pacotes de 20 ms.
// Falar: enquanto o botão do microfone estiver apertado, grava e manda (o servidor entrega só para o seu time).
// Ouvir: cada pessoa tem a sua fila (com folga de 60 ms contra a internet oscilando) e as vozes são somadas.
class ChatDeVoz(private val enviar: (ByteArray) -> Unit) {
    companion object {
        const val TAXA = 8000
        const val QUADRO = 160  // 20 ms

        private val decodifica = ShortArray(256).also { t ->
            for (i in 0 until 256) {
                val u = i.inv() and 0xFF
                var s = ((u and 0x0F) shl 3) + 0x84
                s = s shl ((u and 0x70) shr 4)
                t[i] = (if (u and 0x80 != 0) 0x84 - s else s - 0x84).toShort()
            }
        }

        fun codifica(amostra: Int): Byte {
            var s = amostra.coerceIn(-32635, 32635)
            val sinal = if (s < 0) { s = -s; 0x80 } else 0
            s += 0x84
            var exp = 7
            var mask = 0x4000
            while (exp > 0 && s and mask == 0) { exp--; mask = mask shr 1 }
            val mant = (s shr (exp + 3)) and 0x0F
            return ((sinal or (exp shl 4) or mant).inv() and 0xFF).toByte()
        }
    }

    @Volatile var falando = false
    @Volatile var ouvindo = true
    @Volatile private var ligado = false
    private val filas = HashMap<Int, ArrayDeque<ShortArray>>()
    private var gravacao: Thread? = null
    private var mistura: Thread? = null

    fun ligar() {
        if (ligado) return
        ligado = true
        mistura = Thread { tocar() }.apply { isDaemon = true; start() }
    }

    // O microfone só começa depois que o jogador deu a permissão (na 1ª vez que aperta para falar)
    fun ligarMicrofone() {
        if (!ligado || gravacao != null) return
        gravacao = Thread { gravar() }.apply { isDaemon = true; start() }
    }

    fun desligar() {
        ligado = false
        falando = false
        gravacao?.interrupt(); mistura?.interrupt()
        gravacao = null; mistura = null
        synchronized(filas) { filas.clear() }
    }

    // pacote recebido (μ-law) de quem está na vaga `quem`
    fun receber(quem: Int, dados: ByteArray) {
        if (!ouvindo || !ligado) return
        val pcm = ShortArray(dados.size) { decodifica[dados[it].toInt() and 0xFF] }
        synchronized(filas) {
            val f = filas.getOrPut(quem) { ArrayDeque() }
            f.addLast(pcm)
            while (f.size > 12) f.removeFirst()  // atrasou demais: joga fora o mais velho
        }
    }

    @SuppressLint("MissingPermission")
    private fun gravar() {
        val min = AudioRecord.getMinBufferSize(TAXA, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        val rec = try {
            AudioRecord(MediaRecorder.AudioSource.VOICE_COMMUNICATION, TAXA, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, maxOf(min, QUADRO * 8))
        } catch (_: Exception) { return }
        if (rec.state != AudioRecord.STATE_INITIALIZED) { rec.release(); return }
        val eco = if (AcousticEchoCanceler.isAvailable()) AcousticEchoCanceler.create(rec.audioSessionId)?.apply { enabled = true } else null
        val ruido = if (NoiseSuppressor.isAvailable()) NoiseSuppressor.create(rec.audioSessionId)?.apply { enabled = true } else null
        val buf = ShortArray(QUADRO)
        try {
            rec.startRecording()
            while (ligado && !Thread.currentThread().isInterrupted) {
                var n = 0
                while (n < QUADRO) { val r = rec.read(buf, n, QUADRO - n); if (r <= 0) break; n += r }
                if (!falando || n < QUADRO) continue
                enviar(ByteArray(QUADRO) { codifica(buf[it].toInt()) })
            }
        } catch (_: Exception) {
        } finally {
            try { rec.stop() } catch (_: Exception) {}
            rec.release(); eco?.release(); ruido?.release()
        }
    }

    private fun tocar() {
        val min = AudioTrack.getMinBufferSize(TAXA, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT)
        val track = try {
            AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                .setAudioFormat(AudioFormat.Builder().setSampleRate(TAXA).setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                .setBufferSizeInBytes(maxOf(min, QUADRO * 2 * 6))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
        } catch (_: Exception) { return }
        val prontas = HashSet<Int>()  // filas que já juntaram a folga inicial
        val soma = IntArray(QUADRO)
        val saida = ShortArray(QUADRO)
        try {
            track.play()
            while (ligado && !Thread.currentThread().isInterrupted) {
                java.util.Arrays.fill(soma, 0)
                var alguem = false
                synchronized(filas) {
                    for ((quem, f) in filas) {
                        if (quem !in prontas) { if (f.size >= 3) prontas.add(quem) else continue }
                        val q = f.pollFirst()
                        if (q == null) { prontas.remove(quem); continue }
                        for (i in 0 until minOf(QUADRO, q.size)) soma[i] += q[i].toInt()
                        alguem = true
                    }
                }
                if (alguem) {
                    for (i in 0 until QUADRO) saida[i] = soma[i].coerceIn(-32768, 32767).toShort()
                    track.write(saida, 0, QUADRO)  // bloqueia o tempo do quadro: dá o ritmo
                } else {
                    try { Thread.sleep(10) } catch (_: InterruptedException) { break }
                }
            }
        } catch (_: Exception) {
        } finally {
            try { track.stop() } catch (_: Exception) {}
            track.release()
        }
    }
}
