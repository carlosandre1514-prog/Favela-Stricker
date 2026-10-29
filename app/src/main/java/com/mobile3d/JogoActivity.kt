package com.mobile3d

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.graphics.Color
import android.opengl.GLSurfaceView
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.Vibrator
import android.util.Log
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.webkit.ConsoleMessage
import android.webkit.PermissionRequest
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.widget.TextView
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayInputStream
import java.io.File

// O jogo Favela Stricker. O motor 3D desenha por baixo (EngineView) e as telas por cima, num WebView transparente
// com a mesma interface do site (pasta web/: login, menu, lojas, configurações, HUD, multiplayer e chat de voz).
// As telas falam com o motor pela PonteJogo; os arquivos da interface vêm de dentro do APK (https://jogo.local/).
class JogoActivity : Activity() {
    lateinit var motorView: EngineView
        private set
    private lateinit var web: WebView
    private val ponte = PonteJogo(this)
    private val sons by lazy { SoundManager(this) }
    private val prefs by lazy { getSharedPreferences("jogo", MODE_PRIVATE) }
    private val tempo = FloatArray(3)
    private var ultimoHud = 0L
    private var ultimaRede = 0L
    private var pedidoMicrofone: PermissionRequest? = null
    val diag by lazy { Diagnostico(this) }
    private var morteAnterior: String? = null   // rastro de quando o Android fechou o jogo da última vez
    private var baixouQualidade = ""

