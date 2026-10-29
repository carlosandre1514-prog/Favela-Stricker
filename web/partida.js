// Favela Stricker — a partida: HUD, controles, sensibilidade, editor do HUD, loja de armas da partida,
// partida ao vivo pelo servidor (WebSocket) e chat de voz do time.
'use strict';

// ---------------- peças do HUD que o jogador pode mover e redimensionar ----------------
// x, y: centro da peça em fração da tela (0 a 1). Padrão = o modelo das telas.
const PECAS = {
  analogico: { nome: 'Analógico', x: 0.107, y: 0.805 },
  tiro2: { nome: 'Atirar (esquerda)', x: 0.090, y: 0.555 },  // abaixo do minimapa
  tiro: { nome: 'Atirar', x: 0.925, y: 0.777 },
  mira: { nome: 'Mirar', x: 0.828, y: 0.803 },
  pular: { nome: 'Pular', x: 0.949, y: 0.562 },
  agachar: { nome: 'Agachar', x: 0.873, y: 0.603 },
  granada: { nome: 'Granada', x: 0.797, y: 0.628 },
  recarregar: { nome: 'Recarregar', x: 0.751, y: 0.795 },
  armas: { nome: 'Armas', x: 0.444, y: 0.895 },
  minimapa: { nome: 'Minimapa', x: 0.066, y: 0.308 },
  falar: { nome: 'Falar (voz)', x: 0.949, y: 0.385, online: true },
  ouvir: { nome: 'Som da voz', x: 0.949, y: 0.21, online: true },
};
function carregarLayout() {
  const g = guardado.get('hud_layout', {});
  const l = {};
  for (const k in PECAS) l[k] = Object.assign({ x: PECAS[k].x, y: PECAS[k].y, s: 1, o: 1 }, g[k] || {});
  return l;
}
let layout = carregarLayout();
function salvarLayout() { guardado.set('hud_layout', layout); }
function posicionarPeca(el, id) {
  const p = layout[id];
  el.style.left = (p.x * larguraPalco()).toFixed(1) + 'px';
  el.style.top = (p.y * alturaPalco()).toFixed(1) + 'px';
  el.style.transform = `translate(-50%, -50%) scale(${p.s})`;
  el.style.opacity = p.o;
}
function posicionarTodas(raiz) { for (const el of $$('.peca[data-peca]', raiz)) posicionarPeca(el, el.dataset.peca); }

function pecaHtml(id) {
  switch (id) {
    case 'analogico': return '<div class="peca so-toque" data-peca="analogico" id="analogico" style="width:108px;height:108px"><div class="bola"></div></div>';
    case 'tiro2': return `<button class="peca so-toque redondo tiro esq" data-peca="tiro2" id="b-tiro2" style="width:56px;height:56px" aria-label="Atirar (lado esquerdo)">${ICONE.alvo(24)}</button>`;
    case 'tiro': return `<button class="peca so-toque redondo tiro" data-peca="tiro" id="b-tiro" style="width:82px;height:82px" aria-label="Atirar">${ICONE.alvo(34)}</button>`;
    case 'mira': return '<button class="peca so-toque redondo" data-peca="mira" id="b-mira" style="width:54px;height:54px;font-size:11px">MIRA</button>';
    case 'pular': return `<button class="peca so-toque redondo" data-peca="pular" id="b-pular" style="width:54px;height:54px">${ICONE.cima}<span class="r">PULAR</span></button>`;
    case 'agachar': return `<button class="peca so-toque redondo" data-peca="agachar" id="b-agachar" style="width:54px;height:54px">${ICONE.baixo}<span class="r">AGACHAR</span></button>`;
    case 'granada': return `<button class="peca so-toque redondo" data-peca="granada" id="b-granada" style="width:54px;height:54px;border-color:rgba(252,211,77,.8)">${ICONE.granada}<span class="contador" id="n-granadas">2</span></button>`;
    case 'recarregar': return `<button class="peca so-toque redondo" data-peca="recarregar" id="b-recarregar" style="width:48px;height:48px">${ICONE.recarregar}</button>`;
    case 'armas': return `<div class="peca" data-peca="armas" id="armas"><button class="slot ativa" data-slot="0"><img alt=""><span>–</span></button><button class="slot" data-slot="1"><img alt=""><span>–</span></button><button class="troca" id="b-trocar" aria-label="Troca rápida de arma">${ICONE.trocar}</button></div>`;
    case 'minimapa': return '<canvas class="peca" data-peca="minimapa" id="minimapa" width="168" height="168" style="width:84px;height:84px"></canvas>';
    case 'falar': return `<button class="peca redondo" data-peca="falar" id="b-falar" style="width:48px;height:48px" aria-label="Segure para falar">${ICONE.microfone}</button>`;
    case 'ouvir': return `<button class="peca redondo" data-peca="ouvir" id="b-ouvir" style="width:40px;height:40px" aria-label="Som da voz">${ICONE.altofalante}</button>`;
  }
  return '';
}

// computador: o mouse some e vira a câmera (o navegador pode recusar sem um clique; tenta de novo no próximo)
function travarMouse() {
  try { const r = document.getElementById('tela').requestPointerLock(); if (r && r.catch) r.catch(() => {}); } catch (_) {}
}

// prende o dedo no elemento (alguns navegadores recusam quando o toque já acabou)
function capturar(el, id) { try { el.setPointerCapture(id); } catch (_) {} }

// ---------------- sensibilidade ----------------
let ultimoHud = {};
function fatorOlhar() {
  const mirando = ultimoHud.mirando;
  return config.sens * (mirando ? (ultimoHud.luneta ? config.sensLuneta : config.sensMira) : 1);
}
function olharTela(dx, dy) {  // dx, dy em pixels de tela (CSS)
  const k = 2.2 * fatorOlhar();
  motor.olhar(dx * k, (config.inverterY ? -dy : dy) * k);
}

// ---------------- partida ----------------
let partida = null;  // {time, sala, hud, fim, loja}
let lacoHud = null;

