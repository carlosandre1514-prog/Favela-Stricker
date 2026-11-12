package com.mobile3d

import android.app.Activity
import android.opengl.EGL14
import android.opengl.GLSurfaceView
import android.widget.Toast
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

// amostras > 0: antisserrilhado MSAA (qualidade Ultra); se o celular não tiver, cai para a tela normal
class EngineView(activity: Activity, private val shaderDir: String, private val amostras: Int = 0) : GLSurfaceView(activity) {
    var afterFrame: ((Int) -> Unit)? = null
    var onReady: (() -> Unit)? = null  // chamado na thread GL quando o motor é (re)criado
    // Qualidade: resolução do desenho (fração da tela) e limite de quadros (30 = esquenta menos e não engasga)
    @Volatile var renderScale = 1f
    @Volatile var maxFps = 60
    @Volatile var quality = 2
    // Medidor (FPS, tempo do quadro e o pior quadro do último segundo: os engasgos aparecem nele)
    @Volatile var medidorFps = 0
    @Volatile var medidorMs = 0f
    @Volatile var medidorPior = 0f
    private val telaHz = (activity.windowManager.defaultDisplay?.refreshRate ?: 60f).coerceIn(30f, 240f)
    @Volatile private var intervaloTroca = -1  // quadros da tela por quadro do jogo (aplicado na thread GL)

    fun applyQuality(scale: Float, fps: Int, q: Int) {
        renderScale = scale; maxFps = fps; quality = q
        intervaloTroca = -1  // recalcula o ritmo da tela
        if (width > 0 && height > 0) holder.setFixedSize((width * scale).toInt().coerceAtLeast(1), (height * scale).toInt().coerceAtLeast(1))
        queueEvent { NativeBridge.nativeSetQuality(q); NativeBridge.nativeSetFpsAlvo(fpsAlvo()) }
    }

    // resolução dinâmica do motor: abaixo do Avançado (o Avançado usa antisserrilhado e tela inteira), com o limite
    // de quadros do nível como alvo; se o celular não aguenta, o motor desenha menor e estica (sem piscar)
    private fun fpsAlvo() = if (quality >= 3 || amostras > 0) 0 else maxFps

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (renderScale < 1f) post { holder.setFixedSize((w * renderScale).toInt().coerceAtLeast(1), (h * renderScale).toInt().coerceAtLeast(1)) }
    }

    init {
        setEGLContextClientVersion(3)
        setEGLConfigChooser(EscolheConfig(amostras))
        preserveEGLContextOnPause = true  // evita recriar tudo ao voltar do segundo plano
        setRenderer(object : Renderer {
            private var last = 0L
            private var frame = 0
            private var usaTroca = false     // limite de quadros pela própria tela (vsync): quadros sempre iguais
            private var contaQuadros = 0
            private var inicioSegundo = 0L
            private var somaMs = 0f
            private var piorMs = 0f

            // Limite de quadros sem engasgo: se o limite divide a taxa da tela (30 numa tela de 60 Hz), a tela mostra
            // um quadro do jogo a cada 2 atualizações (eglSwapInterval 2): os quadros saem sempre com o mesmo
            // intervalo. O "dorme até dar o tempo" de antes errava alguns ms e alternava quadros curtos e longos, o
            // que aparecia como travadinhas ao correr.
            private fun ajustarRitmo() {
                if (intervaloTroca >= 0) return
                val passo = Math.round(telaHz / maxFps.coerceAtLeast(1))
                usaTroca = maxFps < 60 && passo >= 2 && Math.abs(telaHz / passo - maxFps) <= 3f
                intervaloTroca = if (usaTroca) passo else 1
                try { EGL14.eglSwapInterval(EGL14.eglGetCurrentDisplay(), intervaloTroca) } catch (_: Exception) { usaTroca = false }
            }

            // Criar o motor demora (no Galaxy A14, mais de 10 s). Enquanto isso a tela principal do Android fica
            // esperando o 1º desenho (o GLSurfaceView segura a thread da tela no resize até um quadro terminar) e
            // o Android mostra "o app não está respondendo" e fecha o jogo. Por isso: o 1º quadro só limpa a tela
            // e volta na hora (libera a tela principal); o motor é criado num quadro seguinte, com o tamanho da tela já estável.
            private var criar = 0          // 0 = motor pronto; 1 = falta criar (próximo quadro); 2 = 1º quadro (vazio)
            private var w0 = 0; private var h0 = 0
            private var mudou = 0L          // quando a tela mudou de tamanho pela última vez

            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                // Contexto novo (1ª vez ou contexto perdido): o motor e a cena serão recriados
                criar = 2
                intervaloTroca = -1
                last = System.nanoTime()
            }
            override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) {
                w0 = w; h0 = h
                mudou = System.nanoTime()
                if (criar == 0) NativeBridge.nativeResize(w, h)
            }
            private fun criarMotor() {
                if (NativeBridge.nativeCreate(shaderDir)) {
                    if (w0 > 0 && h0 > 0) NativeBridge.nativeResize(w0, h0)
                    NativeBridge.nativeSetQuality(quality)
                    NativeBridge.nativeSetFpsAlvo(fpsAlvo())
                    onReady?.invoke()
                } else {
                    post { Toast.makeText(context, "Falha ao iniciar o motor (shaders). Veja o logcat: Mobile3D", Toast.LENGTH_LONG).show() }
                }
                last = System.nanoTime()
            }
            override fun onDrawFrame(gl: GL10?) {
                // 1º quadro, e enquanto a tela ainda está mudando de tamanho (o app ajusta a resolução logo ao abrir,
                // e cada ajuste também espera um quadro): só limpa e volta rápido
                if (criar == 2 || (criar == 1 && System.nanoTime() - mudou < 400_000_000L)) {
                    android.opengl.GLES20.glClearColor(0.043f, 0.059f, 0.09f, 1f)
                    android.opengl.GLES20.glClear(android.opengl.GLES20.GL_COLOR_BUFFER_BIT)
                    criar = 1
                    Thread.sleep(16)  // (sem girar à toa enquanto espera)
                    requestRender()
                    return
                }
                if (criar == 1) { criar = 0; criarMotor(); requestRender(); return }
                ajustarRitmo()
                if (maxFps < 60 && !usaTroca) {  // limite de quadros: espera o resto do intervalo
                    val wait = 1_000_000_000L / maxFps - (System.nanoTime() - last)
                    if (wait > 1_000_000L) Thread.sleep(wait / 1_000_000L)
                }
                val now = System.nanoTime()
                val dt = ((now - last) / 1e9f).coerceIn(0f, 0.1f)
                last = now
                val ms = dt * 1000f
                contaQuadros++; somaMs += ms; if (ms > piorMs) piorMs = ms
                if (now - inicioSegundo >= 1_000_000_000L) {
                    medidorFps = contaQuadros; medidorMs = if (contaQuadros > 0) somaMs / contaQuadros else 0f; medidorPior = piorMs
                    contaQuadros = 0; somaMs = 0f; piorMs = 0f; inicioSegundo = now
                }
                NativeBridge.nativeFrame(dt)
                afterFrame?.invoke(frame++)   // sons, tiro, olhar e o que a página lê (thread GL)
            }
        })
        renderMode = RENDERMODE_CONTINUOUSLY
    }
}

