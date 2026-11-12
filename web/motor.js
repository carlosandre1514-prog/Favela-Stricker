// Favela Stricker — ponte com o motor 3D.
// No site o motor é WebAssembly (favela.js, funções web_* de jogo/plataforma/web_main.cpp).
// No APK a página roda num WebView por cima do motor nativo e fala com ele pelo objeto Nativo
// (app/src/main/java/com/mobile3d/PonteJogo.kt). As telas usam só o objeto `motor` abaixo.
'use strict';

const ANDROID = typeof window.Nativo === 'object' && window.Nativo !== null;
const TOQUE = ANDROID || matchMedia('(pointer: coarse)').matches || 'ontouchstart' in window;
// plataforma de quem joga (aparece no chat e no perfil; o multiplayer é o mesmo para as três)
const PLATAFORMA = ANDROID ? 'android' : /iPhone|iPad|iPod/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1) ? 'ios' : 'pc';
if (ANDROID) { document.documentElement.classList.add('android'); document.body.classList.add('android'); }
if (TOQUE) document.body.classList.add('toque');

const CONFIG = (() => {
  if (ANDROID) { try { return JSON.parse(Nativo.config()); } catch (_) { return {}; } }
  return window.CONFIG_JOGO || {};
})();
// servidor da partida ao vivo: no site é o próprio endereço (Render); no APK vem da configuração
const SERVIDOR = (CONFIG.servidor || location.origin).replace(/\/$/, '');

// ---------------- motor no navegador (WebAssembly) ----------------
let M = null;  // módulo do Emscripten
const wasm = (nome, ret, tipos, args) => (M ? M.ccall(nome, ret, tipos, args) : (ret === 'string' ? '' : 0));
const lerJson = (s, padrao) => { try { return s ? JSON.parse(s) : padrao; } catch (_) { return padrao; } };

// estado de entrada aplicado a cada quadro no site (no APK o Kotlin faz isso na thread do motor)
const entrada = { dx: 0, dy: 0, atirando: false, girando: false };

// Tela inicial leve: no menu aparece uma imagem fixa da favela e o motor fica parado (não desenha);
// o mapa só é carregado quando a primeira partida vai começar.
const modoMenu = (on) => document.body.classList.toggle('menu-fundo', !!on);
let projetoPartida = null, mapaCarregado = false;

// Medidor no navegador: conta os quadros desenhados (requestAnimationFrame) no último segundo
const medWeb = { n: 0, soma: 0, pior: 0, ini: 0, ult: 0, saida: null, ligado: false };
function medidorWeb() {
  if (!medWeb.ligado) {
    medWeb.ligado = true;
    const passo = (t) => {
      if (medWeb.ult) { const ms = t - medWeb.ult; medWeb.n++; medWeb.soma += ms; medWeb.pior = Math.max(medWeb.pior, ms); }
      medWeb.ult = t;
      if (!medWeb.ini) medWeb.ini = t;
      if (t - medWeb.ini >= 1000) {
        medWeb.saida = { fps: medWeb.n, ms: medWeb.n ? medWeb.soma / medWeb.n : 0, pior: medWeb.pior };
        medWeb.n = 0; medWeb.soma = 0; medWeb.pior = 0; medWeb.ini = t;
      }
      requestAnimationFrame(passo);
    };
    requestAnimationFrame(passo);
  }
  return medWeb.saida;
}