function iniciarPartida(time, roupa, sala, armaPrincipal) {
  motor.girarMenu(false);
  ctxAudio();
  partida = { time, sala, fim: false, agachado: false, mirando: false, danoAntes: false };
  ultimoHud = {};
  montarHud(!!sala);
  motor.redeSala(sala ? sala.host : false, sala ? fb.uid : '', sala ? sala.roster() : '');
  for (const a of ARMAS) motor.skinArma(a.key, conta.skinArma(a.key));
  motor.escolhas(time, roupa.i, armaPrincipal || armaInicial(time), 'tt33');
  motor.primeiraPessoa(true);
  motor.play(true);
  // polícia olha para o morro; bandidos, para a rua de baixo
  motor.camera(0, 0, 0, time === 0 ? 0 : 3.14159, 0.05, 3);
  if (sala) ligarAoVivo(sala, time);
  if (!TOQUE) travarMouse();
  clearInterval(lacoHud);
  lacoHud = setInterval(() => {
    if (!partida) return;
    const j = motor.hud();
    if (!j.ativo) return;
    ultimoHud = j;
    atualizarHud(j);
    if (partida.loja) atualizarLojaPartida();
    if (!partida.fim && j.estado) {
      partida.fim = true;
      motor.atirar(false);
      setTimeout(() => fimDePartida(j), 2500);
    }
  }, 100);
}

function pararPartida() {
  if (!partida) return;
  clearInterval(lacoHud);
  lacoHud = null;
  motor.atirar(false);
  desligarAoVivo();
  motor.andar(0, 0, false);
  motor.mirar(false);
  motor.agachar(false);
  motor.lojaFechar();
  motor.play(false);
  motor.primeiraPessoa(false);
  motor.redeSala(false, '', '');
  motor.visaoGeral();
  motor.girarMenu(true);
  partida = null;
  if (document.pointerLockElement) document.exitPointerLock();
}

// ---------------- HUD ----------------
function montarHud(online) {
  const ids = Object.keys(PECAS).filter((k) => online || !PECAS[k].online);
  const t = mostrar(html(`<div id="hud">
    <div id="zona-andar" class="toque so-toque"></div><div id="zona-olhar" class="toque so-toque"></div>
    <canvas id="luneta"></canvas><div id="dano"></div>
    <canvas id="mira" width="160" height="160" style="width:80px;height:80px"></canvas>
    <div id="vida" class="caixa">${ICONE.coracao}<div class="trilho"><div class="barra"></div></div><b id="hp">100</b></div>
    <div id="info"><span id="dinheiro">$ 0</span><span id="kd">0 abates · 0 mortes</span></div>
    <div id="placar"><div class="faixa"><div class="pol"><span class="rot">POLÍCIA</span><span class="num" id="pt-pol">0</span></div>
      <div class="relogio" id="relogio">10:00</div><div class="ban"><span class="num" id="pt-ban">0</span><span class="rot">BANDIDOS</span></div></div>
      <span class="meta" id="meta">até 25 abates</span></div>
    <div id="rede" style="display:${online ? '' : 'none'}"></div>
    <div id="abates"></div>
    <button id="pausar" aria-label="Pausar">${ICONE.pausa}</button>
    <div id="centro"></div>
    <button id="botao-loja">${ICONE.sacola}<span>Loja de armas</span></button>
    ${ids.map(pecaHtml).join('')}
    <div id="teclas" class="so-teclado">WASD andar · Shift correr · mouse olhar · clique atirar · botão direito mirar · Espaço pular · C agachar · R recarregar · Q trocar arma · G granada · E loja${online ? ' · V falar (segure)' : ''} · Esc pausa</div>
  </div>`), 'hud');
  partida.hud = t;
  posicionarTodas(t);
  $('#pausar', t).onclick = pausar;
  $('#botao-loja', t).onclick = () => {
    const rot = ultimoHud.interagir || '';
    if (/Loja de armas/.test(rot)) abrirLojaPartida();
    else motor.interagir();
  };
  ligarControlesToque(t);
  for (const s of $$('.slot', t)) s.onclick = () => { if (Number(s.dataset.slot) !== ultimoHud.slot) motor.trocar(); };
  $('#b-trocar', t).onclick = () => motor.trocar();
  if (online) {
    const fala = $('#b-falar', t);
    const on = (e) => { e.preventDefault(); falar(true); fala.classList.add('ligado'); };
    const off = (e) => { e.preventDefault(); falar(false); fala.classList.remove('ligado'); };
    fala.addEventListener('pointerdown', on);
    for (const ev of ['pointerup', 'pointercancel', 'pointerleave']) fala.addEventListener(ev, off);
    $('#b-ouvir', t).onclick = () => {
      if (!voz) return;
      voz.ouvindo = !voz.ouvindo;
      $('#b-ouvir', t).innerHTML = voz.ouvindo ? ICONE.altofalante : ICONE.mudo;
      aviso(voz.ouvindo ? 'Voz do time ligada' : 'Voz do time desligada');
    };
  }
  desenharLuneta($('#luneta', t));
  desenharMira($('#mira', t), false, false, false);
}
aoRedimensionar.push(() => {
  if (partida && partida.hud) { posicionarTodas(partida.hud); desenharLuneta($('#luneta', partida.hud)); }
  const ed = $('#editor-hud');
  if (ed) posicionarTodas(ed);
});

function desenharMira(cv, mirando, acerto, cabeca) {
  const c = cv.getContext('2d'), w = cv.width, cx = w / 2, cy = w / 2, k = w / 80;
  c.clearRect(0, 0, w, w);
  c.lineCap = 'round';
  if (mirando) {  // mirando: a bolinha vermelha é onde a bala vai
    c.fillStyle = 'rgba(0,0,0,.75)'; c.beginPath(); c.arc(cx, cy, 3.4 * k, 0, 7); c.fill();
    c.fillStyle = '#FF2A1F'; c.beginPath(); c.arc(cx, cy, 2.4 * k, 0, 7); c.fill();
  } else {  // cruz do modelo (com contorno escuro para ver em fundo claro)
    const g = 5 * k, l = 9 * k;
    for (const [cor, lw] of [['rgba(0,0,0,.55)', 4 * k], ['#fff', 2 * k]]) {
      c.strokeStyle = cor; c.lineWidth = lw; c.beginPath();
      c.moveTo(cx, cy - g - l); c.lineTo(cx, cy - g); c.moveTo(cx, cy + g); c.lineTo(cx, cy + g + l);
      c.moveTo(cx - g - l, cy); c.lineTo(cx - g, cy); c.moveTo(cx + g, cy); c.lineTo(cx + g + l, cy); c.stroke();
    }
    c.fillStyle = 'rgba(0,0,0,.55)'; c.beginPath(); c.arc(cx, cy, 2.4 * k, 0, 7); c.fill();
    c.fillStyle = '#fff'; c.beginPath(); c.arc(cx, cy, 1.5 * k, 0, 7); c.fill();
  }
  if (acerto) {  // marca de acerto (cabeça: maior e vermelha)
    c.strokeStyle = cabeca ? '#FF3B30' : '#fff'; c.lineWidth = 2 * k;
    const a = (cabeca ? 13 : 10) * k, b = 5 * k;
    c.beginPath();
    c.moveTo(cx - a, cy - a); c.lineTo(cx - b, cy - b); c.moveTo(cx + a, cy - a); c.lineTo(cx + b, cy - b);
    c.moveTo(cx - a, cy + a); c.lineTo(cx - b, cy + b); c.moveTo(cx + a, cy + a); c.lineTo(cx + b, cy + b); c.stroke();
  }
}