// Configuração da tela GL: RGBA8 + profundidade 24; com amostras > 0 tenta MSAA (bordas sem serrilhado)
private class EscolheConfig(private val amostras: Int) : GLSurfaceView.EGLConfigChooser {
    override fun chooseConfig(egl: javax.microedition.khronos.egl.EGL10, display: javax.microedition.khronos.egl.EGLDisplay): javax.microedition.khronos.egl.EGLConfig {
        val es3 = 4  // EGL_OPENGL_ES2_BIT: o mesmo que o GLSurfaceView usa (o contexto ES 3 sai dele)
        fun tenta(msaa: Int): javax.microedition.khronos.egl.EGLConfig? {
            val base = intArrayOf(
                javax.microedition.khronos.egl.EGL10.EGL_RED_SIZE, 8, javax.microedition.khronos.egl.EGL10.EGL_GREEN_SIZE, 8,
                javax.microedition.khronos.egl.EGL10.EGL_BLUE_SIZE, 8, javax.microedition.khronos.egl.EGL10.EGL_ALPHA_SIZE, 8,
                javax.microedition.khronos.egl.EGL10.EGL_DEPTH_SIZE, 24, javax.microedition.khronos.egl.EGL10.EGL_RENDERABLE_TYPE, es3)
            val extra = if (msaa > 0) intArrayOf(javax.microedition.khronos.egl.EGL10.EGL_SAMPLE_BUFFERS, 1, javax.microedition.khronos.egl.EGL10.EGL_SAMPLES, msaa) else intArrayOf()
            val attrs = base + extra + intArrayOf(javax.microedition.khronos.egl.EGL10.EGL_NONE)
            val n = IntArray(1)
            if (!egl.eglChooseConfig(display, attrs, null, 0, n) || n[0] <= 0) return null
            val configs = arrayOfNulls<javax.microedition.khronos.egl.EGLConfig>(n[0])
            egl.eglChooseConfig(display, attrs, configs, n[0], n)
            return configs.firstOrNull()
        }
        return (if (amostras > 0) tenta(amostras) ?: tenta(2) else null) ?: tenta(0)
            ?: throw IllegalStateException("sem configuração OpenGL ES 3 neste aparelho")
    }
}
