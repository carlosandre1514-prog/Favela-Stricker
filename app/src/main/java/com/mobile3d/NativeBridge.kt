package com.mobile3d

// Funções nativas do Favela Stricker (libmobile3d.so: o motor + o jogo). Só o que o jogo usa, sem editor.
// Chamar SEMPRE na thread GL (GLSurfaceView.queueEvent / onDrawFrame).
object NativeBridge {
    init { System.loadLibrary("mobile3d") }

    // motor
    @JvmStatic external fun nativeSetCrashFile(path: String)  // registro de travamento do motor (ver JogoActivity)
    @JvmStatic external fun nativeCreate(shaderDir: String): Boolean  // cria o motor e liga o jogo nele
    @JvmStatic external fun nativeDestroy()
    @JvmStatic external fun nativeResize(w: Int, h: Int)
    @JvmStatic external fun nativeFrame(dt: Float)
    @JvmStatic external fun nativeSetQuality(q: Int)
    @JvmStatic external fun nativePollSounds(): String
    @JvmStatic external fun nativeGetWeather(out3: FloatArray)
    @JvmStatic external fun nativeImportProject(json: ByteArray): Boolean  // abre o mapa da partida
    @JvmStatic external fun nativeSetPlay(playing: Boolean)
    @JvmStatic external fun nativeSetCamera(tx: Float, ty: Float, tz: Float, yaw: Float, pitch: Float, dist: Float)
    @JvmStatic external fun nativeSetFirstPerson(on: Boolean)
    @JvmStatic external fun nativeLook(dx: Float, dy: Float)

    // controles do jogador
    @JvmStatic external fun nativePlayerInput(x: Float, y: Float, run: Boolean)
    @JvmStatic external fun nativePlayerJump()
    @JvmStatic external fun nativePlayerCrouch(on: Boolean)
    @JvmStatic external fun nativePlayerAim(on: Boolean)
    @JvmStatic external fun nativeScopeZoom(d: Int): Int
    @JvmStatic external fun nativePlayerFire()
    @JvmStatic external fun nativePlayerReload()
    @JvmStatic external fun nativePlayerSwap(): Boolean
    @JvmStatic external fun nativePlayerGrenade(): Boolean
    @JvmStatic external fun nativeInteract(): Boolean

    // a partida (jogo): escolhas, HUD, loja e online
    @JvmStatic external fun nativeTeamLoadout(team: Int, skin: Int, primary: String, secondary: String)
    @JvmStatic external fun nativeTeamGunSkin(gun: String, skin: String)
    @JvmStatic external fun nativeGameMode(modo: Int)
    @JvmStatic external fun nativeWarmup(segundos: Float)
    @JvmStatic external fun nativeEquip(gun: String): Boolean
    @JvmStatic external fun nativeHudJson(): ByteArray
    @JvmStatic external fun nativeShopJson(): ByteArray
    @JvmStatic external fun nativeShopBuy(item: Int): ByteArray
    @JvmStatic external fun nativeShopClose()
    @JvmStatic external fun nativeNetRoster(host: Boolean, id: ByteArray, roster: ByteArray)
    @JvmStatic external fun nativeNetCollect(): ByteArray
    @JvmStatic external fun nativeNetApply(states: ByteArray)
    @JvmStatic external fun nativeNetEvents(): ByteArray
    @JvmStatic external fun nativeNetEvent(ev: ByteArray)

    fun importProject(json: String) = nativeImportProject(json.toByteArray(Charsets.UTF_8))
}