// Luneta da M4 Colt: tela preta com o círculo da lente, a cruz fina e o ponto vermelho
function desenharLuneta(cv) {
  if (!cv) return;
  const w = cv.width = Math.round(larguraPalco() * 2), h = cv.height = Math.round(alturaPalco() * 2), c = cv.getContext('2d');
  const cx = w / 2, cy = h / 2, r = Math.min(w, h) * 0.47;
  c.fillStyle = '#000'; c.beginPath(); c.rect(0, 0, w, h); c.arc(cx, cy, r, 0, Math.PI * 2, true); c.fill();
  const grad = c.createRadialGradient(cx, cy, r * 0.86, cx, cy, r);
  grad.addColorStop(0, 'rgba(0,0,0,0)'); grad.addColorStop(1, 'rgba(0,0,0,.8)');
  c.fillStyle = grad; c.beginPath(); c.arc(cx, cy, r, 0, 7); c.fill();
  c.strokeStyle = 'rgba(16,16,16,.95)';
  c.lineWidth = 6; c.beginPath(); c.moveTo(cx - r, cy); c.lineTo(cx - r * 0.45, cy); c.moveTo(cx + r * 0.45, cy); c.lineTo(cx + r, cy); c.moveTo(cx, cy + r * 0.45); c.lineTo(cx, cy + r); c.stroke();
  const gap = r * 0.06;
  c.lineWidth = 2.4; c.beginPath(); c.moveTo(cx - r * 0.45, cy); c.lineTo(cx - gap, cy); c.moveTo(cx + gap, cy); c.lineTo(cx + r * 0.45, cy);
  c.moveTo(cx, cy + gap); c.lineTo(cx, cy + r * 0.45); c.moveTo(cx, cy - r * 0.3); c.lineTo(cx, cy - gap); c.stroke();
  c.fillStyle = 'rgba(255,42,31,.35)'; c.beginPath(); c.arc(cx, cy, 8, 0, 7); c.fill();
  c.fillStyle = '#FF2A1F'; c.beginPath(); c.arc(cx, cy, 4, 0, 7); c.fill();
}

function desenharMinimapa(cv, pontos, meuTime) {
  if (!cv) return;
  const c = cv.getContext('2d'), w = cv.width, cx = w / 2, cy = w / 2, r = w / 2;
  c.clearRect(0, 0, w, w);
  c.fillStyle = 'rgba(11,15,23,.72)'; c.beginPath(); c.arc(cx, cy, r - 2, 0, 7); c.fill();
  c.strokeStyle = 'rgba(255,255,255,.25)'; c.lineWidth = 4; c.beginPath(); c.arc(cx, cy, r - 2, 0, 7); c.stroke();
  const k = (r - 12) / 45;
  for (const p of pontos || []) {  // companheiros na cor do time; inimigo (que atirou agora há pouco) em vermelho
    c.fillStyle = p[2] === 1 ? '#F87171' : meuTime === 0 ? '#60A5FA' : '#FBBF24';
    c.beginPath(); c.arc(cx + p[0] * k, cy - p[1] * k, 6, 0, 7); c.fill();
  }
  c.fillStyle = '#fff'; c.beginPath(); c.moveTo(cx, cy - 11); c.lineTo(cx - 7, cy + 7); c.lineTo(cx + 7, cy + 7); c.fill();
}