const motor = ANDROID ? {
  pronto: () => Nativo.pronto(),
  girarMenu: (on) => { modoMenu(on); Nativo.girarMenu(false); Nativo.desenhar(!on); },
  prepararPartida: () => Nativo.prepararPartida(),
  prepararMapa: () => { try { Nativo.prepararMapa(); } catch (_) {} },  // por trás do menu (thread do motor)
  mapaPronto: () => Nativo.mapaPronto(),
  visaoGeral: () => Nativo.visaoGeral(),
  play: (on) => Nativo.play(!!on),
  primeiraPessoa: (on) => Nativo.primeiraPessoa(!!on),
  escolhas: (t, s, a, b) => Nativo.escolhas(t, s, a, b || ''),
  modo: (m) => { try { Nativo.modo(m); } catch (_) {} },
  skinArma: (a, s) => Nativo.skinArma(a, s || ''),
  aquecimento: (s) => Nativo.aquecimento(s),
  equipar: (a) => Nativo.equipar(a),
  acessorios: (a, b) => Nativo.acessorios(a, b | 0),
  camera: (tx, ty, tz, yaw, pitch, dist) => Nativo.camera(tx, ty, tz, yaw, pitch, dist),
  olhar: (dx, dy) => Nativo.olhar(dx, dy),
  andar: (x, y, run) => Nativo.andar(x, y, !!run),
  pular: () => Nativo.pular(),
  agachar: (on) => Nativo.agachar(!!on),
  atirar: (on) => Nativo.atirar(!!on),
  mirar: (on) => Nativo.mirar(!!on),
  zoom: (d) => Nativo.zoom(d | 0),
  trocar: () => Nativo.trocar(),
  granada: () => Nativo.granada(),
  recarregar: () => Nativo.recarregar(),
  interagir: () => Nativo.interagir(),
  hud: () => lerJson(Nativo.hud(), {}),
  desempenho: () => { try { const [f, m, p] = Nativo.desempenho().split('|').map(Number); return { fps: f, ms: m, pior: p }; } catch (_) { return null; } },
  loja: () => lerJson(Nativo.loja(), null),
  lojaComprar: (id) => Nativo.lojaComprar(id),
  lojaFechar: () => Nativo.lojaFechar(),
  redeSala: (host, id, roster) => Nativo.redeSala(!!host, id || '', roster || ''),
  redeColetar: () => Nativo.redeColetar(),
  redeAplicar: (s) => Nativo.redeAplicar(s),
  redeEventos: () => Nativo.redeEventos(),
  redeEvento: (s) => Nativo.redeEvento(s),
  nivel: () => Nativo.nivel(),
  mudarNivel: (n) => Nativo.mudarNivel(n),
  volume: (v) => Nativo.volume(v),
  vibrar: (ms) => Nativo.vibrar(ms),
  sair: () => Nativo.sair(),
} : {
  pronto: () => !!M,
  girarMenu: (on) => { modoMenu(on); entrada.girando = false; },
  prepararPartida: () => {
    if (mapaCarregado || !M || !projetoPartida) return;
    M.ccall('web_abrir_projeto', 'number', ['string'], [JSON.stringify(projetoPartida)]);
    mapaCarregado = true;
  },
  prepararMapa: () => {},  // no site o carregamento trava a página: fica para quando a partida começa
  mapaPronto: () => mapaCarregado,
  visaoGeral: () => wasm('web_camera', null, ['number', 'number', 'number', 'number', 'number', 'number'], [0, 8, 20, 3.6, 0.42, 115]),
  play: (on) => wasm('web_play', null, ['number'], [on ? 1 : 0]),
  primeiraPessoa: (on) => wasm('web_primeira_pessoa', null, ['number'], [on ? 1 : 0]),
  escolhas: (t, s, a, b) => wasm('web_escolhas', null, ['number', 'number', 'string', 'string'], [t, s, a, b || '']),
  modo: (m) => wasm('web_modo', null, ['number'], [m]),
  skinArma: (a, s) => wasm('web_skin_arma', null, ['string', 'string'], [a, s || '']),
  aquecimento: (s) => wasm('web_aquecimento', null, ['number'], [s]),
  equipar: (a) => wasm('web_equipar', 'number', ['string'], [a]),
  acessorios: (a, b) => wasm('web_acessorios', null, ['string', 'number'], [a, b | 0]),
  camera: (tx, ty, tz, yaw, pitch, dist) => wasm('web_camera', null, ['number', 'number', 'number', 'number', 'number', 'number'], [tx, ty, tz, yaw, pitch, dist]),
  olhar: (dx, dy) => { entrada.dx += dx; entrada.dy += dy; },
  andar: (x, y, run) => wasm('web_andar', null, ['number', 'number', 'number'], [x, y, run ? 1 : 0]),
  pular: () => wasm('web_pular', null, [], []),
  agachar: (on) => wasm('web_agachar', null, ['number'], [on ? 1 : 0]),
  atirar: (on) => { entrada.atirando = !!on; },
  mirar: (on) => wasm('web_mirar', null, ['number'], [on ? 1 : 0]),
  zoom: (d) => wasm('web_zoom', 'number', ['number'], [d | 0]),
  trocar: () => wasm('web_trocar_arma', 'number', [], []),
  granada: () => wasm('web_granada', 'number', [], []),
  recarregar: () => wasm('web_recarregar', null, [], []),
  interagir: () => wasm('web_interagir', 'number', [], []),
  hud: () => lerJson(wasm('web_hud', 'string', [], []), {}),
  desempenho: () => medidorWeb(),
  loja: () => lerJson(wasm('web_loja', 'string', [], []), null),
  lojaComprar: (id) => wasm('web_loja_comprar', 'string', ['number'], [id]),
  lojaFechar: () => wasm('web_loja_fechar', null, [], []),
  redeSala: (host, id, roster) => wasm('web_rede_sala', null, ['number', 'string', 'string'], [host ? 1 : 0, id || '', roster || '']),
  redeColetar: () => wasm('web_rede_coletar', 'string', [], []),
  redeAplicar: (s) => wasm('web_rede_aplicar', null, ['string'], [s]),
  redeEventos: () => wasm('web_rede_eventos', 'string', [], []),
  redeEvento: (s) => wasm('web_rede_evento', null, ['string'], [s]),
  nivel: () => guardado.get('nivel', TOQUE ? 0 : 2),
  mudarNivel: (n) => { guardado.set('nivel', n); location.reload(); },  // texturas e modelos são preparados ao abrir
  volume: (v) => { volumeSons = v; },
  vibrar: (ms) => { try { navigator.vibrate && navigator.vibrate(ms); } catch (_) {} },
  sair: () => {},
};

