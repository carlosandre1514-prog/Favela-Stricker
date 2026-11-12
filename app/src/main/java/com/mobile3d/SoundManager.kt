package com.mobile3d

import android.content.Context
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.media.SoundPool
import kotlin.random.Random

// Sons do jogo (assets/sons/*.wav): tiros de cada arma, recarga, impacto, golpes e trovão; a chuva em loop
// (assets/sons/chuva.ogg, tools/gerar_chuva.py). O motor pede os sons (nativePollSounds) e diz quanto está chovendo
// (nativeGetWeather).
// A chuva toca num player só dela (MediaPlayer): no SoundPool ela dividia os 16 canais com os tiros e, numa rajada,
// o Android tirava o canal dela (prioridade mais baixa) — a chuva cortava e voltava falhando junto com os tiros.
class SoundManager(context: Context) {
    private val pool = SoundPool.Builder()
        .setMaxStreams(16)
        .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
        .build()
    private val ids = HashMap<String, Int>()
    private val ready = HashSet<Int>()
    private val rain: MediaPlayer? = try {
        context.assets.openFd("sons/chuva.ogg").use { fd ->
            MediaPlayer().apply {
                setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
                setDataSource(fd.fileDescriptor, fd.startOffset, fd.length)
                isLooping = true
                setVolume(0f, 0f)
                prepare()
            }
        }
    } catch (_: Exception) { null }
    private var rainOn = false
    private var rainVolume = 0f
    private var paused = false
    var enabled = true
    var volume = 1f  // configurações do jogo (efeitos)

    init {
        pool.setOnLoadCompleteListener { _, id, status -> if (status == 0) synchronized(ready) { ready.add(id) } }
        val am = context.assets
        for (f in am.list("sons").orEmpty()) {
            if (!f.endsWith(".wav")) continue
            try { am.openFd("sons/$f").use { ids[f.removeSuffix(".wav")] = pool.load(it, 1) } } catch (_: Exception) {}
        }
    }

    private fun isReady(id: Int) = synchronized(ready) { ready.contains(id) }

    // Linhas "nome\tvolume\tpan" vindas do motor. De longe: usa "<nome>_longe" se existir.
    fun playEvents(text: String) {
        if (!enabled || text.isEmpty()) return
        for (line in text.split('\n')) {
            val p = line.split('\t')
            if (p.size < 3) continue
            val name = if (ids.containsKey(p[0])) p[0] else p[0].removeSuffix("_longe")
            val id = ids[name] ?: continue
            if (!isReady(id)) continue
            val vol = (p[1].toFloatOrNull() ?: continue) * volume
            val pan = (p[2].toFloatOrNull() ?: 0f).coerceIn(-1f, 1f)
            val l = vol * (if (pan > 0) 1f - pan * 0.7f else 1f)
            val r = vol * (if (pan < 0) 1f + pan * 0.7f else 1f)
            // cada tiro um pouco diferente (como na vida real)
            val rate = if (name.startsWith("tiro") || name.startsWith("golpe")) 0.94f + Random.nextFloat() * 0.12f else 1f
            pool.play(id, l, r, if (name.startsWith("tiro")) 2 else 1, 0, rate)
        }
    }

    // Chuva em loop: volume acompanha a intensidade (0..1), subindo e descendo aos poucos
    fun setRain(level: Float) {
        val p = rain ?: return
        val alvo = if (enabled) (level * 0.85f * volume).coerceIn(0f, 1f) else 0f
        if (alvo < 0.01f && rainVolume < 0.01f) {
            if (rainOn) { try { p.pause() } catch (_: Exception) {}; rainOn = false }
            rainVolume = 0f
            return
        }
        val v = rainVolume + (alvo - rainVolume) * 0.15f
        if (kotlin.math.abs(v - rainVolume) > 0.003f || !rainOn) {
            try { p.setVolume(v, v) } catch (_: Exception) {}
            rainVolume = v
        }
        if (!rainOn && !paused) { try { p.start() } catch (_: Exception) {}; rainOn = true }
    }

    // Saiu da partida: a chuva para na hora. No menu o motor não desenha, então o setRain (chamado depois de cada
    // quadro) não vinha mais e a chuva continuava tocando no lobby
    fun pararChuva() {
        if (rainOn) { try { rain?.pause() } catch (_: Exception) {}; rainOn = false }
        rainVolume = 0f
        try { rain?.setVolume(0f, 0f) } catch (_: Exception) {}
    }

    fun pause() {
        paused = true
        pool.autoPause()
        if (rainOn) { try { rain?.pause() } catch (_: Exception) {}; rainOn = false }
    }
    fun resume() { paused = false; pool.autoResume() }
    fun release() { pool.release(); try { rain?.release() } catch (_: Exception) {} }
}