const corTime = (t) => (t === 0 ? '#93C5FD' : '#FCD34D');
function atualizarHud(j) {
  const t = partida && partida.hud;
  if (!t || !t.isConnected) return;
  const hp = j.vida | 0, max = Math.max(1, j.vidaMax | 0);
  const barra = $('#vida .barra', t);
  barra.style.width = (100 * hp / max) + '%';
  barra.style.background = hp > 50 ? '#4ADE80' : hp > 25 ? '#FBBF24' : '#F87171';
  $('#hp', t).textContent = hp;
  $('#dinheiro', t).textContent = '$ ' + fmt(j.dinheiro | 0);
  $('#kd', t).textContent = `${j.abates | 0} abate${(j.abates | 0) === 1 ? '' : 's'} · ${j.mortes | 0} morte${(j.mortes | 0) === 1 ? '' : 's'}`;
  const pl = j.placar || [0, 0];
  $('#pt-pol', t).textContent = pl[0];
  $('#pt-ban', t).textContent = pl[1];
  $('#relogio', t).textContent = j.tempo < 0 ? '--:--' : `${Math.floor(j.tempo / 60)}:${String(j.tempo % 60).padStart(2, '0')}`;
  $('#meta', t).textContent = `até ${j.meta || 25} abates`;
  const ng = $('#n-granadas', t);
  if (ng) ng.textContent = j.granadas | 0;
  // armas: imagem com a skin escolhida, balas no pente / reserva
  const armas = j.armas || [];
  $$('.slot', t).forEach((s, i) => {
    const a = armas[i];
    s.style.display = a ? '' : 'none';
    if (!a) return;
    s.classList.toggle('ativa', i === j.slot);
    const sk = conta.skinArma(a.arma);
    const src = `armas/${a.arma}${sk ? '_' + sk : ''}.png`;
    const img = s.querySelector('img');
    if (img.getAttribute('src') !== src) img.src = src;
    const recarregando = j.recarregando && i === j.slot;
    const sp = s.querySelector('span');
    sp.textContent = recarregando ? 'recarregando…' : `${a.balas} / ${a.reserva < 0 ? '∞' : a.reserva}`;
    sp.style.color = a.balas === 0 && !recarregando ? '#F87171' : '#F3F4F6';
  });
  const bt = $('#b-trocar', t);
  if (bt) bt.style.display = armas.length > 1 ? '' : 'none';
  // mira e luneta
  const luneta = j.mirando && j.luneta;
  $('#luneta', t).style.display = luneta ? 'block' : 'none';
  const mira = $('#mira', t);
  mira.style.visibility = luneta ? 'hidden' : 'visible';
  desenharMira(mira, j.mirando, j.acerto, j.cabeca);
  const bm = $('#b-mira', t);
  if (bm) bm.classList.toggle('ligado', !!j.mirando);
  // dano: tela vermelha nas bordas (e vibra, se ligado)
  $('#dano', t).style.display = j.dano ? 'block' : 'none';
  if (j.dano && !partida.danoAntes && config.vibrar) motor.vibrar(35);
  partida.danoAntes = !!j.dano;
  const centro = $('#centro', t);
  const msg = j.morto ? `Você foi abatido<br><span style="font-size:13px;font-weight:600">Renasce em ${j.renasce} s</span>` : j.estado ? esc(j.msg) : '';
  centro.innerHTML = msg;
  centro.style.display = msg ? 'block' : 'none';
  // botão do meio: loja de armas na base (ou outra interação perto)
  const rot = (j.interagir || '').replace(/^[^\p{L}🔒]+/u, '');
  const bl = $('#botao-loja', t);
  bl.style.display = rot && !j.morto && !partida.loja ? 'flex' : 'none';
  bl.classList.toggle('outro', !/Loja de armas/.test(rot));
  bl.querySelector('span').textContent = rot;
  // abates (últimos 4)
  const feed = (j.abatidos || []).slice(-4);
  $('#abates', t).innerHTML = feed.map((f) => (f.a
    ? `<span><b style="color:${corTime(f.at)}">${f.voce ? 'Você' : esc(f.a)}</b> abateu <b style="color:${corTime(f.vt)}">${esc(f.v)}</b>${f.arma ? ' · ' + esc(f.arma) : ''}</span>`
    : `<span><b style="color:${corTime(f.vt)}">${esc(f.v)}</b> caiu</span>`)).join('');
  desenharMinimapa($('#minimapa', t), j.mapa, j.time | 0);
}

// ---------------- controles de toque: metade esquerda anda, metade direita olha ----------------
function ligarControlesToque(t) {
  const analog = $('#analogico', t), bola = analog && analog.querySelector('.bola');
  const zonaA = $('#zona-andar', t), zonaO = $('#zona-olhar', t);
  let dedoAndar = null, ox = 0, oy = 0;
  const raio = () => 54 * layout.analogico.s;
  const pontoPalco = (e) => [e.clientX / escala, e.clientY / escala];
  const soltarAnalogico = () => {
    dedoAndar = null;
    motor.andar(0, 0, false);
    if (bola) bola.style.transform = '';
    if (analog) posicionarPeca(analog, 'analogico');
  };
  const inicioAndar = (e) => {
    if (dedoAndar !== null) return;
    ctxAudio();
    dedoAndar = e.pointerId;
    [ox, oy] = pontoPalco(e);
    // analógico flutuante: aparece onde o dedo encostou
    analog.style.left = ox + 'px';
    analog.style.top = oy + 'px';
    capturar(e.currentTarget, e.pointerId);
    e.preventDefault();
  };
  const moverAndar = (e) => {
    if (e.pointerId !== dedoAndar) return;
    const [px, py] = pontoPalco(e);
    const R = raio();
    let dx = (px - ox) / R, dy = (py - oy) / R;
    const len = Math.hypot(dx, dy);
    if (len > 1) {  // passou da borda: o analógico acompanha o dedo
      ox += (dx - dx / len) * R; oy += (dy - dy / len) * R; dx /= len; dy /= len;
      analog.style.left = ox + 'px'; analog.style.top = oy + 'px';
    }
    bola.style.transform = `translate(${dx * R * 0.55 / layout.analogico.s}px, ${dy * R * 0.55 / layout.analogico.s}px)`;
    motor.andar(dx, -dy, -dy > 0.93);  // empurrando até o fim para frente: corre
  };
  const fimAndar = (e) => { if (e.pointerId === dedoAndar) soltarAnalogico(); };
  for (const alvo of [zonaA, analog]) {
    if (!alvo) continue;
    alvo.addEventListener('pointerdown', inicioAndar);
    alvo.addEventListener('pointermove', moverAndar);
    alvo.addEventListener('pointerup', fimAndar);
    alvo.addEventListener('pointercancel', fimAndar);
  }
  // olhar: arrastar na metade direita (vários dedos ao mesmo tempo)
  const ult = new Map();
  zonaO.addEventListener('pointerdown', (e) => { ctxAudio(); ult.set(e.pointerId, [e.clientX, e.clientY]); capturar(zonaO, e.pointerId); });
  zonaO.addEventListener('pointermove', (e) => {
    const p = ult.get(e.pointerId);
    if (!p) return;
    olharTela(e.clientX - p[0], e.clientY - p[1]);
    ult.set(e.pointerId, [e.clientX, e.clientY]);
  });
  const soltaO = (e) => ult.delete(e.pointerId);
  zonaO.addEventListener('pointerup', soltaO);
  zonaO.addEventListener('pointercancel', soltaO);
  // atirar: segurar atira; arrastar o mesmo dedo também mira
  for (const id of ['#b-tiro', '#b-tiro2']) {
    const b = $(id, t);
    if (!b) continue;
    let px = 0, py = 0, dedo = null;
    b.addEventListener('pointerdown', (e) => {
      ctxAudio(); dedo = e.pointerId; px = e.clientX; py = e.clientY;
      capturar(b, e.pointerId); b.style.filter = 'brightness(1.3)'; motor.atirar(true);
    });
    b.addEventListener('pointermove', (e) => {
      if (e.pointerId !== dedo) return;
      olharTela(e.clientX - px, e.clientY - py); px = e.clientX; py = e.clientY;
    });
    const solta = (e) => { if (e.pointerId !== dedo) return; dedo = null; b.style.filter = ''; motor.atirar(false); };
    b.addEventListener('pointerup', solta);
    b.addEventListener('pointercancel', solta);
  }
  const botao = (id, f) => { const b = $(id, t); if (b) b.addEventListener('pointerdown', (e) => { e.preventDefault(); f(b); }); };
  botao('#b-mira', () => { partida.mirando = !ultimoHud.mirando; motor.mirar(partida.mirando); });
  botao('#b-pular', () => motor.pular());
  botao('#b-agachar', (b) => { partida.agachado = !partida.agachado; motor.agachar(partida.agachado); b.classList.toggle('ligado', partida.agachado); });
  botao('#b-granada', () => { if (!motor.granada() && (ultimoHud.granadas | 0) === 0) aviso('Sem granadas: compre na loja da base ($ 300)'); });
  botao('#b-recarregar', () => motor.recarregar());
}