    // ---------------- ciclo de vida ----------------
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        telaCheia()
        instalarRegistroDeErros()
        // morreu na tela da última vez (falta de memória não deixa erro): guarda o rastro e baixa a qualidade
        morteAnterior = diag.morteAnterior()
        if (morteAnterior != null && nivelGrafico() > 0) {
            val antes = nivelGrafico()
            prefs.edit().putInt("nivel", antes - 1).apply()
            baixouQualidade = "A qualidade gráfica baixou de ${NOMES_NIVEL[antes]} para ${NOMES_NIVEL[antes - 1]}."
        }
        diag.fase = "preparando os arquivos"
        diag.iniciar()
        val abertura = TextView(this).apply {
            text = "Preparando o jogo…"
            setTextColor(0xFFF3F4F6.toInt())
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            gravity = Gravity.CENTER
            setBackgroundColor(0xFF0B0F17.toInt())
        }
        setContentView(abertura)
        Thread {
            val dir = EngineFiles.copy(this) { feito, total ->
                runOnUiThread { abertura.text = "Preparando os arquivos do jogo…\n$feito de $total\n(só depois de instalar ou atualizar)" }
            }
            runOnUiThread { if (!isFinishing && !isDestroyed) montar(dir) }
        }.start()
    }

    @Suppress("DEPRECATION")
    private fun telaCheia() {
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) telaCheia()
    }

    override fun onPause() {
        super.onPause()
        diag.emUso(false)
        if (::motorView.isInitialized) motorView.onPause()
        if (::web.isInitialized) web.onPause()
        sons.pause()
        ponte.atirando = false
    }

    override fun onResume() {
        super.onResume()
        diag.emUso(true)
        if (::motorView.isInitialized) motorView.onResume()
        if (::web.isInitialized) web.onResume()
        sons.resume()
    }

    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        diag.registrar("o Android pediu memória (nível $level) · ${try { diag.memoria() } catch (_: Exception) { "" }}")
    }

    override fun onDestroy() {
        diag.parar()
        if (::web.isInitialized) web.destroy()
        sons.release()
        super.onDestroy()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (!::web.isInitialized) return
        // a tela atual decide (voltar, pausar a partida, fechar a janela); na tela inicial, fecha o app
        web.evaluateJavascript("window.voltar ? String(window.voltar()) : 'false'") { r ->
            if (r == null || r.trim('"') != "true") @Suppress("DEPRECATION") super.onBackPressed()
        }
    }

    // ---------------- motor + telas ----------------
    @SuppressLint("SetJavaScriptEnabled")
    private fun montar(dirMotor: String) {
        // 1ª vez: o nível gráfico vem da memória do celular (ex.: Galaxy A14 com 4 GB começa no Leve)
        if (!prefs.contains("nivel")) {
            val mi = android.app.ActivityManager.MemoryInfo()
            (getSystemService(ACTIVITY_SERVICE) as android.app.ActivityManager).getMemoryInfo(mi)
            val gb = mi.totalMem / 1_000_000_000.0
            prefs.edit().putInt("nivel", if (gb < 4.5) 0 else if (gb < 6.5) 1 else 2).apply()
        }
        try { File(dirMotor, "qualidade.txt").writeText(nivelGrafico().toString()) } catch (_: Exception) {}
        diag.fase = "abrindo o motor (gráficos ${NOMES_NIVEL[nivelGrafico()]})"

        motorView = EngineView(this, dirMotor, if (nivelGrafico() >= 3) 4 else 0)  // Ultra: antisserrilhado 4x
        motorView.touchToEngine = false
        // tela inicial leve: o motor abre vazio e parado; o mapa só carrega quando a partida vai começar
        motorView.onReady = {
            ponte.mapaPronto = false
            ponte.carregandoMapa = false
            ponte.pronto = true
            diag.fase = "motor pronto (menu sem mapa)"
        }
        motorView.afterFrame = { quadro -> depoisDoQuadro(quadro) }
        aplicarResolucao()

        web = WebView(this)
        web.setBackgroundColor(Color.TRANSPARENT)
        web.isVerticalScrollBarEnabled = false
        web.isHorizontalScrollBarEnabled = false
        web.overScrollMode = View.OVER_SCROLL_NEVER
        with(web.settings) {
            javaScriptEnabled = true
            domStorageEnabled = true
            mediaPlaybackRequiresUserGesture = false
            allowFileAccess = false
            allowContentAccess = false
            textZoom = 100
        }
        if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0) WebView.setWebContentsDebuggingEnabled(true)
        web.addJavascriptInterface(ponte, "Nativo")
        web.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(view: WebView, req: WebResourceRequest): WebResourceResponse? =
                if (req.url.host == "jogo.local") arquivoDaInterface(req.url.path ?: "/") else null
            // o processo das telas morreu (o Android tirou memória dele): reabre em vez de fechar o jogo
            override fun onRenderProcessGone(view: WebView, detail: android.webkit.RenderProcessGoneDetail): Boolean {
                diag.registrar("as telas (WebView) caíram · ${if (detail.didCrash()) "erro" else "falta de memória"}")
                runOnUiThread { recreate() }
                return true
            }
        }
        web.webChromeClient = object : WebChromeClient() {
            // chat de voz: o microfone só abre com a permissão do Android
            override fun onPermissionRequest(request: PermissionRequest) {
                runOnUiThread {
                    if (PermissionRequest.RESOURCE_AUDIO_CAPTURE !in request.resources) { request.deny(); return@runOnUiThread }
                    if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
                        request.grant(arrayOf(PermissionRequest.RESOURCE_AUDIO_CAPTURE))
                    } else {
                        pedidoMicrofone = request
                        requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 7)
                    }
                }
            }
            override fun onConsoleMessage(m: ConsoleMessage): Boolean {
                if (m.messageLevel() == ConsoleMessage.MessageLevel.ERROR) Log.e("FavelaStricker", "${m.message()} (${m.sourceId()}:${m.lineNumber()})")
                return true
            }
        }

        val raiz = FrameLayout(this)
        raiz.addView(motorView, FrameLayout.LayoutParams(-1, -1))
        motorView.renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY  // no menu o motor não desenha (só na partida)
        raiz.addView(web, FrameLayout.LayoutParams(-1, -1))
        setContentView(raiz)
        web.loadUrl("https://jogo.local/index.html")
        mostrarUltimoErro()
    }

    // Projeto da partida (o mesmo do editor); as barracas de roupa ficam de fora: roupa se compra no mercado
    fun abrirMapaDaPartida() {
        diag.fase = "abrindo o mapa"
        try {
            val json = JSONObject(assets.open("exemplos/5_policia_x_bandidos.json").bufferedReader().readText())
            val objs = json.getJSONArray("objetos")
            val manter = JSONArray()
            for (i in 0 until objs.length()) {
                val o = objs.getJSONObject(i)
                if (o.optString("tipo") == "loja" && o.optString("loja") == "roupas") continue
                manter.put(o)
            }
            json.put("objetos", manter)
            NativeBridge.nativeSetEditorOverlay(false)
            NativeBridge.importProject(json.toString())
            NativeBridge.nativeSelect(-1)
        } catch (e: Exception) {
            Log.e("FavelaStricker", "mapa da partida", e)
        }
        NativeBridge.nativeSetCamera(0f, 8f, 20f, 3.6f, 0.42f, 115f)
        ponte.mapaPronto = true
        ponte.carregandoMapa = false
        diag.fase = "mapa aberto"
    }

    // Na thread GL, depois de cada quadro: sons, tiro, olhar e as cópias que a página lê
    private fun depoisDoQuadro(quadro: Int) {
        sons.playEvents(NativeBridge.nativePollSounds())
        if (quadro % 10 == 0) { NativeBridge.nativeGetWeather(tempo); sons.setRain(tempo[0]) }
        if (ponte.atirando) NativeBridge.nativePlayerFire()
        val (dx, dy) = ponte.pegarOlhar()
        if (dx != 0f || dy != 0f) NativeBridge.nativeLook(dx, dy)
        if (ponte.girando) NativeBridge.nativeLook(-0.35f, 0f)  // menu: o morro girando devagar ao fundo
        val agora = SystemClock.uptimeMillis()
        if (agora - ultimoHud >= 90) {
            ultimoHud = agora
            ponte.hudJson = String(NativeBridge.nativeHudJson(), Charsets.UTF_8)
            if (ponte.lojaJson.isNotEmpty()) ponte.lojaJson = String(NativeBridge.nativeShopJson(), Charsets.UTF_8)
        }
        if (ponte.online && agora - ultimaRede >= 45) {  // ~20 vezes por segundo
            ultimaRede = agora
            ponte.redeEstado = String(NativeBridge.nativeNetCollect(), Charsets.UTF_8)
            val ev = String(NativeBridge.nativeNetEvents(), Charsets.UTF_8)
            if (ev.length > 2) ponte.redeEventos.add(ev)
        }
    }

    // Arquivos da interface (assets do APK: a pasta web/ do repositório e as imagens/sons do jogo)
    private fun arquivoDaInterface(caminho: String): WebResourceResponse {
        val nome = caminho.trimStart('/').ifEmpty { "index.html" }
        val tipo = when (nome.substringAfterLast('.', "").lowercase()) {
            "html" -> "text/html"; "js" -> "application/javascript"; "css" -> "text/css"; "json" -> "application/json"
            "png" -> "image/png"; "jpg", "jpeg" -> "image/jpeg"; "svg" -> "image/svg+xml"; "woff2" -> "font/woff2"
            "wav" -> "audio/wav"; else -> "application/octet-stream"
        }
        val texto = tipo.startsWith("text/") || tipo.endsWith("javascript") || tipo.endsWith("json")
        return try {
            WebResourceResponse(tipo, if (texto) "utf-8" else null, assets.open(nome)).apply {
                responseHeaders = mapOf("Cache-Control" to "no-cache", "Access-Control-Allow-Origin" to "*")
            }
        } catch (_: Exception) {
            WebResourceResponse("text/plain", "utf-8", 404, "Not Found", emptyMap(), ByteArrayInputStream(ByteArray(0)))
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        @Suppress("DEPRECATION") super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        val pedido = pedidoMicrofone ?: return
        pedidoMicrofone = null
        if (grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) pedido.grant(arrayOf(PermissionRequest.RESOURCE_AUDIO_CAPTURE))
        else pedido.deny()
    }

    // ---------------- usados pela PonteJogo ----------------
    fun versao(): String = try { packageManager.getPackageInfo(packageName, 0).versionName ?: "" } catch (_: Exception) { "" }

    fun configJogo(): String = try { assets.open("config_jogo.json").bufferedReader().readText() } catch (_: Exception) { "{}" }

    // partida: o motor desenha sem parar; menu: parado (não gasta processador, vídeo nem bateria)
    fun desenhar(on: Boolean) = runOnUiThread {
        if (!::motorView.isInitialized) return@runOnUiThread
        motorView.renderMode = if (on) GLSurfaceView.RENDERMODE_CONTINUOUSLY else GLSurfaceView.RENDERMODE_WHEN_DIRTY
    }

    fun nivelGrafico() = prefs.getInt("nivel", 0).coerceIn(0, 3)

    // resolução e quadros por segundo de cada nível (o motor cuida de sombras, visão, texturas e modelos)
    private fun aplicarResolucao() {
        when (val n = nivelGrafico()) {
            0 -> motorView.applyQuality(0.5f, 30, n)
            1 -> motorView.applyQuality(0.65f, 40, n)
            2 -> motorView.applyQuality(0.8f, 50, n)
            else -> motorView.applyQuality(1f, 60, n)
        }
    }

    // texturas e modelos são preparados ao abrir o motor: reabre a tela do jogo (a conta continua)
    fun mudarNivelGrafico(n: Int) {
        val nivel = n.coerceIn(0, 3)
        prefs.edit().putInt("nivel", nivel).apply()
        try { File(filesDir, "shaders/qualidade.txt").writeText(nivel.toString()) } catch (_: Exception) {}
        runOnUiThread { recreate() }
    }

    fun mudarVolume(v: Float) { sons.volume = v.coerceIn(0f, 1f) }

    fun vibrar(ms: Int) {
        val v = getSystemService(VIBRATOR_SERVICE) as? Vibrator ?: return
        v.vibrate(VibrationEffect.createOneShot(ms.toLong().coerceIn(5, 300), VibrationEffect.DEFAULT_AMPLITUDE))
    }

    // ---------------- registro de travamento ----------------
    // Se o jogo fechar sozinho (erro no Kotlin ou no motor), o motivo fica num arquivo e aparece na próxima abertura
    private val arquivoErro by lazy { File(filesDir, "ultimo_erro.txt") }

    private fun instalarRegistroDeErros() {
        val anterior = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            try {
                val sw = java.io.StringWriter()
                e.printStackTrace(java.io.PrintWriter(sw))
                arquivoErro.writeText("Erro no app (${t.name}):\n$sw")
            } catch (_: Exception) {}
            anterior?.uncaughtException(t, e)
        }
        try { NativeBridge.nativeSetCrashFile(arquivoErro.absolutePath) } catch (_: Throwable) {}
    }

    // Aviso na abertura: o jogo deu erro (motor ou app) ou o Android fechou ele na tela (memória)
    private fun mostrarUltimoErro() {
        val erro = if (arquivoErro.exists()) (try { arquivoErro.readText() } catch (_: Exception) { "" }).also { arquivoErro.delete() } else ""
        val morte = morteAnterior
        morteAnterior = null
        if (erro.isBlank() && morte == null) return
        val titulo = if (erro.isNotBlank()) "O jogo fechou da última vez" else "O Android fechou o jogo da última vez"
        val explica = if (erro.isNotBlank()) "Deu um erro." else "Quase sempre é falta de memória no celular."
        val info = "Favela Stricker ${versao()} · Android ${Build.VERSION.RELEASE} · ${Build.MANUFACTURER} ${Build.MODEL} · " +
            "gráficos agora: ${NOMES_NIVEL[nivelGrafico()]}\n" + (if (erro.isNotBlank()) "\n$erro\n" else "") +
            (if (morte != null) "\nO que o jogo fazia (rastro):\n$morte" else "")
        AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle(titulo)
            .setMessage("$explica ${baixouQualidade}\nCopie e mande para o desenvolvedor:\n\n" + info.takeLast(3000))
            .setPositiveButton("Copiar") { _, _ ->
                (getSystemService(CLIPBOARD_SERVICE) as android.content.ClipboardManager)
                    .setPrimaryClip(android.content.ClipData.newPlainText("erro", info))
            }
            .setNegativeButton("Fechar", null)
            .show()
    }

    companion object {
        val NOMES_NIVEL = arrayOf("Leve", "Padrão", "Intermediária", "Ultra")
    }
}
