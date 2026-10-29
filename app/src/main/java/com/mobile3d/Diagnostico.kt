package com.mobile3d

import android.app.ActivityManager
import android.content.Context
import android.os.Debug
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

// Rastro do que o jogo estava fazendo, gravado a cada 2 s: etapa, memória livre do celular e memória do jogo.
// Se o Android matar o jogo (falta de memória não gera erro nenhum), na próxima abertura dá para saber
// em que etapa ele estava e quanta memória sobrava. "Em uso" = o jogo estava na tela quando morreu.
class Diagnostico(private val ctx: Context) {
    private val pasta = ctx.filesDir
    private val arquivoRastro = File(pasta, "rastro.txt")
    private val marcaEmUso = File(pasta, "em_uso")
    private val linhas = ArrayDeque<String>()
    @Volatile var fase = "abrindo"
        set(v) { field = v; registrar("etapa: $v") }
    @Volatile private var rodando = false
    private val hora = SimpleDateFormat("HH:mm:ss", Locale.US)

    // O jogo morreu na tela da última vez? Devolve o rastro (e limpa), ou null
    fun morteAnterior(): String? {
        if (!marcaEmUso.exists()) return null
        marcaEmUso.delete()
        val r = try { arquivoRastro.readText() } catch (_: Exception) { "" }
        return r.ifBlank { "(sem rastro)" }
    }

    fun emUso(sim: Boolean) {
        try { if (sim) marcaEmUso.writeText("1") else marcaEmUso.delete() } catch (_: Exception) {}
    }

    fun registrar(texto: String) {
        synchronized(linhas) {
            linhas.addLast("${hora.format(Date())} $texto")
            while (linhas.size > 40) linhas.removeFirst()
            try { arquivoRastro.writeText(linhas.joinToString("\n")) } catch (_: Exception) {}
        }
    }

    fun memoria(): String {
        val am = ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val mi = ActivityManager.MemoryInfo()
        am.getMemoryInfo(mi)
        val dm = Debug.MemoryInfo()
        Debug.getMemoryInfo(dm)
        val graficos = dm.getMemoryStat("summary.graphics")?.toIntOrNull()?.div(1024) ?: -1
        val total = dm.totalPss / 1024
        return "celular livre ${mi.availMem / 1_048_576} de ${mi.totalMem / 1_048_576} MB${if (mi.lowMemory) " (POUCA)" else ""} · " +
            "jogo $total MB (gráficos $graficos MB)"
    }

    fun iniciar() {
        if (rodando) return
        rodando = true
        Thread {
            while (rodando) {
                try { registrar("[$fase] ${memoria()}") } catch (_: Exception) {}
                try { Thread.sleep(2000) } catch (_: InterruptedException) {}
            }
        }.apply { isDaemon = true; name = "diagnostico" }.start()
    }

    fun parar() { rodando = false }
}