// ---------------- teclado e mouse (computador) ----------------
const teclas = new Set();
function enviarAndar() {
  if (!partida) return;
  const x = (teclas.has('KeyD') ? 1 : 0) - (teclas.has('KeyA') ? 1 : 0);
  const y = (teclas.has('KeyW') ? 1 : 0) - (teclas.has('KeyS') ? 1 : 0);
  const n = Math.hypot(x, y) || 1;
  motor.andar(x / n, y / n, (teclas.has('ShiftLeft') || teclas.has('ShiftRight')) && y > 0);
}
addEventListener('keydown', (e) => {
  if (!partida || e.target.tagName === 'INPUT') return;
  if (e.code === 'Escape') { if (partida.loja) fecharLojaPartida(); else pausar(); return; }
  if (e.repeat) return;
  teclas.add(e.code);
  switch (e.code) {
    case 'Space': motor.pular(); e.preventDefault(); break;
    case 'KeyC': case 'ControlLeft': partida.agachado = !partida.agachado; motor.agachar(partida.agachado); break;
    case 'KeyR': motor.recarregar(); break;
    case 'KeyQ': case 'Digit1': case 'Digit2': motor.trocar(); break;
    case 'KeyG': motor.granada(); break;
    case 'KeyE': if (/Loja de armas/.test(ultimoHud.interagir || '')) abrirLojaPartida(); else motor.interagir(); break;
    case 'KeyV': falar(true); break;
  }
  enviarAndar();
});
addEventListener('keyup', (e) => {
  teclas.delete(e.code);
  if (e.code === 'KeyV') falar(false);
  enviarAndar();
});
addEventListener('blur', () => { teclas.clear(); if (partida) { motor.atirar(false); enviarAndar(); } });
(() => {
  const tela = document.getElementById('tela');
  const travado = () => document.pointerLockElement === tela;
  document.addEventListener('mousedown', (e) => {
    if (!partida || TOQUE || partida.loja || $('.camada')) return;
    if (e.target.closest('button, input, .slot')) return;
    ctxAudio();
    if (!travado()) { travarMouse(); return; }
    if (e.button === 0) motor.atirar(true);
    if (e.button === 2) motor.mirar(true);
  });
  addEventListener('mouseup', (e) => { if (!partida) return; if (e.button === 0) motor.atirar(false); if (e.button === 2) motor.mirar(false); });
  addEventListener('contextmenu', (e) => { if (partida) e.preventDefault(); });
  addEventListener('mousemove', (e) => {
    if (!partida || !travado()) return;
    const k = 0.9 * fatorOlhar();
    motor.olhar(e.movementX * k, (config.inverterY ? -e.movementY : e.movementY) * k);
  });
})();

// ---------------- pausa ----------------
function pausar() {
  if (!partida || partida.fim || $('.camada')) return;
  motor.atirar(false);
  if (document.pointerLockElement) document.exitPointerLock();
  const c = html(`<div class="camada fixo"><div class="janela" style="width:400px">
    <span class="t">PAUSA</span>
    ${ajusteHtml('sens', 'Câmera', 0.2, 3, 0.05)}${ajusteHtml('sensMira', 'Mirando', 0.2, 2, 0.05)}${ajusteHtml('sensLuneta', 'Luneta', 0.1, 1.5, 0.05)}
    <div class="linha" style="margin-top:4px"><button class="btn btn-fraco" id="p-sair">Sair da partida</button><span class="cresce"></span><button class="btn btn-ouro" id="p-voltar">Continuar</button></div>
  </div></div>`);
  palco.appendChild(c);
  ligarAjustes(c);
  $('#p-voltar', c).onclick = () => { c.remove(); if (!TOQUE) travarMouse(); };
  $('#p-sair', c).onclick = async () => {
    c.remove();
    if (await confirmar('Sair da partida?', partida.sala ? 'Você sai da sala também.' : 'O progresso desta partida não conta.', 'Sair')) {
      pararPartida(); sairDaSala(); telaInicio();
    }
  };
}

// controle deslizante de configuração (usado na pausa e nas configurações)
function ajusteHtml(chave, nome, min, max, passo, dica = '', fmtValor = (v) => v.toFixed(2)) {
  return `<div class="ajuste" data-ajuste="${chave}" data-min="${min}" data-max="${max}"><span class="nome">${esc(nome)}${dica ? `<small>${esc(dica)}</small>` : ''}</span>
    <input type="range" min="${min}" max="${max}" step="${passo}" value="${config[chave]}"><span class="valor">${fmtValor(config[chave])}</span></div>`;
}
function ligarAjustes(raiz, fmtValores = {}) {
  for (const a of $$('[data-ajuste]', raiz)) {
    const k = a.dataset.ajuste, r = a.querySelector('input'), v = a.querySelector('.valor');
    r.oninput = () => {
      config[k] = parseFloat(r.value);
      v.textContent = (fmtValores[k] || ((x) => x.toFixed(2)))(config[k]);
      salvarConfig();
      if (k === 'volume') motor.volume(config.volume);
    };
  }
}