// ---------------- sons no site (WebAudio). No APK quem toca é o SoundPool nativo ----------------
let audio = null;
let volumeSons = 1;
const buffers = {};
function ctxAudio() {
  if (!audio) { try { audio = new (window.AudioContext || window.webkitAudioContext)(); } catch (_) {} }
  if (audio && audio.state === 'suspended') audio.resume();
  return audio;
}
function tocarSom(nome, vol, pan) {
  const a = audio;
  if (!a || volumeSons <= 0) return;
  const usar = (b) => {
    const src = a.createBufferSource();
    src.buffer = b;
    if (nome.startsWith('tiro')) src.playbackRate.value = 0.94 + Math.random() * 0.12;
    const g = a.createGain();
    g.gain.value = Math.min(1, vol * volumeSons);
    const p = a.createStereoPanner ? a.createStereoPanner() : null;
    if (p) { p.pan.value = Math.max(-1, Math.min(1, pan)); src.connect(g).connect(p).connect(a.destination); } else src.connect(g).connect(a.destination);
    src.start();
  };
  const base = nome.replace(/_longe$/, '');
  const b = buffers[nome];
  if (b === undefined) {
    buffers[nome] = null;
    fetch('sons/' + nome + '.wav').then((r) => (r.ok ? r.arrayBuffer() : Promise.reject())).then((d) => a.decodeAudioData(d))
      .then((dec) => { buffers[nome] = dec; usar(dec); })
      .catch(() => { if (nome !== base && buffers[base]) usar(buffers[base]); });
  } else if (b) usar(b);
  else if (nome !== base && buffers[base]) usar(buffers[base]);
}

