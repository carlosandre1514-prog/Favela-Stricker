package com.mobile3d

import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString
import okio.ByteString.Companion.toByteString
import org.json.JSONObject
import java.net.URLEncoder
import java.util.concurrent.TimeUnit

// Partida ao vivo pelo servidor do jogo (Render): um WebSocket por partida.
// Envia o estado dos meus lutadores 20 vezes por segundo e os eventos (dano, abate, granada); recebe os dos outros.
// A voz (chat do time) vai pelo mesmo WebSocket, em pacotes binários.
class PartidaAoVivo(
    server: String, code: String, id: String, team: Int, name: String,
    private val onState: (String) -> Unit,
    private val onEvent: (String) -> Unit,
    private val onVoice: (Int, ByteArray) -> Unit,
    private val onStatus: (aberta: Boolean, erro: String?) -> Unit,
) {
    private val client = OkHttpClient.Builder().pingInterval(15, TimeUnit.SECONDS).readTimeout(0, TimeUnit.MILLISECONDS).build()
    private val url: String
    @Volatile private var ws: WebSocket? = null
    @Volatile var aberta = false
        private set
    @Volatile private var fechada = false
    private var tentativas = 0

    init {
        val base = server.trimEnd('/').replaceFirst("https://", "wss://").replaceFirst("http://", "ws://")
        fun enc(s: String) = URLEncoder.encode(s, "UTF-8")
        url = "$base/partida?sala=${enc(code)}&id=${enc(id)}&time=$team&nome=${enc(name)}"
        conectar()
    }

    private fun conectar() {
        if (fechada) return
        ws = client.newWebSocket(Request.Builder().url(url).build(), object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                aberta = true
                tentativas = 0
                onStatus(true, null)
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                try {
                    val j = JSONObject(text)
                    val d = j.opt("d") ?: return
                    when (j.optString("k")) {
                        "st" -> onState(d.toString())
                        "ev" -> onEvent(d.toString())
                    }
                } catch (_: Exception) {}
            }

            override fun onMessage(webSocket: WebSocket, bytes: ByteString) {
                if (bytes.size < 2) return
                val b = bytes.toByteArray()
                onVoice(b[0].toInt() and 0xFF, b.copyOfRange(1, b.size))
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) = caiu(reason)
            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) = caiu(t.message ?: "falha")
        })
    }

    // Caiu a conexão (sinal ruim, servidor acordando): tenta de novo algumas vezes
    private fun caiu(motivo: String) {
        aberta = false
        if (fechada) return
        onStatus(false, motivo)
        if (++tentativas <= 8) Thread {
            try { Thread.sleep(1000L * tentativas) } catch (_: InterruptedException) {}
            conectar()
        }.start()
    }

    fun enviarEstado(estados: String) { if (aberta && estados.length > 2) ws?.send("{\"k\":\"st\",\"d\":$estados}") }
    fun enviarEvento(evento: String) { if (aberta) ws?.send("{\"k\":\"ev\",\"d\":$evento}") }
    fun enviarVoz(pcmMuLaw: ByteArray) { if (aberta) ws?.send(pcmMuLaw.toByteString()) }

    fun fechar() {
        fechada = true
        aberta = false
        try { ws?.close(1000, "saiu") } catch (_: Exception) {}
        client.dispatcher.executorService.shutdown()
    }
}