// ---------------- loja de armas da partida (compra com o $ dos abates) ----------------
let lojaSel = null;
function abrirLojaPartida() {
  if (!partida) return;
  motor.interagir();
  motor.atirar(false);
  motor.andar(0, 0, false);
  const j = motor.loja();
  if (!j) { aviso('Vá até a base do seu time para abrir a loja'); return; }
  if (document.pointerLockElement) document.exitPointerLock();
  partida.loja = html(`<div id="loja-partida" class="fixo">
    <div class="linha"><span class="bebas" style="font-size:36px">LOJA DE ARMAS</span>
      <span class="etiqueta" style="background:${partida.time ? '#B45309' : '#1D4ED8'};color:#fff">${partida.time ? 'BANDIDOS' : 'POLÍCIA'}</span>
      <span class="dica">compra com o $ ganho na partida</span><span class="cresce"></span>
      <span class="chip" id="lp-dinheiro" style="color:#86EFAC;font-weight:800;font-size:14px">$ 0</span>
      <button class="voltar" id="lp-fechar" aria-label="Fechar">${ICONE.fechar}</button></div>
    <div class="linha cresce" style="align-items:stretch;gap:14px"><div class="lista-armas" id="lp-lista"></div><div class="detalhe cresce" id="lp-det"></div></div>
  </div>`);
  palco.appendChild(partida.loja);
  $('#lp-fechar', partida.loja).onclick = fecharLojaPartida;
  lojaSel = null;
  atualizarLojaPartida(true);
}
function fecharLojaPartida() {
  if (!partida || !partida.loja) return;
  partida.loja.remove();
  partida.loja = null;
  motor.lojaFechar();
  if (!TOQUE) travarMouse();
}
function atualizarLojaPartida(redesenhar) {
  const el = partida && partida.loja;
  if (!el) return;
  const j = motor.loja();
  if (!j) { fecharLojaPartida(); return; }
  const din = j.dinheiro | 0;
  $('#lp-dinheiro', el).textContent = '$ ' + fmt(din);
  const chave = JSON.stringify(j.itens) + din;
  if (!redesenhar && el.dataset.chave === chave) return;
  el.dataset.chave = chave;
  const itens = j.itens || [];
  if (lojaSel === null || !itens.some((i) => i.id === lojaSel)) lojaSel = (itens.find((i) => i.usando) || itens[0] || {}).id;
  const img = (it) => (it.arma === 'granada' ? '' : `armas/${it.arma}${conta.skinArma(it.arma) ? '_' + conta.skinArma(it.arma) : ''}.png`);
  $('#lp-lista', el).innerHTML = itens.map((it) => {
    const estado = it.usando ? '<span class="azul" style="font-size:12px">usando</span>'
      : it.tem ? '<span class="verde" style="font-size:12px">sua</span>'
      : `<span style="color:${din >= it.preco ? '#86EFAC' : '#FCA5A5'}">$ ${fmt(it.preco)}</span>`;
    const icone = it.arma === 'granada' ? `<span style="width:68px;display:flex;justify-content:center">${ICONE.granada}</span>` : `<img src="${img(it)}" alt="">`;
    return `<div class="linha-arma ${it.id === lojaSel ? 'sel' : ''}" data-id="${it.id}">${icone}<span class="cresce">${esc(it.nome)}</span>${estado}</div>`;
  }).join('');
  for (const d of $$('.linha-arma', el)) d.onclick = () => { lojaSel = Number(d.dataset.id); atualizarLojaPartida(true); };
  const it = itens.find((i) => i.id === lojaSel);
  if (!it) return;
  const det = $('#lp-det', el);
  const ficha = it.arma === 'granada'
    ? `Até 4 m: abate na hora · 4 a 8 m: 100 até 20 de dano · explode 3 s depois de jogar · você tem ${it.qtd}/${it.max}`
    : `${it.dano} de dano no corpo, ${it.cabeca} na cabeça · ${it.tiros} tiros para abater · pente ${it.pente}`;
  const barras = it.arma === 'granada' ? '' : `<div class="barras">
      <span>Dano</span><span class="t"><i style="width:${Math.round(it.barra_dano * 100)}%;background:#F87171"></i></span>
      <span>Cadência</span><span class="t"><i style="width:${Math.round(it.barra_cadencia * 100)}%;background:#FBBF24"></i></span>
      <span>Precisão</span><span class="t"><i style="width:${Math.round(it.barra_precisao * 100)}%;background:#60A5FA"></i></span></div>`;
  let botao;
  if (it.arma === 'granada') botao = it.qtd >= it.max ? '<button class="btn btn-escuro" disabled>JÁ TEM 2 GRANADAS</button>'
    : din >= it.preco ? `<button class="btn btn-ouro" id="lp-acao">COMPRAR POR $ ${fmt(it.preco)}</button>` : `<button class="btn" disabled style="background:#4B1D1D;color:#FCA5A5">FALTAM $ ${fmt(it.preco - din)}</button>`;
  else if (it.usando) botao = '<button class="btn btn-escuro" disabled>USANDO</button>';
  else if (it.tem) botao = '<button class="btn btn-azul" id="lp-acao">USAR</button>';
  else if (din >= it.preco) botao = `<button class="btn btn-ouro" id="lp-acao">COMPRAR POR $ ${fmt(it.preco)}</button>`;
  else botao = `<button class="btn" disabled style="background:#4B1D1D;color:#FCA5A5;font-weight:800">FALTAM $ ${fmt(it.preco - din)}</button>`;
  det.innerHTML = `<div class="linha"><span class="bebas cresce" style="font-size:34px">${esc(it.nome.toUpperCase())}</span>
      <span style="font-weight:800;font-size:16px;color:${it.tem ? '#93C5FD' : din >= it.preco ? '#86EFAC' : '#FCA5A5'}">${it.tem && it.arma !== 'granada' ? 'SUA' : '$ ' + fmt(it.preco)}</span></div>
    <div class="previa">${it.arma === 'granada' ? ICONE.granada.replace(/width="24" height="24"/, 'width="90" height="90"') : `<img src="${img(it)}" alt="${esc(it.nome)}">`}</div>
    <span class="dica">${esc(ficha)}</span>${barras}${botao}`;
  const acao = $('#lp-acao', det);
  if (acao) acao.onclick = () => { aviso(motor.lojaComprar(it.id) || 'Pronto'); atualizarLojaPartida(true); };
}