// chuva em loop (sons/chuva.ogg, tools/gerar_chuva.py): volume acompanha quanto está chovendo, num canal só dela
let chuva = null, chuvaQuadro = 0;
function atualizarChuva() {
  const a = audio;
  if (!a) return;
  if (!chuva) {
    chuva = { ganho: a.createGain(), fonte: null, carregando: true };
    chuva.ganho.gain.value = 0;
    chuva.ganho.connect(a.destination);
    // iPhone/iPad (Safari) antigos não tocam ogg: a mesma chuva em m4a
    const ogg = document.createElement('audio').canPlayType('audio/ogg; codecs="vorbis"');
    fetch(ogg ? 'sons/chuva.ogg' : 'sons/chuva.m4a').then((r) => (r.ok ? r.arrayBuffer() : Promise.reject())).then((d) => a.decodeAudioData(d))
      .then((buf) => {
        const src = a.createBufferSource();
        src.buffer = buf; src.loop = true;
        src.connect(chuva.ganho); src.start();
        chuva.fonte = src;
      }).catch(() => {}).finally(() => { chuva.carregando = false; });
  }
  const nivel = Math.max(0, Math.min(1, wasm('web_chuva', 'number', [], []) || 0));
  chuva.ganho.gain.setTargetAtTime(nivel * 0.85 * volumeSons, a.currentTime, 0.4);
}

// laço do site: chamado pelo motor depois de desenhar cada quadro
const aposQuadroExtra = [];
function aposQuadroSite() {
  if (audio && ++chuvaQuadro % 15 === 0) atualizarChuva();
  if (entrada.dx || entrada.dy) { wasm('web_olhar', null, ['number', 'number'], [entrada.dx, entrada.dy]); entrada.dx = entrada.dy = 0; }
  if (entrada.girando) wasm('web_olhar', null, ['number', 'number'], [-0.35, 0]);
  if (entrada.atirando) wasm('web_atirar', null, [], []);
  const s = wasm('web_sons', 'string', [], []);
  if (s && audio) for (const l of s.split('\n')) { const p = l.split('\t'); if (p.length >= 3) tocarSom(p[0], parseFloat(p[1]), parseFloat(p[2])); }
  for (const f of aposQuadroExtra) f();
}

// resolução do desenho no site (o APK cuida da dele)
function ajustarCanvas() {
  if (ANDROID) return;
  const cv = document.getElementById('tela');
  const n = motor.nivel();
  // Ultra: a resolução inteira da tela (até 2x em telas de alta densidade)
  const escala = [0.5, 0.6, 0.7, 1][n] * Math.min(devicePixelRatio || 1, n >= 3 ? 2 : 1.5);
  cv.width = Math.max(1, Math.round(innerWidth * escala));
  cv.height = Math.max(1, Math.round(innerHeight * escala));
}

// Abre o motor. No site: baixa favela.js/.wasm/.data, cria o WebGL 2 e abre o mapa da partida.
async function iniciarMotor(aoProgresso) {
  if (ANDROID) {
    // o Kotlin abre o mapa sozinho quando o motor fica pronto
    while (!motor.pronto()) await new Promise((r) => setTimeout(r, 120));
    return true;
  }
  ajustarCanvas();
  addEventListener('resize', ajustarCanvas);
  await new Promise((ok, falha) => {
    const s = document.createElement('script');
    s.src = 'favela.js';
    s.onload = ok;
    s.onerror = () => falha(new Error('o motor do jogo não carregou (favela.js)'));
    document.head.appendChild(s);
  });
  M = await window.FavelaMotor({
    canvas: document.getElementById('tela'),
    locateFile: (p) => p,
    print: (t) => console.log(t),
    printErr: (t) => console.warn(t),
    setStatus: (t) => aoProgresso && aoProgresso(t),
    aposQuadro: aposQuadroSite,
  });
  M.aposQuadro = aposQuadroSite;
  aoProgresso && aoProgresso('Preparando o mapa…');
  await new Promise((r) => setTimeout(r, 30));
  if (!M.ccall('web_iniciar', 'number', ['number'], [motor.nivel()])) throw new Error(PLATAFORMA === 'ios' ? 'Seu iPhone/iPad precisa do iOS 15 ou mais novo (WebGL 2). Atualize o sistema e abra pelo Safari.' : 'Seu navegador não tem WebGL 2. Use o Chrome ou o Firefox atualizados.');
  // o mapa fica guardado e só abre na primeira partida (barracas de roupa fora: roupa é no mercado)
  projetoPartida = await (await fetch('exemplos/5_policia_x_bandidos.json')).json();
  projetoPartida.objetos = (projetoPartida.objetos || []).filter((o) => !(o.tipo === 'loja' && o.loja === 'roupas'));
  return true;
}
