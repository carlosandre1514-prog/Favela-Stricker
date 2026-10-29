package com.mobile3d

import android.webkit.JavascriptInterface
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.FutureTask
import java.util.concurrent.TimeUnit

// Ponte entre as telas (HTML em web/, o mesmo código do site) e o motor nativo.
// O JavaScript chama estes métodos numa thread própria do WebView; o motor só pode ser usado na thread GL,
// então os comandos vão por fila (queueEvent) e as leituras (HUD, loja, rede) vêm de cópias que a thread GL
// atualiza a cada quadro (JogoActivity.depoisDoQuadro).
class PonteJogo(private val jogo: JogoActivity) {
    // estado lido pela thread GL a cada quadro
    @Volatile var atirando = false
    @Volatile var girando = false
    @Volatile var online = false
    private val olharLock = Any()
    private var olharX = 0f
    private var olharY = 0f

    // cópias atualizadas pela thread GL
    @Volatile var hudJson = "{}"
    @Volatile var lojaJson = ""
    @Volatile var redeEstado = ""
    val redeEventos = ConcurrentLinkedQueue<String>()
    @Volatile var pronto = false          // motor criado
    @Volatile var mapaPronto = false      // mapa da partida carregado
    @Volatile var carregandoMapa = false

    fun pegarOlhar(): Pair<Float, Float> = synchronized(olharLock) { val p = olharX to olharY; olharX = 0f; olharY = 0f; p }

    private fun gl(bloco: () -> Unit) = jogo.motorView.queueEvent(bloco)
    // roda na thread GL e espera a resposta (no máximo 1 s)
    private fun <T> glEspera(padrao: T, bloco: () -> T): T {
        val tarefa = FutureTask(bloco)
        jogo.motorView.queueEvent(tarefa)
        return try { tarefa.get(1, TimeUnit.SECONDS) } catch (_: Exception) { padrao }
    }
    private fun texto(b: ByteArray) = String(b, Charsets.UTF_8)

    @JavascriptInterface fun config(): String = jogo.configJogo()
    @JavascriptInterface fun versao(): String = jogo.versao()
    @JavascriptInterface fun fase(texto: String) { jogo.diag.fase = texto }
    @JavascriptInterface fun pronto(): Boolean = pronto
    // o mapa só carrega quando a partida vai começar (a tela inicial fica leve); a página espera mapaPronto()
    @JavascriptInterface fun prepararPartida() {
        if (mapaPronto || carregandoMapa) return
        carregandoMapa = true
        jogo.desenhar(true)
        gl { jogo.abrirMapaDaPartida() }
    }
    @JavascriptInterface fun mapaPronto(): Boolean = mapaPronto
    @JavascriptInterface fun desenhar(on: Boolean) = jogo.desenhar(on)
    @JavascriptInterface fun girarMenu(on: Boolean) { girando = on }
    @JavascriptInterface fun visaoGeral() = gl { NativeBridge.nativeSetCamera(0f, 8f, 20f, 3.6f, 0.42f, 115f) }
    @JavascriptInterface fun play(on: Boolean) { if (!on) atirando = false; gl { NativeBridge.nativeSetPlay(on) } }
    @JavascriptInterface fun primeiraPessoa(on: Boolean) = gl { NativeBridge.nativeSetFirstPerson(on) }
    @JavascriptInterface fun escolhas(time: Int, roupa: Int, principal: String, segunda: String) =
        gl { NativeBridge.nativeTeamLoadout(time, roupa, principal, segunda) }
    @JavascriptInterface fun skinArma(arma: String, skin: String) = gl { NativeBridge.nativeTeamGunSkin(arma, skin) }
    @JavascriptInterface fun camera(tx: Float, ty: Float, tz: Float, yaw: Float, pitch: Float, dist: Float) =
        gl { NativeBridge.nativeSetCamera(tx, ty, tz, yaw, pitch, dist) }

    // controles
    @JavascriptInterface fun olhar(dx: Float, dy: Float) { synchronized(olharLock) { olharX += dx; olharY += dy } }
    @JavascriptInterface fun andar(x: Float, y: Float, correr: Boolean) = gl { NativeBridge.nativePlayerInput(x, y, correr) }
    @JavascriptInterface fun pular() = gl { NativeBridge.nativePlayerJump() }
    @JavascriptInterface fun agachar(on: Boolean) = gl { NativeBridge.nativePlayerCrouch(on) }
    @JavascriptInterface fun atirar(on: Boolean) { atirando = on }
    @JavascriptInterface fun mirar(on: Boolean) = gl { NativeBridge.nativePlayerAim(on) }
    @JavascriptInterface fun trocar(): Boolean = glEspera(false) { NativeBridge.nativePlayerSwap() }
    @JavascriptInterface fun granada(): Boolean = glEspera(false) { NativeBridge.nativePlayerGrenade() }
    @JavascriptInterface fun recarregar() = gl { NativeBridge.nativePlayerReload() }
    @JavascriptInterface fun interagir(): Boolean = glEspera(false) {
        val ok = NativeBridge.nativeInteract()
        lojaJson = texto(NativeBridge.nativeShopJson())
        ok
    }

    // HUD e loja da partida
    @JavascriptInterface fun hud(): String = hudJson
    @JavascriptInterface fun loja(): String = lojaJson
    @JavascriptInterface fun lojaComprar(item: Int): String = glEspera("") {
        val msg = texto(NativeBridge.nativeShopBuy(item))
        lojaJson = texto(NativeBridge.nativeShopJson())
        hudJson = texto(NativeBridge.nativeHudJson())
        msg
    }
    @JavascriptInterface fun lojaFechar() { lojaJson = ""; gl { NativeBridge.nativeShopClose() } }

    // partida online: a página leva e traz os dados pelo servidor/Firebase
    @JavascriptInterface fun redeSala(host: Boolean, id: String, roster: String) {
        online = roster.isNotEmpty()
        redeEventos.clear()
        redeEstado = ""
        gl { NativeBridge.nativeNetRoster(host, id.toByteArray(), roster.toByteArray()) }
    }
    @JavascriptInterface fun redeColetar(): String { val s = redeEstado; redeEstado = ""; return s }
    @JavascriptInterface fun redeEventos(): String {
        // junta os pacotes de eventos ("[...]") que a thread GL juntou desde a última leitura
        val partes = ArrayList<String>()
        while (true) {
            val s = redeEventos.poll() ?: break
            val miolo = s.trim().removePrefix("[").removeSuffix("]").trim()
            if (miolo.isNotEmpty()) partes.add(miolo)
        }
        return "[" + partes.joinToString(",") + "]"
    }
    @JavascriptInterface fun redeAplicar(estados: String) = gl { NativeBridge.nativeNetApply(estados.toByteArray()) }
    @JavascriptInterface fun redeEvento(ev: String) = gl { NativeBridge.nativeNetEvent(ev.toByteArray()) }

    // aparelho
    @JavascriptInterface fun nivel(): Int = jogo.nivelGrafico()
    @JavascriptInterface fun mudarNivel(n: Int) = jogo.mudarNivelGrafico(n)
    @JavascriptInterface fun volume(v: Float) = jogo.mudarVolume(v)
    @JavascriptInterface fun vibrar(ms: Int) = jogo.vibrar(ms)
    @JavascriptInterface fun sair() = jogo.runOnUiThread { jogo.finish() }
}