// ---------------- editor do HUD: arrastar, tamanho e transparência de cada botão ----------------
function abrirEditorHud(aoSair) {
  const ed = mostrar(html(`<div id="editor-hud" class="tela" style="background:rgba(8,11,18,.55);padding:0">
    <div id="hud" style="pointer-events:none">
      <div id="vida" class="caixa">${ICONE.coracao}<div class="trilho"><div class="barra" style="width:72%"></div></div><b>72</b></div>
      <div id="placar"><div class="faixa"><div class="pol"><span class="rot">POLÍCIA</span><span class="num">6</span></div><div class="relogio">7:42</div><div class="ban"><span class="num">4</span><span class="rot">BANDIDOS</span></div></div></div>
      <canvas id="mira" width="160" height="160" style="width:80px;height:80px"></canvas>
    </div>
    ${Object.keys(PECAS).map((id) => pecaHtml(id).replace('class="peca', 'class="peca editavel')).join('')}
    <div class="barra"><span class="dica" id="ed-nome" style="width:110px;color:#F3F4F6;font-weight:800">Toque num botão</span>
      <span class="dica">Tamanho</span><input type="range" id="ed-tam" min="0.6" max="1.6" step="0.05" value="1">
      <span class="dica">Visível</span><input type="range" id="ed-opa" min="0.3" max="1" step="0.05" value="1">
      <button class="btn btn-fraco" id="ed-padrao" style="height:36px">Padrão</button>
      <button class="btn btn-fraco" id="ed-cancelar" style="height:36px">Cancelar</button>
      <button class="btn btn-ouro" id="ed-salvar" style="height:36px">Salvar</button></div>
  </div>`), 'editor-hud');
  const copia = JSON.parse(JSON.stringify(layout));
  desenharMira($('#mira', ed), false, false, false);
  desenharMinimapa($('#minimapa', ed), [[-8, 10, 0], [12, -6, 0], [4, 26, 1]], 0);
  $$('.slot', ed).forEach((sl, i) => { sl.querySelector('img').src = i ? 'armas/tt33.png' : 'armas/m4a1.png'; sl.querySelector('span').textContent = i ? '8 / 32' : '30 / 90'; });
  let sel = null;
  const pecas = $$('.peca[data-peca]', ed);
  for (const p of pecas) {
    p.classList.remove('so-toque');  // no editor aparecem todas, até no computador
    p.style.display = 'flex';
    p.style.pointerEvents = 'auto';
    for (const filho of $$('*', p)) filho.style.pointerEvents = 'none';
  }
  posicionarTodas(ed);
  const escolher = (p) => {
    sel = p ? p.dataset.peca : null;
    for (const q of pecas) q.classList.toggle('sel', q === p);
    $('#ed-nome', ed).textContent = sel ? PECAS[sel].nome : 'Toque num botão';
    $('#ed-tam', ed).value = sel ? layout[sel].s : 1;
    $('#ed-opa', ed).value = sel ? layout[sel].o : 1;
  };
  for (const p of pecas) {
    let arrastando = null;
    p.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      escolher(p);
      const L = larguraPalco(), A = alturaPalco();
      arrastando = { id: e.pointerId, dx: e.clientX / escala - layout[p.dataset.peca].x * L, dy: e.clientY / escala - layout[p.dataset.peca].y * A };
      capturar(p, e.pointerId);
    });
    p.addEventListener('pointermove', (e) => {
      if (!arrastando || e.pointerId !== arrastando.id) return;
      const L = larguraPalco(), A = alturaPalco(), id = p.dataset.peca;
      layout[id].x = Math.min(0.98, Math.max(0.02, (e.clientX / escala - arrastando.dx) / L));
      layout[id].y = Math.min(0.97, Math.max(0.03, (e.clientY / escala - arrastando.dy) / A));
      posicionarPeca(p, id);
    });
    const solta = () => { arrastando = null; };
    p.addEventListener('pointerup', solta);
    p.addEventListener('pointercancel', solta);
  }
  $('#ed-tam', ed).oninput = (e) => { if (!sel) return; layout[sel].s = parseFloat(e.target.value); posicionarPeca($(`[data-peca="${sel}"]`, ed), sel); };
  $('#ed-opa', ed).oninput = (e) => { if (!sel) return; layout[sel].o = parseFloat(e.target.value); posicionarPeca($(`[data-peca="${sel}"]`, ed), sel); };
  $('#ed-padrao', ed).onclick = () => {
    for (const k in PECAS) layout[k] = { x: PECAS[k].x, y: PECAS[k].y, s: 1, o: 1 };
    posicionarTodas(ed);
    escolher(null);
  };
  $('#ed-cancelar', ed).onclick = () => { layout = copia; aoSair(); };
  $('#ed-salvar', ed).onclick = () => { salvarLayout(); aviso('HUD salvo'); aoSair(); };
}

// ---------------- partida ao vivo (servidor) + chat de voz ----------------
let ws = null, enviandoTimer = null, voz = null, viaFirebase = null;
function ligarAoVivo(s, time) {
  const rede = () => partida && partida.hud && $('#rede', partida.hud);
  const status = (txt, ok) => { const r = rede(); if (r) { r.textContent = '● ' + txt; r.style.color = ok ? '#86EFAC' : '#FCD34D'; } };
  status('conectando ao servidor…', false);
  voz = new ChatDeVoz((pcm) => { if (ws && ws.readyState === 1) ws.send(pcm); });
  const base = SERVIDOR.replace(/^http/, 'ws');
  const url = `${base}/partida?sala=${encodeURIComponent(s.codigo)}&id=${encodeURIComponent(fb.uid)}&time=${time}&nome=${encodeURIComponent(conta.nome())}`;
  let tentativas = 0, abriuUmaVez = false;
  const inicio = Date.now();
  const conectar = () => {
    if (!partida || viaFirebase) return;
    ws = new WebSocket(url);
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => { tentativas = 0; abriuUmaVez = true; status('ao vivo', true); };
    ws.onmessage = (ev) => {
      if (typeof ev.data !== 'string') { const b = new Uint8Array(ev.data); if (b.length > 1 && voz) voz.receber(b[0], b.subarray(1)); return; }
      try { const j = JSON.parse(ev.data); if (j.k === 'st') motor.redeAplicar(JSON.stringify(j.d)); else if (j.k === 'ev') motor.redeEvento(JSON.stringify(j.d)); } catch (_) {}
    };
    ws.onclose = () => {
      if (!partida || viaFirebase) return;
      status('reconectando…', false);
      if (!abriuUmaVez && Date.now() - inicio > 25000) { usarFirebase(s); return; }  // servidor dormindo/fora: Firebase
      if (++tentativas <= 12) setTimeout(conectar, Math.min(5000, 800 * tentativas));
    };
  };
  conectar();
  // 20 vezes por segundo: o meu estado e os eventos (tiros que acertaram, abates, granadas)
  enviandoTimer = setInterval(() => {
    if (!ws || ws.readyState !== 1) return;
    const st = motor.redeColetar();
    if (st && st.length > 2) ws.send(`{"k":"st","d":${st}}`);
    try { for (const e of JSON.parse(motor.redeEventos() || '[]')) ws.send(JSON.stringify({ k: 'ev', d: e })); } catch (_) {}
  }, 50);
}

// Sem o servidor: estado e eventos pelo Firebase (mais devagar, sem voz)
function usarFirebase(s) {
  viaFirebase = { ouvintes: [] };
  if (ws) { try { ws.close(); } catch (_) {} ws = null; }
  clearInterval(enviandoTimer);
  const r = partida && partida.hud && $('#rede', partida.hud);
  if (r) { r.textContent = '● ao vivo (Firebase, sem voz)'; r.style.color = '#86EFAC'; }
  viaFirebase.ouvintes.push(fb.ouvir(`salas/${s.codigo}/est`, (ev, sub, dado) => {
    const out = {};
    if (sub === '/') Object.assign(out, dado || {});
    else if (!sub.slice(1).includes('/') && dado) out[sub.slice(1)] = dado;
    if (Object.keys(out).length) motor.redeAplicar(JSON.stringify(out));
  }));
  const vistos = new Set();
  viaFirebase.ouvintes.push(fb.ouvir(`salas/${s.codigo}/ev`, (_ev, sub, dado) => {
    const itens = sub === '/' ? Object.entries(dado || {}).sort() : dado ? [[sub.slice(1), dado]] : [];
    for (const [k, v] of itens) { if (vistos.has(k) || v.cel === fb.uid) continue; vistos.add(k); motor.redeEvento(JSON.stringify(v)); }
  }));
  enviandoTimer = setInterval(() => {
    const st = motor.redeColetar();
    if (st && st.length > 2) fb.patch(`salas/${s.codigo}/est`, JSON.parse(st)).catch(() => {});
    try { for (const e of JSON.parse(motor.redeEventos() || '[]')) fb.post(`salas/${s.codigo}/ev`, { ...e, cel: fb.uid }).catch(() => {}); } catch (_) {}
  }, 150);
}

function desligarAoVivo() {
  clearInterval(enviandoTimer);
  enviandoTimer = null;
  if (ws) { try { ws.close(1000, 'saiu'); } catch (_) {} ws = null; }
  if (viaFirebase) { for (const o of viaFirebase.ouvintes) o.fechar(); viaFirebase = null; }
  if (voz) { voz.desligar(); voz = null; }
}

function falar(on) { if (voz) voz.falar(on); }

// Chat de voz: 8 kHz μ-law, pacotes de 20 ms (o mesmo formato do servidor). Microfone só enquanto aperta para falar.
class ChatDeVoz {
  constructor(enviar) { this.enviar = enviar; this.ouvindo = true; this.falando = false; this.prox = {}; this.mic = null; }
  static dec(u) { u = ~u & 0xFF; let s = ((u & 0x0F) << 3) + 0x84; s <<= (u & 0x70) >> 4; return (u & 0x80 ? 0x84 - s : s - 0x84) / 32768; }
  static cod(x) {
    let s = Math.max(-32635, Math.min(32635, Math.round(x * 32767))), sinal = 0;
    if (s < 0) { s = -s; sinal = 0x80; }
    s += 0x84;
    let exp = 7;
    for (let m = 0x4000; exp > 0 && !(s & m); m >>= 1) exp--;
    return ~(sinal | (exp << 4) | ((s >> (exp + 3)) & 0x0F)) & 0xFF;
  }
  receber(quem, dados) {
    const a = ctxAudio();
    if (!a || !this.ouvindo || config.volumeVoz <= 0) return;
    const buf = a.createBuffer(1, dados.length, 8000);
    const ch = buf.getChannelData(0);
    for (let i = 0; i < dados.length; i++) ch[i] = ChatDeVoz.dec(dados[i]) * config.volumeVoz;
    const src = a.createBufferSource();
    src.buffer = buf;
    src.connect(a.destination);
    const quando = Math.max(this.prox[quem] || 0, a.currentTime + 0.06);  // folga contra a internet oscilando
    src.start(quando);
    this.prox[quem] = quando + buf.duration;
  }
  async falar(on) {
    this.falando = on;
    if (!on || this.mic) return;
    try {
      const a = ctxAudio();
      const stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true } });
      const fonte = a.createMediaStreamSource(stream);
      const proc = a.createScriptProcessor(2048, 1, 1);
      const passo = a.sampleRate / 8000;
      let pos = 0, n = 0;
      const quadro = new Uint8Array(160);
      proc.onaudioprocess = (ev) => {
        if (!this.falando) return;
        const x = ev.inputBuffer.getChannelData(0);
        for (; pos < x.length; pos += passo) {
          quadro[n++] = ChatDeVoz.cod(x[Math.floor(pos)]);
          if (n === 160) { this.enviar(quadro.slice(0)); n = 0; }
        }
        pos -= x.length;
      };
      fonte.connect(proc);
      const mudo = a.createGain();
      mudo.gain.value = 0;
      proc.connect(mudo).connect(a.destination);
      this.mic = { stream, proc, fonte };
    } catch (e) { aviso('Microfone bloqueado: libere o microfone para falar'); }
  }
  desligar() {
    this.falando = false;
    if (this.mic) { this.mic.stream.getTracks().forEach((t) => t.stop()); this.mic.proc.disconnect(); this.mic = null; }
  }
}
