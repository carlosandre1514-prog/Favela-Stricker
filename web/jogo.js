// Favela Stricker — versão site. As mesmas telas e regras do app Android (app/.../JogoActivity.kt):
// login, menu, lado, skins, loja, multiplayer (salas com código), partida em primeira pessoa com HUD,
// partida ao vivo pelo servidor (WebSocket) e chat de voz do time. O motor é o mesmo, compilado para WebAssembly.
'use strict';
(() => {
  const CFG = window.CONFIG_JOGO || {};
  const SERVIDOR = (CFG.servidor || location.origin).replace(/\/$/, '');

  // ---------------- utilidades ----------------
  const $ = (s, e = document) => e.querySelector(s);
  const $$ = (s, e = document) => [...e.querySelectorAll(s)];
  const ui = $('#ui');
  const html = (s) => { const t = document.createElement('template'); t.innerHTML = s.trim(); return t.content.firstElementChild; };
  const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const fmt = (n) => Number(n || 0).toLocaleString('pt-BR');
  const dorme = (ms) => new Promise((r) => setTimeout(r, ms));
  const toque = matchMedia('(pointer: coarse)').matches || 'ontouchstart' in window;
  if (toque) document.body.classList.add('toque');

  // Preferências e progresso no navegador (o que importa também vai para a conta no Firebase)
  const guardado = {
    get(k, d) { try { const v = localStorage.getItem('fs_' + k); return v === null ? d : JSON.parse(v); } catch (_) { return d; } },
    set(k, v) { try { localStorage.setItem('fs_' + k, JSON.stringify(v)); } catch (_) {} },
    del(k) { try { localStorage.removeItem('fs_' + k); } catch (_) {} },
  };
  function aviso(msg) {
    const t = html(`<div style="position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:rgba(17,24,39,.95);padding:10px 16px;border-radius:12px;font-weight:700;z-index:20;pointer-events:none">${esc(msg)}</div>`);
    document.body.appendChild(t);
    setTimeout(() => t.remove(), 2600);
  }

  // ---------------- catálogo (preços provisórios) ----------------
  // Ordem das skins = ordem da lista do motor (Game.cpp kSkins): o índice vai para o motor
  const SKINS = [
    { key: 'pm', nome: 'PM', time: 0, i: 0, preco: 0 }, { key: 'civil', nome: 'Polícia Civil', time: 0, i: 1, preco: 500 },
    { key: 'exercito', nome: 'Exército', time: 0, i: 2, preco: 700 }, { key: 'choque', nome: 'Choque', time: 0, i: 3, preco: 1000 },
    { key: 'especial', nome: 'Especial', time: 0, i: 4, preco: 1500 },
    { key: 'rubro', nome: 'Rubro', time: 1, i: 0, preco: 0 }, { key: 'alvinegro', nome: 'Alvinegro', time: 1, i: 1, preco: 0 },
    { key: 'tricolor', nome: 'Tricolor', time: 1, i: 2, preco: 0 }, { key: 'canarinho', nome: 'Canarinho', time: 1, i: 3, preco: 0 },
    { key: 'faixa', nome: 'Faixa', time: 1, i: 4, preco: 0 },
  ];
  const ARMAS = [
    { key: 'm4a1', nome: 'M4A1', time: 0, preco: 0, info: '24 de dano · 60 na cabeça · 10 tiros/s · pente 30' },
    { key: 'm4colt', nome: 'M4 Colt', time: 0, preco: 1500, info: 'Com luneta · 26 de dano · 65 na cabeça · 9 tiros/s · pente 30' },
    { key: 'ak47', nome: 'AK-47', time: 1, preco: 0, info: '30 de dano · 75 na cabeça · 8 tiros/s · pente 30' },
    { key: 'ak74', nome: 'AK-74', time: 1, preco: 1400, info: '27 de dano · 68 na cabeça · 9 tiros/s · pente 30' },
    { key: 'ak74_tatica', nome: 'AK-74 tática', time: 1, preco: 1800, info: '28 de dano · 70 na cabeça · 9,5 tiros/s · pente 30' },
    { key: 'tt33', nome: 'TT-33', time: -1, preco: 0, info: 'Pistola · 25 de dano · 62 na cabeça · 3 tiros/s · pente 8' },
  ];
  const SKINS_ARMA = [
    { arma: 'tt33', key: 'crimson_forge', nome: 'TT-33 Crimson Forge', preco: 800 },
    { arma: 'tt33', key: 'nebula_shadow', nome: 'TT-33 Nebula Shadow', preco: 1000 },
    { arma: 'tt33', key: 'gold_relic', nome: 'TT-33 Gold Relic', preco: 1500 },
  ];

  // ---------------- progresso (moedas e itens) ----------------
  const conta = {
    get moedas() { return guardado.get('moedas', 1000); },
    set moedas(v) { guardado.set('moedas', v); salvarConta(); },
    itens() { return new Set(guardado.get('itens', [])); },
    tem(id, preco) { return preco === 0 || this.itens().has(id); },
    ganhar(id) { const s = this.itens(); s.add(id); guardado.set('itens', [...s]); salvarConta(); },
    temSkin(s) { return this.tem('skin:' + s.key, s.preco); },
    temArma(a) { return this.tem('arma:' + a.key, a.preco); },
    nome() { return guardado.get('nome', 'Jogador'); },
    skinArma(arma) { return guardado.get('skin_arma_' + arma, ''); },
  };

  // ---------------- Firebase (API REST + streaming) ----------------
  class Firebase {
    constructor(apiKey, dbUrl) { this.key = apiKey; this.db = dbUrl.replace(/\/$/, ''); this.uid = ''; this.email = ''; this.token = null; this.refresh = null; this.expira = 0; this.anonimo = false; }
    static erro(m) {
      m = String(m || '');
      if (m.startsWith('EMAIL_EXISTS')) return 'Esse e-mail já tem conta. Use Entrar.';
      if (/INVALID_LOGIN_CREDENTIALS|INVALID_PASSWORD|EMAIL_NOT_FOUND/.test(m)) return 'E-mail ou senha errados';
      if (m.startsWith('WEAK_PASSWORD')) return 'Senha fraca: use pelo menos 6 caracteres';
      if (m.startsWith('INVALID_EMAIL')) return 'E-mail inválido';
      if (m.startsWith('MISSING_PASSWORD')) return 'Digite a senha';
      if (m.startsWith('TOO_MANY_ATTEMPTS')) return 'Muitas tentativas. Espere um pouco.';
      if (/OPERATION_NOT_ALLOWED|ADMIN_ONLY/.test(m)) return 'Esse tipo de login está desligado no Firebase (Authentication → Sign-in method)';
      return m || 'erro';
    }
    pegar(j) {
      this.token = j.idToken || j.id_token; this.refresh = j.refreshToken || j.refresh_token || this.refresh;
      this.uid = j.localId || j.user_id || this.uid; this.email = j.email || this.email;
      this.expira = Date.now() + (Number(j.expiresIn || j.expires_in || 3600) * 1000) - 300000;
    }
    async auth(metodo, corpo) {
      try {
        const r = await fetch(`https://identitytoolkit.googleapis.com/v1/accounts:${metodo}?key=${this.key}`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(corpo) });
        const j = await r.json();
        if (!r.ok) return Firebase.erro(j.error && j.error.message);
        if (metodo !== 'sendOobCode') this.pegar(j);
        return null;
      } catch (e) { return 'Sem internet (' + e.message + ')'; }
    }
    criar(email, senha) { this.anonimo = false; return this.auth('signUp', { email, password: senha, returnSecureToken: true }); }
    entrar(email, senha) { this.anonimo = false; return this.auth('signInWithPassword', { email, password: senha, returnSecureToken: true }); }
    async convidado() { const e = await this.auth('signUp', { returnSecureToken: true }); this.anonimo = !e; return e; }
    recuperar(email) { return this.auth('sendOobCode', { requestType: 'PASSWORD_RESET', email }); }
    async restaurar(refresh) {
      try {
        const r = await fetch(`https://securetoken.googleapis.com/v1/token?key=${this.key}`, { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'grant_type=refresh_token&refresh_token=' + encodeURIComponent(refresh) });
        if (!r.ok) return false;
        this.refresh = refresh; this.pegar(await r.json()); return true;
      } catch (_) { return false; }
    }
    async tokenValido() { if (this.refresh && Date.now() > this.expira) await this.restaurar(this.refresh); return this.token; }
    async url(caminho) { const t = await this.tokenValido(); return `${this.db}/${caminho}.json` + (t ? '?auth=' + encodeURIComponent(t) : ''); }
    async pedir(metodo, caminho, corpo) {
      const r = await fetch(await this.url(caminho), { method: metodo, headers: corpo !== undefined ? { 'Content-Type': 'application/json' } : {}, body: corpo !== undefined ? JSON.stringify(corpo) : undefined });
      const texto = await r.text();
      let dado = null;
      try { dado = JSON.parse(texto); } catch (_) {}
      return { ok: r.ok, code: r.status, dado };
    }
    get(c) { return this.pedir('GET', c); }
    put(c, v) { return this.pedir('PUT', c, v); }
    patch(c, v) { return this.pedir('PATCH', c, v); }
    post(c, v) { return this.pedir('POST', c, v); }
    del(c) { return this.pedir('DELETE', c); }
    // Acompanha um caminho ao vivo (EventSource). cb(evento, subcaminho, dado). Devolve {fechar()}
    ouvir(caminho, cb) {
      let fonte = null, fechado = false;
      const abrir = async () => {
        if (fechado) return;
        fonte = new EventSource(await this.url(caminho));
        const trata = (tipo) => (ev) => { try { const j = JSON.parse(ev.data); cb(tipo, j.path || '/', j.data); } catch (_) {} };
        fonte.addEventListener('put', trata('put'));
        fonte.addEventListener('patch', trata('patch'));
        fonte.addEventListener('auth_revoked', () => { fonte.close(); setTimeout(abrir, 500); });
        fonte.addEventListener('cancel', () => { fonte.close(); setTimeout(abrir, 1500); });
      };
      abrir();
      return { fechar() { fechado = true; if (fonte) fonte.close(); } };
    }
  }

  let fb = null;
  const novoFirebase = () => (CFG.firebase && CFG.firebase.apiKey ? new Firebase(CFG.firebase.apiKey, CFG.firebase.databaseURL) : null);

  function contaJson() {
    const itens = {};
    for (const i of conta.itens()) itens[i.replace(/:/g, '|')] = true;  // ':' não pode em chave do Firebase
    return { nome: conta.nome(), moedas: conta.moedas, itens, atualizado: { '.sv': 'timestamp' } };
  }
  let salvando = null;
  function salvarConta() {
    if (!fb || !fb.token) return;
    clearTimeout(salvando);
    salvando = setTimeout(() => fb.put('usuarios/' + fb.uid, contaJson()).catch(() => {}), 400);
  }
  async function carregarConta() {
    if (!fb) return;
    try {
      const r = await fb.get('usuarios/' + fb.uid);
      if (!r.ok) return;
      if (!r.dado) { await fb.put('usuarios/' + fb.uid, contaJson()); return; }
      guardado.set('moedas', r.dado.moedas ?? conta.moedas);
      guardado.set('itens', Object.keys(r.dado.itens || {}).map((k) => k.replace(/\|/g, ':')));
      if (r.dado.nome) guardado.set('nome', r.dado.nome);
    } catch (_) {}
  }

  // ---------------- sala do multiplayer (Firebase) ----------------
  const LETRAS = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';
  class Sala {
    constructor(codigo, host) { this.codigo = codigo; this.host = host; this.jogadores = {}; this.estado = 'espera'; this.fechada = false; this.ouvintes = []; }
    static novoCodigo() { let s = ''; for (let i = 0; i < 6; i++) s += LETRAS[Math.floor(Math.random() * LETRAS.length)]; return s; }
    static async criar(eu) {
      for (let k = 0; k < 6; k++) {
        const c = Sala.novoCodigo();
        const g = await fb.get('salas/' + c);
        if (!g.ok) return [null, `o Firebase recusou (${g.code}). Veja as regras do banco.`];
        if (g.dado !== null) continue;
        const r = await fb.put('salas/' + c, { host: fb.uid, estado: 'espera', criada: { '.sv': 'timestamp' }, jogadores: { [fb.uid]: eu } });
        if (!r.ok) return [null, `não consegui criar a sala (${r.code})`];
        return [new Sala(c, true), ''];
      }
      return [null, 'não achei um código livre, tente de novo'];
    }
    static async entrar(codigo, eu) {
      const c = codigo.trim().toUpperCase();
      const g = await fb.get('salas/' + c);
      if (!g.ok) return [null, `o Firebase recusou (${g.code})`];
      if (!g.dado) return [null, 'não existe sala com o código ' + c];
      if (g.dado.estado !== 'espera') return [null, 'essa sala já está jogando'];
      const js = g.dado.jogadores || {};
      if (Object.keys(js).length >= 10 && !js[fb.uid]) return [null, 'a sala está cheia (10 pessoas)'];
      const r = await fb.put(`salas/${c}/jogadores/${fb.uid}`, eu);
      if (!r.ok) return [null, `não consegui entrar (${r.code})`];
      return [new Sala(c, g.dado.host === fb.uid), ''];
    }
    ouvir(aoMudar, aoEstado, aoFechar, aoSair) {
      this.ouvintes.push(fb.ouvir(`salas/${this.codigo}/jogadores`, (ev, sub, dado) => {
        if (sub === '/') {
          if (ev === 'put') this.jogadores = {};
          for (const [k, v] of Object.entries(dado || {})) { if (v && typeof v === 'object') this.jogadores[k] = Object.assign(ev === 'patch' ? (this.jogadores[k] || {}) : {}, v); else delete this.jogadores[k]; }
        } else {
          const [id, campo] = sub.replace(/^\//, '').split('/');
          if (campo) { if (this.jogadores[id]) this.jogadores[id][campo] = dado; }
          else if (dado) this.jogadores[id] = ev === 'patch' ? Object.assign(this.jogadores[id] || {}, dado) : dado;
          else { delete this.jogadores[id]; if (this.estado === 'jogo') aoSair(id); }
        }
        aoMudar();
      }));
      this.ouvintes.push(fb.ouvir(`salas/${this.codigo}/estado`, (_ev, _sub, dado) => {
        if (dado === null) { this.fechada = true; aoFechar(); return; }
        if (dado !== this.estado) { this.estado = dado; aoEstado(dado); }
      }));
    }
    lista() { return Object.entries(this.jogadores).map(([id, p]) => ({ id, ...p })); }
    roster() { return JSON.stringify(this.lista().map((p) => ({ id: p.id, nome: p.nome || 'Jogador', time: p.time | 0, skin: p.skin | 0, arma: p.arma || '' }))); }
    atualizar(eu) { return fb.patch(`salas/${this.codigo}/jogadores/${fb.uid}`, eu); }
    async comecar() { await fb.del(`salas/${this.codigo}/est`); await fb.del(`salas/${this.codigo}/ev`); return (await fb.patch('salas/' + this.codigo, { estado: 'jogo', inicio: { '.sv': 'timestamp' } })).ok; }
    async voltar() { if (!this.host) return; await fb.del(`salas/${this.codigo}/est`); await fb.del(`salas/${this.codigo}/ev`); await fb.patch('salas/' + this.codigo, { estado: 'espera' }); }
    async sair() { this.parar(); if (this.host) await fb.del('salas/' + this.codigo); else await fb.del(`salas/${this.codigo}/jogadores/${fb.uid}`); this.fechada = true; }
    parar() { for (const o of this.ouvintes) o.fechar(); this.ouvintes = []; }
  }

  // ---------------- motor (WebAssembly) ----------------
  let M = null;
  const motor = {
    c(nome, ret, tipos, args) { if (!M) return ret === 'string' ? '' : 0; return M.ccall(nome, ret, tipos, args); },
    abrir: (json) => motor.c('web_abrir_projeto', 'number', ['string'], [json]),
    hud() { try { return JSON.parse(motor.c('web_hud', 'string', [], [])); } catch (_) { return {}; } },
    sons: () => motor.c('web_sons', 'string', [], []),
    camera: (tx, ty, tz, y, p, d) => motor.c('web_camera', null, ['number', 'number', 'number', 'number', 'number', 'number'], [tx, ty, tz, y, p, d]),
    olhar: (dx, dy) => motor.c('web_olhar', null, ['number', 'number'], [dx, dy]),
    play: (on) => motor.c('web_play', null, ['number'], [on ? 1 : 0]),
    fps: (on) => motor.c('web_primeira_pessoa', null, ['number'], [on ? 1 : 0]),
    escolhas: (t, s, a, b) => motor.c('web_escolhas', null, ['number', 'number', 'string', 'string'], [t, s, a, b]),
    skinArma: (a, s) => motor.c('web_skin_arma', null, ['string', 'string'], [a, s]),
    andar: (x, y, run) => motor.c('web_andar', null, ['number', 'number', 'number'], [x, y, run ? 1 : 0]),
    pular: () => motor.c('web_pular', null, [], []),
    agachar: (on) => motor.c('web_agachar', null, ['number'], [on ? 1 : 0]),
    atirar: () => motor.c('web_atirar', null, [], []),
    mirar: (on) => motor.c('web_mirar', null, ['number'], [on ? 1 : 0]),
    trocar: () => motor.c('web_trocar_arma', 'number', [], []),
    granada: () => motor.c('web_granada', 'number', [], []),
    recarregar: () => motor.c('web_recarregar', null, [], []),
    interagir: () => motor.c('web_interagir', 'number', [], []),
    redeSala: (host, id, roster) => motor.c('web_rede_sala', null, ['number', 'string', 'string'], [host ? 1 : 0, id, roster]),
    redeColetar: () => motor.c('web_rede_coletar', 'string', [], []),
    redeAplicar: (s) => motor.c('web_rede_aplicar', null, ['string'], [s]),
    redeEventos: () => motor.c('web_rede_eventos', 'string', [], []),
    redeEvento: (s) => motor.c('web_rede_evento', null, ['string'], [s]),
  };
  const visaoGeral = () => motor.camera(0, 8, 20, 3.6, 0.42, 115);
  let menuGira = true;

  // ---------------- sons do jogo (tiros, recarga, explosão...) ----------------
  let audio = null;
  const buffers = {};
  function ctxAudio() {
    if (!audio) { try { audio = new (window.AudioContext || window.webkitAudioContext)(); } catch (_) {} }
    if (audio && audio.state === 'suspended') audio.resume();
    return audio;
  }
  function tocar(nome, vol, pan) {
    const a = audio;
    if (!a) return;
    const usar = (b) => {
      const src = a.createBufferSource(); src.buffer = b;
      if (nome.startsWith('tiro')) src.playbackRate.value = 0.94 + Math.random() * 0.12;
      const g = a.createGain(); g.gain.value = Math.min(1, vol);
      const p = a.createStereoPanner ? a.createStereoPanner() : null;
      if (p) { p.pan.value = Math.max(-1, Math.min(1, pan)); src.connect(g).connect(p).connect(a.destination); } else src.connect(g).connect(a.destination);
      src.start();
    };
    const b = buffers[nome];
    if (b === undefined) {
      buffers[nome] = null;
      fetch('sons/' + nome + '.wav').then((r) => (r.ok ? r.arrayBuffer() : Promise.reject())).then((d) => a.decodeAudioData(d))
        .then((dec) => { buffers[nome] = dec; usar(dec); })
        .catch(() => { if (nome.endsWith('_longe')) { buffers[nome] = buffers[nome.replace('_longe', '')] || null; } });
    } else if (b) usar(b);
    else if (nome.endsWith('_longe') && buffers[nome.replace('_longe', '')]) usar(buffers[nome.replace('_longe', '')]);
  }

  // ---------------- telas ----------------
  function mostrar(no, tag) { ui.innerHTML = ''; no.dataset.tela = tag; ui.appendChild(no); return no; }
  const telaAtual = () => (ui.firstElementChild ? ui.firstElementChild.dataset.tela : '');
  const chipMoedas = () => `<span class="chip">● ${fmt(conta.moedas)} moedas</span>`;
  function cabecalho(titulo, voltar) {
    const h = html(`<div class="cabecalho"><button class="b-fraco" style="width:44px;padding:0">‹</button><span class="titulo">${esc(titulo)}</span><span class="cresce"></span>${chipMoedas()}</div>`);
    h.querySelector('button').onclick = voltar;
    return h;
  }

  function telaLogin() {
    menuGira = true;
    let criando = false;
    const t = mostrar(html(`<div class="tela transparente" style="flex-direction:row;align-items:center;justify-content:space-between;gap:20px">
      <div class="coluna"><span class="titulo ouro" style="font-size:52px">FAVELA</span><span class="titulo azul" style="font-size:52px">STRICKER</span>
      <span class="apagado" style="font-size:12px;letter-spacing:.12em;font-weight:700">POLÍCIA x BANDIDOS · PRIMEIRA PESSOA</span></div>
      <div class="cartao coluna" style="width:min(360px,46vw)">
        <span class="titulo" id="lt" style="font-size:28px">ENTRAR</span>
        <input id="lnome" placeholder="Seu nome no jogo" maxlength="16" style="display:none">
        <input id="lemail" placeholder="E-mail" type="email" autocomplete="email">
        <input id="lsenha" placeholder="Senha (6 ou mais)" type="password" autocomplete="current-password">
        <button class="b-ouro" id="lgo">ENTRAR</button>
        <div class="linha"><button class="b-link cresce" id="ltroca">Não tem conta? Criar conta</button><button class="b-link apagado" id="lesq">Esqueci a senha</button></div>
        <button class="b-fraco" id="lconv">Jogar como convidado</button>
        <div class="status" id="lst"></div>
      </div></div>`), 'login');
    $('#lnome', t).value = guardado.get('nome', '');
    $('#lemail', t).value = guardado.get('email', '');
    const st = $('#lst', t);
    const modo = (c) => {
      criando = c;
      $('#lt', t).textContent = c ? 'CRIAR CONTA' : 'ENTRAR';
      $('#lnome', t).style.display = c ? '' : 'none';
      $('#lgo', t).textContent = c ? 'CRIAR CONTA' : 'ENTRAR';
      $('#ltroca', t).textContent = c ? 'Já tem conta? Entrar' : 'Não tem conta? Criar conta';
    };
    const rodar = async (msg, trabalho) => {
      const f = novoFirebase();
      if (!f) { st.textContent = 'Configuração do jogo faltando'; return; }
      st.textContent = msg;
      $('#lgo', t).disabled = true;
      const e = await trabalho(f);
      $('#lgo', t).disabled = false;
      if (e) { st.textContent = e; return; }
      fb = f;
      guardado.set('login_refresh', f.refresh || '');
      await carregarConta();
      telaInicio();
    };
    $('#lgo', t).onclick = () => {
      const em = $('#lemail', t).value.trim(), pw = $('#lsenha', t).value;
      guardado.set('email', em);
      if (criando) {
        const nm = $('#lnome', t).value.trim().slice(0, 16);
        if (!nm) { st.textContent = 'Escolha um nome para o jogo'; return; }
        guardado.set('nome', nm);
        rodar('Criando a conta…', (f) => f.criar(em, pw));
      } else rodar('Entrando…', (f) => f.entrar(em, pw));
    };
    $('#lsenha', t).onkeydown = (e) => { if (e.key === 'Enter') $('#lgo', t).click(); };
    $('#ltroca', t).onclick = () => modo(!criando);
    $('#lesq', t).onclick = async () => {
      const em = $('#lemail', t).value.trim();
      if (!em) { st.textContent = 'Digite o e-mail primeiro'; return; }
      const f = novoFirebase(); if (!f) return;
      st.textContent = (await f.recuperar(em)) || 'Enviamos um e-mail para trocar a senha';
    };
    $('#lconv', t).onclick = () => rodar('Entrando como convidado…', async (f) => { await f.convidado(); if (!f.uid) f.uid = guardado.get('uid_local', null) || (guardado.set('uid_local', 'web' + Math.random().toString(36).slice(2, 12)), guardado.get('uid_local')); return null; });
  }

  async function comecarLogin() {
    const salvo = guardado.get('login_refresh', '');
    const f = novoFirebase();
    if (!salvo || !f) { telaLogin(); return; }
    mostrar(html('<div class="tela transparente" style="align-items:center;justify-content:center;font-size:16px">Entrando…</div>'), 'entrando');
    if (await f.restaurar(salvo)) { fb = f; await carregarConta(); telaInicio(); } else telaLogin();
  }

  function telaConta() {
    const who = !fb ? 'Sem conta (só neste navegador)' : fb.anonimo ? 'Convidado' : fb.email;
    if (confirm(`${conta.nome()}\nConta: ${who}\nMoedas: ${fmt(conta.moedas)}\n\nSair da conta?`)) {
      sairDaSala();
      fb = null; guardado.del('login_refresh');
      telaLogin();
    }
  }

  function telaInicio() {
    menuGira = true;
    const destaque = SKINS.filter((s) => !conta.temSkin(s)).sort((a, b) => b.preco - a.preco)[0];
    const t = mostrar(html(`<div class="tela transparente" style="flex-direction:row;gap:16px">
      <div class="coluna" style="width:290px">
        <span class="titulo azul" style="font-size:54px">POLÍCIA</span>
        <div class="linha"><span class="titulo" style="font-size:30px">x</span><span class="titulo ouro" style="font-size:54px">BANDIDOS</span></div>
        <span class="apagado" style="font-size:12px;letter-spacing:.12em;font-weight:700">FAVELA · 5 CONTRA 5 · PRIMEIRA PESSOA</span>
        <span class="cresce"></span>
        <button class="b-ouro titulo" id="jogar" style="font-size:26px;min-height:58px">▶ JOGAR</button>
        <div class="linha"><button class="b-fraco cresce" id="loja">Loja</button><button class="b-fraco cresce" id="graf">Gráficos</button></div>
        <button class="b-fraco" id="multi">👥 MULTIPLAYER (criar sala / entrar com código)</button>
      </div>
      <span class="cresce"></span>
      <div class="coluna" style="align-items:flex-end;justify-content:space-between">
        <div class="linha"><button class="b-fraco" id="contab">👤 ${esc(conta.nome())}</button>${chipMoedas()}</div>
        ${destaque ? `<div class="cartao coluna" id="dest" style="cursor:pointer;align-items:center;width:200px"><span class="ouro" style="font-size:11px;font-weight:800;letter-spacing:.1em">DESTAQUE DA LOJA</span>
          <img src="retratos/${destaque.key}.png" style="height:170px;object-fit:contain"><span class="titulo" style="font-size:22px">${esc(destaque.nome.toUpperCase())}</span>
          <span class="amarelo" style="font-weight:800">${fmt(destaque.preco)} moedas</span></div>` : ''}
      </div></div>`), 'inicio');
    $('#jogar', t).onclick = telaLado;
    $('#loja', t).onclick = () => telaLoja(0);
    $('#graf', t).onclick = escolherQualidade;
    $('#multi', t).onclick = telaMultiplayer;
    $('#contab', t).onclick = telaConta;
    if (destaque) $('#dest', t).onclick = () => telaLoja(destaque.time === 0 ? 2 : 3);
  }

  function escolherQualidade() {
    const atual = guardado.get('qualidade', toque ? 0 : 1);
    const q = prompt('Gráficos: 0 = leve (celular), 1 = equilibrado, 2 = alto', String(atual));
    if (q === null) return;
    const n = Math.max(0, Math.min(2, parseInt(q, 10) || 0));
    guardado.set('qualidade', n);
    motor.c('web_qualidade', null, ['number'], [n]);
    ajustarTela();
  }

  function telaLado() {
    menuGira = true;
    const cartao = (time) => `<div class="cartao coluna cresce" data-time="${time}" style="cursor:pointer;background:${time ? '#2A1C08' : '#0F1D36'};border:2px solid ${time ? '#F59E0B' : '#3B82F6'}">
      <span class="titulo" style="font-size:42px;color:${time ? '#FCD34D' : '#93C5FD'}">${time ? 'BANDIDOS' : 'POLÍCIA'}</span>
      <span style="font-size:13px">${time ? 'Nasce na quadra, dentro da favela. Com polícia na quadra, nasce num canto do morro sem policial.' : 'Nasce fora da favela, na rua dos prédios. Sobe as escadarias para tomar o morro.'}</span>
      <span class="cresce"></span><span class="apagado" style="font-size:11px">${time ? 'AK-47 · AK-74 · AK-74 tática' : 'M4A1 · M4 Colt (com luneta)'}</span>
      <div class="linha"><span class="cresce apagado" style="font-size:12px">4 robôs + você</span><button class="${time ? 'b-laranja' : 'b-azul'}">ENTRAR</button></div></div>`;
    const t = mostrar(html(`<div class="tela"><div class="linha cresce" style="gap:16px;align-items:stretch">${cartao(0)}${cartao(1)}</div></div>`), 'lado');
    t.prepend(cabecalho('ESCOLHA SEU LADO', telaInicio));
    for (const c of $$('[data-time]', t)) c.onclick = () => telaSkins(Number(c.dataset.time));
  }

  // Skins do lado + arma principal + segunda arma. pronto != null: veio da sala do multiplayer
  function telaSkins(time, pronto) {
    menuGira = true;
    const minhas = SKINS.filter((s) => s.time === time);
    let sel = minhas.find((s) => s.key === guardado.get('skin_' + time, '') && conta.temSkin(s)) || minhas.find((s) => conta.temSkin(s));
    const prim = ARMAS.filter((a) => a.time === time || a.time === -1);
    let arma = prim.find((a) => a.key === guardado.get('arma_' + time, '') && conta.temArma(a)) || prim.find((a) => conta.temArma(a));
    let segunda = prim.find((a) => a.key === guardado.get('arma2_' + time, 'tt33') && conta.temArma(a) && a !== arma) || null;
    const t = mostrar(html(`<div class="tela"><div class="grade-skins" id="gs"></div>
      <div class="linha" style="margin-top:8px"><span class="apagado" style="font-weight:700;font-size:13px">Arma principal:</span><div class="chips cresce" id="c1"></div>
        <button class="b-ouro titulo" id="comecar" style="font-size:19px;width:170px">${pronto ? 'PRONTO ✓' : 'COMEÇAR ▶'}</button></div>
      <div class="linha" style="margin-top:6px"><span class="apagado" style="font-weight:700;font-size:13px">Segunda arma (troca rápida):</span><div class="chips cresce" id="c2"></div></div></div>`), 'skins');
    t.prepend(cabecalho(time ? 'SUA ROUPA · BANDIDOS' : 'SUA FARDA · POLÍCIA', () => (pronto ? telaSala() : telaLado())));
    const desenhar = () => {
      $('#gs', t).innerHTML = minhas.map((s) => `<div class="skin ${s === sel ? 'sel' : ''} ${conta.temSkin(s) ? '' : 'travada'}" data-k="${s.key}">
        <img src="retratos/${s.key}.png" alt=""><b style="font-size:13px">${esc(s.nome)}</b>
        <span style="font-size:11px;font-weight:800" class="${conta.temSkin(s) ? 'verde' : 'amarelo'}">${conta.temSkin(s) ? (s.preco ? 'sua' : 'grátis') : '🔒 ' + fmt(s.preco) + ' moedas'}</span></div>`).join('');
      for (const d of $$('.skin', t)) d.onclick = () => {
        const s = minhas.find((x) => x.key === d.dataset.k);
        if (conta.temSkin(s)) { sel = s; desenhar(); } else comprar('a skin ' + s.nome, s.preco, 'skin:' + s.key, () => telaSkins(time, pronto));
      };
      if (segunda === arma) segunda = null;
      $('#c1', t).innerHTML = prim.map((a) => `<button data-k="${a.key}" class="${a === arma ? 'sel' : ''}">${conta.temArma(a) ? '' : '🔒 '}${esc(a.nome)}</button>`).join('');
      for (const b of $$('#c1 button', t)) b.onclick = () => {
        const a = prim.find((x) => x.key === b.dataset.k);
        if (conta.temArma(a)) { arma = a; desenhar(); } else comprar('a ' + a.nome, a.preco, 'arma:' + a.key, () => telaSkins(time, pronto));
      };
      const ops = [null, ...prim.filter((a) => conta.temArma(a) && a !== arma)];
      $('#c2', t).innerHTML = ops.map((a) => `<button data-k="${a ? a.key : ''}" class="${a === segunda ? 'sel' : ''}">${a ? esc(a.nome) : 'nenhuma'}</button>`).join('');
      for (const b of $$('#c2 button', t)) b.onclick = () => { segunda = prim.find((x) => x.key === b.dataset.k) || null; desenhar(); };
    };
    desenhar();
    $('#comecar', t).onclick = () => {
      guardado.set('skin_' + time, sel.key); guardado.set('arma_' + time, arma.key); guardado.set('arma2_' + time, segunda ? segunda.key : '');
      guardado.set('lado', time);
      if (pronto) pronto(); else iniciarPartida(time, sel, arma, segunda, null);
    };
  }

  function comprar(oque, preco, id, depois) {
    const tem = conta.moedas;
    if (tem >= preco) {
      if (confirm(`Comprar ${oque}?\nCusta ${fmt(preco)} moedas. Você tem ${fmt(tem)}.`)) { conta.moedas = tem - preco; conta.ganhar(id); aviso('Comprado!'); depois(); }
    } else alert(`Comprar ${oque}?\nCusta ${fmt(preco)} moedas e você tem ${fmt(tem)}. Faltam ${fmt(preco - tem)}: ganhe jogando (abates e vitórias dão moedas).`);
  }

  function telaLoja(aba) {
    menuGira = true;
    const abas = ['Armas', 'Skins de arma', 'Skins Polícia', 'Skins Bandidos'];
    let itens;
    if (aba === 0) itens = ARMAS.map((a) => ({ id: 'arma:' + a.key, nome: a.nome, preco: a.preco, img: `armas/${a.key}.png`, info: (a.time === 0 ? 'Polícia · ' : a.time === 1 ? 'Bandidos · ' : 'Os dois lados · ') + a.info, tem: conta.temArma(a) }));
    else if (aba === 1) itens = SKINS_ARMA.map((g) => ({ id: `skin_arma:${g.arma}:${g.key}`, nome: g.nome, preco: g.preco, img: `armas/${g.arma}_${g.key}.png`, info: 'Skin de arma · troca só a pintura', tem: conta.tem(`skin_arma:${g.arma}:${g.key}`, g.preco), usando: conta.skinArma(g.arma) === g.key, arma: g.arma, key: g.key }));
    else itens = SKINS.filter((s) => s.time === aba - 2).map((s) => ({ id: 'skin:' + s.key, nome: s.nome, preco: s.preco, img: `retratos/${s.key}.png`, info: s.time ? 'Skin dos bandidos' : 'Skin da polícia', tem: conta.temSkin(s) }));
    const t = mostrar(html(`<div class="tela"><div class="linha" style="margin-bottom:6px">${abas.map((a, i) => `<button class="${i === aba ? 'b-ouro' : 'b-fraco'}" data-aba="${i}" style="font-size:13px">${a}</button>`).join('')}<span class="apagado" style="font-size:11px">Preços provisórios</span></div>
      <div class="linha cresce" style="align-items:stretch;gap:14px"><div class="lista-loja" id="lista"></div><div class="cartao coluna cresce" id="det"></div></div></div>`), 'loja');
    t.prepend(cabecalho('LOJA', telaInicio));
    for (const b of $$('[data-aba]', t)) b.onclick = () => telaLoja(Number(b.dataset.aba));
    const escolher = (it) => {
      for (const x of $$('.item-loja', t)) x.classList.toggle('sel', x.dataset.id === it.id);
      const pode = !it.tem && conta.moedas >= it.preco;
      $('#det', t).innerHTML = `<div class="linha"><span class="titulo cresce" style="font-size:30px">${esc(it.nome.toUpperCase())}</span><span class="amarelo" style="font-weight:800">${it.preco ? fmt(it.preco) + ' moedas' : 'grátis'}</span></div>
        <div class="cresce" style="background:#1B2232;border-radius:12px;display:flex;align-items:center;justify-content:center;min-height:120px"><img src="${it.img}" style="max-width:90%;max-height:100%;object-fit:contain"></div>
        <span class="apagado" style="font-size:12px">${esc(it.info)}</span><button id="acao"></button>`;
      const b = $('#acao', t);
      if (it.tem && it.arma) {
        b.className = it.usando ? 'b-fraco' : 'b-ouro';
        b.textContent = it.usando ? 'USANDO (clique para tirar)' : 'USAR ESTA SKIN';
        b.onclick = () => { guardado.set('skin_arma_' + it.arma, it.usando ? '' : it.key); telaLoja(1); };
      } else {
        b.className = it.tem ? 'b-fraco' : pode ? 'b-ouro' : 'b-fraco';
        b.textContent = it.tem ? 'JÁ É SUA' : pode ? 'COMPRAR POR ' + fmt(it.preco) : 'FALTAM ' + fmt(it.preco - conta.moedas) + ' MOEDAS';
        b.onclick = () => { if (pode) { conta.moedas -= it.preco; conta.ganhar(it.id); aviso('Comprado: ' + it.nome); telaLoja(aba); } };
      }
    };
    $('#lista', t).innerHTML = itens.map((it) => `<div class="item-loja" data-id="${it.id}"><img src="${it.img}" alt=""><span class="cresce">${esc(it.nome)}</span>
      <span style="font-size:12px" class="${it.tem ? 'azul' : 'verde'}">${it.usando ? 'usando' : it.tem ? '✓ sua' : it.preco ? fmt(it.preco) : 'grátis'}</span></div>`).join('');
    for (const d of $$('.item-loja', t)) d.onclick = () => escolher(itens.find((x) => x.id === d.dataset.id));
    if (itens[0]) escolher(itens[0]);
  }

  // ---------------- multiplayer ----------------
  let sala = null;
  function minhaEntrada(time) {
    const minhas = SKINS.filter((s) => s.time === time);
    const sk = minhas.find((s) => s.key === guardado.get('skin_' + time, '') && conta.temSkin(s)) || minhas.find((s) => conta.temSkin(s));
    const prim = ARMAS.filter((a) => a.time === time || a.time === -1);
    const a = prim.find((x) => x.key === guardado.get('arma_' + time, '') && conta.temArma(x)) || prim.find((x) => conta.temArma(x));
    return { nome: conta.nome(), time, skin: sk.i, arma: a.key, arma2: guardado.get('arma2_' + time, 'tt33') };
  }

  function telaMultiplayer() {
    menuGira = true;
    const t = mostrar(html(`<div class="tela">
      <div class="linha"><span class="apagado" style="font-weight:700">Seu nome:</span><input id="mnome" maxlength="16" style="max-width:260px"></div>
      <div class="linha cresce" style="align-items:stretch;gap:14px;margin-top:10px">
        <div class="cartao coluna cresce" style="border-color:rgba(251,191,36,.3)"><span class="titulo ouro" style="font-size:28px">CRIAR SALA</span>
          <span class="apagado" style="font-size:12px">Gera um código. Mande para os amigos: até 10 pessoas na mesma sala, jogador contra jogador.</span><span class="cresce"></span><button class="b-ouro" id="mcriar">CRIAR</button></div>
        <div class="cartao coluna cresce" style="border-color:rgba(96,165,250,.3)"><span class="titulo azul" style="font-size:28px">ENTRAR COM CÓDIGO</span>
          <input id="mcod" maxlength="6" placeholder="CÓDIGO" style="font-size:22px;text-transform:uppercase;letter-spacing:.2em"><span class="cresce"></span><button class="b-azul" id="mentrar">ENTRAR</button></div>
      </div><div class="status" id="mst"></div></div>`), 'multiplayer');
    t.prepend(cabecalho('MULTIPLAYER', telaInicio));
    $('#mnome', t).value = conta.nome();
    const st = $('#mst', t);
    const conectar = async (acao) => {
      const nm = $('#mnome', t).value.trim().slice(0, 16) || 'Jogador';
      guardado.set('nome', nm); salvarConta();
      if (!fb || !fb.token) { st.textContent = 'Entre na sua conta (ou como convidado, com o login anônimo ligado no Firebase) para jogar online'; return; }
      st.textContent = 'Conectando…';
      try {
        const [s, erro] = await acao();
        if (!s) { st.textContent = erro; return; }
        sala = s;
        ligarSala(s);
        telaSala();
      } catch (e) { st.textContent = 'sem conexão: ' + e.message; }
    };
    $('#mcriar', t).onclick = () => conectar(() => Sala.criar(minhaEntrada(guardado.get('lado', 0))));
    $('#mentrar', t).onclick = () => {
      const c = $('#mcod', t).value.trim().toUpperCase();
      if (c.length !== 6) { st.textContent = 'O código tem 6 letras/números'; return; }
      conectar(() => Sala.entrar(c, minhaEntrada(guardado.get('lado', 0))));
    };
  }

  function ligarSala(s) {
    s.ouvir(
      () => { if (telaAtual() === 'sala') telaSala(); },
      (estado) => { if (sala === s && estado === 'jogo' && !partida) comecarOnline(s); },
      () => { if (sala !== s) return; sala = null; aviso('A sala fechou (quem criou saiu)'); if (partida) pararPartida(); telaInicio(); },
      (id) => motor.redeEvento(JSON.stringify({ t: 'saiu', id })));
  }

  function sairDaSala() { const s = sala; sala = null; if (s) s.sair().catch(() => {}); }

  function telaSala() {
    const s = sala;
    if (!s) { telaInicio(); return; }
    menuGira = true;
    const lista = s.lista();
    const eu = lista.find((p) => p.id === fb.uid);
    const meuTime = eu ? eu.time | 0 : guardado.get('lado', 0);
    const coluna = (time) => {
      const do_time = lista.filter((p) => (p.time | 0) === time);
      return `<div class="cartao coluna cresce" style="background:${time ? '#2A1C08' : '#0F1D36'}"><b style="font-size:12px;color:${time ? '#FCD34D' : '#93C5FD'}">${time ? 'BANDIDOS' : 'POLÍCIA'}</b>
        ${do_time.map((p) => `<span style="font-size:14px;${p.id === fb.uid ? 'font-weight:800' : ''}">${p.id === fb.uid ? '▶ ' : ''}${esc(p.nome || 'Jogador')}${p.id === fb.uid ? ' (você)' : ''}</span>`).join('') || '<span class="apagado" style="font-size:12px">ninguém ainda</span>'}</div>`;
    };
    const t = mostrar(html(`<div class="tela">
      <div class="linha"><button class="b-fraco" id="ssair" style="width:44px;padding:0">‹</button>
        <div class="coluna" style="gap:0"><span class="apagado" style="font-size:11px;font-weight:800;letter-spacing:.12em">CÓDIGO DA SALA</span><span class="titulo ouro" style="font-size:40px;letter-spacing:.2em">${s.codigo}</span></div>
        <span class="apagado cresce" style="font-size:12px">Mande o código para os amigos · ${lista.length}/10 na sala</span></div>
      <div class="linha cresce" style="align-items:stretch;gap:12px;margin:8px 0">${coluna(0)}${coluna(1)}</div>
      <div class="linha"><button class="b-fraco" id="stime">${meuTime ? 'Ir para a Polícia' : 'Ir para os Bandidos'}</button><button class="b-fraco" id="sskin">Skin e armas</button><span class="cresce"></span>
        ${s.host ? '<button class="b-ouro titulo" id="sini" style="font-size:18px">INICIAR PARTIDA ▶</button>' : '<span class="apagado">Esperando quem criou a sala iniciar…</span>'}</div></div>`), 'sala');
    $('#ssair', t).onclick = () => { if (confirm(s.host ? 'Você criou a sala: ela fecha para todos. Sair?' : 'Sair da sala?')) { sairDaSala(); telaInicio(); } };
    $('#stime', t).onclick = () => { const outro = 1 - meuTime; guardado.set('lado', outro); s.atualizar(minhaEntrada(outro)); };
    $('#sskin', t).onclick = () => telaSkins(meuTime, () => { s.atualizar(minhaEntrada(meuTime)); telaSala(); });
    if (s.host) $('#sini', t).onclick = async () => { if (!(await s.comecar())) aviso('Não consegui iniciar (sem conexão?)'); };
  }

  function comecarOnline(s) {
    const eu = s.lista().find((p) => p.id === fb.uid) || minhaEntrada(guardado.get('lado', 0));
    const time = eu.time | 0;
    const skin = SKINS.find((x) => x.time === time && x.i === (eu.skin | 0)) || SKINS.find((x) => x.time === time);
    const arma = ARMAS.find((a) => a.key === eu.arma) || ARMAS.find((a) => a.time === time);
    const seg = ARMAS.find((a) => a.key === eu.arma2 && a !== arma) || null;
    iniciarPartida(time, skin, arma, seg, s);
  }

  // ---------------- partida ----------------
  let partida = null;  // {time, sala, hud, ...}
  let atirando = false;
  let olhar = { dx: 0, dy: 0 };

  function iniciarPartida(time, skin, arma, segunda, s) {
    menuGira = false;
    ctxAudio();
    partida = { time, sala: s, fim: false, ultimoHud: {} };
    montarHud(!!s);
    motor.redeSala(s ? s.host : false, s ? fb.uid : '', s ? s.roster() : '');
    for (const a of ARMAS) motor.skinArma(a.key, conta.skinArma(a.key));
    motor.escolhas(time, skin.i, arma.key, segunda ? segunda.key : '');
    motor.fps(true);
    motor.play(true);
    motor.camera(0, 0, 0, time === 0 ? 0 : 3.14159, 0.05, 3);
    if (s) ligarAoVivo(s, time);
    if (!toque) $('#tela').requestPointerLock?.();
  }

  function pararPartida() {
    if (!partida) return;
    atirando = false;
    desligarAoVivo();
    motor.andar(0, 0, false); motor.mirar(false); motor.agachar(false);
    motor.play(false); motor.fps(false); motor.redeSala(false, '', '');
    visaoGeral();
    partida = null;
    if (document.pointerLockElement) document.exitPointerLock();
  }

  function pausar() {
    if (!partida || partida.fim) return;
    atirando = false;
    if (document.pointerLockElement) document.exitPointerLock();
    const p = html(`<div style="position:absolute;inset:0;background:rgba(0,0,0,.55);display:flex;align-items:center;justify-content:center;z-index:5">
      <div class="cartao coluna" style="width:260px"><span class="titulo" style="font-size:30px">PAUSA</span><button class="b-ouro" id="pcont">Continuar</button><button class="b-fraco" id="psair">Sair da partida</button></div></div>`);
    ui.appendChild(p);
    $('#pcont', p).onclick = () => { p.remove(); if (!toque) $('#tela').requestPointerLock?.(); };
    $('#psair', p).onclick = () => { p.remove(); pararPartida(); sairDaSala(); telaInicio(); };
  }

  function telaFim(j) {
    const sc = j.placar || [0, 0];
    const venceu = sc[0] > sc[1] ? 0 : sc[1] > sc[0] ? 1 : -1;
    const time = partida ? partida.time : 0;
    const ganho = (j.abates | 0) * 25 + (venceu === time ? 150 : venceu < 0 ? 80 : 50);
    const s = partida && partida.sala;
    pararPartida();
    conta.moedas = conta.moedas + ganho;
    const tabela = (tm) => {
      const l = (j.jogadores || []).filter((p) => p.time === tm).sort((a, b) => b.abates - a.abates);
      return `<div class="cartao coluna cresce" style="background:${tm ? '#2A1C08' : '#0F1D36'}"><div class="linha" style="font-size:11px;font-weight:800"><span class="cresce" style="color:${tm ? '#FCD34D' : '#93C5FD'}">${tm ? 'BANDIDOS' : 'POLÍCIA'}</span><span class="apagado">ABATES MORTES</span></div>
        ${l.map((p) => `<div class="linha" style="font-size:13px;${p.voce ? 'font-weight:800;background:rgba(255,255,255,.12);border-radius:8px;padding:2px 6px' : ''}"><span class="cresce">${esc(p.nome)}</span><b style="width:52px;text-align:right">${p.abates}</b><span class="apagado" style="width:52px;text-align:right">${p.mortes}</span></div>`).join('')}</div>`;
    };
    const t = mostrar(html(`<div class="tela"><div class="linha"><div class="coluna cresce" style="gap:0"><span style="font-size:12px;font-weight:800;letter-spacing:.15em" class="${venceu === 1 ? 'amarelo' : 'azul'}">${venceu < 0 ? 'FIM DE JOGO' : 'VITÓRIA DOS'}</span>
      <span class="titulo ${venceu === 1 ? 'ouro' : 'azul'}" style="font-size:50px">${venceu < 0 ? 'EMPATE' : venceu === 0 ? 'POLÍCIA' : 'BANDIDOS'}</span></div>
      <div class="coluna" style="align-items:flex-end"><div class="cartao linha"><span class="titulo azul" style="font-size:44px">${sc[0]}</span><span class="apagado">x</span><span class="titulo ouro" style="font-size:44px">${sc[1]}</span></div>
      <span class="amarelo" style="font-size:13px;font-weight:800">+${fmt(ganho)} moedas</span></div></div>
      <div class="linha cresce" style="align-items:flex-start;gap:12px;margin:10px 0;overflow:auto">${tabela(0)}${tabela(1)}</div>
      <div class="linha" style="justify-content:flex-end">${s ? '<button class="b-fraco" id="fsair">Sair da sala</button><button class="b-ouro" id="fvolta">VOLTAR À SALA</button>' : '<button class="b-fraco" id="fmenu">Menu inicial</button><button class="b-ouro" id="fnovo">JOGAR DE NOVO</button>'}</div></div>`), 'fim');
    if (s) { $('#fsair', t).onclick = () => { sairDaSala(); telaInicio(); }; $('#fvolta', t).onclick = () => { s.voltar(); telaSala(); }; }
    else { $('#fmenu', t).onclick = telaInicio; $('#fnovo', t).onclick = () => telaSkins(time); }
  }

  // ---------------- HUD ----------------
  function montarHud(online) {
    const t = mostrar(html(`<div id="hud">
      <div id="vida" class="hud-caixa"><span style="color:#F87171">♥</span><div class="trilho"><div class="barra"></div></div><b id="hp">100</b></div>
      <div id="kd" class="hud-caixa">0 abates · 0 mortes</div>
      <canvas id="minimapa" width="168" height="168"></canvas>
      <div id="placar"><div class="faixa"><div class="pol"><span style="font-size:11px">POLÍCIA</span><span class="num" id="sp">0</span></div><div class="relogio" id="rel">10:00</div><div class="ban"><span class="num" id="sb">0</span><span style="font-size:11px">BANDIDOS</span></div></div>
        <span class="hud-caixa" id="meta" style="font-size:11px;padding:1px 8px">até 25 abates</span></div>
      <div id="rede" class="hud-caixa" style="display:${online ? '' : 'none'}"></div>
      <div id="abates"></div>
      <button id="pausa" class="toque">II</button>
      <canvas id="luneta"></canvas><div id="dano"></div><canvas id="mira" width="160" height="160"></canvas>
      <div id="centro"></div>
      <div id="armas"></div>
      <div id="teclas">WASD andar · Shift correr · mouse olhar · clique atirar · botão direito mirar · Espaço pular · C agachar · R recarregar · Q trocar arma · G granada${online ? ' · V falar (segure)' : ''} · Esc pausa</div>
      <div class="so-toque toque" id="zonaAndar" style="position:absolute;left:0;top:0;width:50%;height:100%"></div>
      <div class="so-toque toque" id="zonaOlhar" style="position:absolute;right:0;top:0;width:50%;height:100%"></div>
      <canvas class="so-toque" id="stick" width="300" height="300" style="position:absolute;left:0;top:0;width:150px;height:150px;pointer-events:none"></canvas>
      <button class="so-toque toque redondo tiro" id="btiro2" style="left:46px;top:calc(50% - 28px);width:56px;height:56px;font-size:22px">✛</button>
      <div class="so-toque" style="position:absolute;right:14px;bottom:12px;width:250px;height:250px">
        <button class="toque redondo tiro" id="btiro" style="right:8px;bottom:34px;width:84px;height:84px">✛</button>
        <button class="toque redondo" id="bmira" style="right:104px;bottom:38px;width:54px;height:54px">MIRA</button>
        <button class="toque redondo" id="bpula" style="right:2px;bottom:132px;width:54px;height:54px">▲<br>PULAR</button>
        <button class="toque redondo" id="bagacha" style="right:66px;bottom:118px;width:54px;height:54px">▼<br>AGACHAR</button>
        <button class="toque redondo" id="bgranada" style="right:130px;bottom:108px;width:54px;height:54px;border-color:rgba(252,211,77,.8)">💣<span id="ngr" style="position:absolute;right:-4px;top:-4px;background:#FBBF24;color:#111827;border-radius:10px;min-width:20px;font-size:11px">2</span></button>
        <button class="toque redondo" id="brec" style="right:172px;bottom:44px;width:48px;height:48px;font-size:18px">⟳</button>
      </div>
      ${online ? '<button class="toque redondo" id="bfala" style="right:14px;top:calc(50% - 64px);width:48px;height:48px;font-size:20px;display:flex">🎤</button><button class="toque redondo" id="bouve" style="right:14px;top:62px;width:44px;height:36px;border-radius:10px;font-size:16px;display:flex">🔊</button>' : ''}
    </div>`), 'hud');
    partida.hud = t;
    $('#pausa', t).onclick = pausar;
    ligarToque(t);
    if (online) {
      const fala = $('#bfala', t);
      const on = (e) => { e.preventDefault(); falar(true); fala.classList.add('ligado'); };
      const off = (e) => { e.preventDefault(); falar(false); fala.classList.remove('ligado'); };
      fala.addEventListener('pointerdown', on); fala.addEventListener('pointerup', off); fala.addEventListener('pointercancel', off); fala.addEventListener('pointerleave', off);
      $('#bouve', t).onclick = () => { if (voz) { voz.ouvindo = !voz.ouvindo; $('#bouve', t).textContent = voz.ouvindo ? '🔊' : '🔇'; } };
    }
  }

  function desenharMira(cv, mirando, acerto, cabeca) {
    const c = cv.getContext('2d'), w = cv.width, cx = w / 2, cy = w / 2, k = w / 80;
    c.clearRect(0, 0, w, w);
    c.lineCap = 'round';
    if (mirando) {
      c.fillStyle = '#000'; c.beginPath(); c.arc(cx, cy, 3.6 * k, 0, 7); c.fill();
      c.fillStyle = '#FF2A1F'; c.beginPath(); c.arc(cx, cy, 2.6 * k, 0, 7); c.fill();
    } else {
      const g = 7 * k, l = 11 * k;
      for (const [cor, lw] of [['rgba(0,0,0,.7)', 4.5 * k], ['#fff', 2.5 * k]]) {
        c.strokeStyle = cor; c.lineWidth = lw; c.beginPath();
        c.moveTo(cx, cy - g - l); c.lineTo(cx, cy - g); c.moveTo(cx, cy + g); c.lineTo(cx, cy + g + l);
        c.moveTo(cx - g - l, cy); c.lineTo(cx - g, cy); c.moveTo(cx + g, cy); c.lineTo(cx + g + l, cy); c.stroke();
      }
      c.fillStyle = 'rgba(0,0,0,.7)'; c.beginPath(); c.arc(cx, cy, 2.6 * k, 0, 7); c.fill();
      c.fillStyle = '#fff'; c.beginPath(); c.arc(cx, cy, 1.6 * k, 0, 7); c.fill();
    }
    if (acerto) {
      c.strokeStyle = cabeca ? '#FF3B30' : '#fff'; c.lineWidth = 2 * k;
      const a = (cabeca ? 13 : 10) * k, b = 5 * k;
      c.beginPath();
      c.moveTo(cx - a, cy - a); c.lineTo(cx - b, cy - b); c.moveTo(cx + a, cy - a); c.lineTo(cx + b, cy - b);
      c.moveTo(cx - a, cy + a); c.lineTo(cx - b, cy + b); c.moveTo(cx + a, cy + a); c.lineTo(cx + b, cy + b); c.stroke();
    }
  }

  function desenharLuneta(cv) {
    const w = cv.width = innerWidth, h = cv.height = innerHeight, c = cv.getContext('2d');
    const cx = w / 2, cy = h / 2, r = Math.min(w, h) * 0.47;
    c.fillStyle = '#000'; c.beginPath(); c.rect(0, 0, w, h); c.arc(cx, cy, r, 0, Math.PI * 2, true); c.fill();
    const grad = c.createRadialGradient(cx, cy, r * 0.86, cx, cy, r);
    grad.addColorStop(0, 'rgba(0,0,0,0)'); grad.addColorStop(1, 'rgba(0,0,0,.8)');
    c.fillStyle = grad; c.beginPath(); c.arc(cx, cy, r, 0, 7); c.fill();
    c.strokeStyle = 'rgba(16,16,16,.95)';
    c.lineWidth = 4; c.beginPath(); c.moveTo(cx - r, cy); c.lineTo(cx - r * 0.45, cy); c.moveTo(cx + r * 0.45, cy); c.lineTo(cx + r, cy); c.moveTo(cx, cy + r * 0.45); c.lineTo(cx, cy + r); c.stroke();
    const gap = r * 0.06;
    c.lineWidth = 1.6; c.beginPath(); c.moveTo(cx - r * 0.45, cy); c.lineTo(cx - gap, cy); c.moveTo(cx + gap, cy); c.lineTo(cx + r * 0.45, cy);
    c.moveTo(cx, cy + gap); c.lineTo(cx, cy + r * 0.45); c.moveTo(cx, cy - r * 0.3); c.lineTo(cx, cy - gap); c.stroke();
    c.fillStyle = 'rgba(255,42,31,.35)'; c.beginPath(); c.arc(cx, cy, 5, 0, 7); c.fill();
    c.fillStyle = '#FF2A1F'; c.beginPath(); c.arc(cx, cy, 2.6, 0, 7); c.fill();
  }

  function desenharMinimapa(cv, pontos, meuTime) {
    const c = cv.getContext('2d'), w = cv.width, cx = w / 2, cy = w / 2, r = w / 2;
    c.clearRect(0, 0, w, w);
    c.fillStyle = 'rgba(11,15,23,.72)'; c.beginPath(); c.arc(cx, cy, r, 0, 7); c.fill();
    c.strokeStyle = 'rgba(255,255,255,.25)'; c.lineWidth = 3; c.beginPath(); c.arc(cx, cy, r - 2, 0, 7); c.stroke();
    const k = (r - 10) / 45;
    for (const p of pontos || []) {
      c.fillStyle = p[2] === 1 ? '#F87171' : meuTime === 0 ? '#60A5FA' : '#FBBF24';
      c.beginPath(); c.arc(cx + p[0] * k, cy - p[1] * k, 6, 0, 7); c.fill();
    }
    c.fillStyle = '#fff'; c.beginPath(); c.moveTo(cx, cy - 12); c.lineTo(cx - 8, cy + 8); c.lineTo(cx + 8, cy + 8); c.fill();
  }

  function atualizarHud(j) {
    const t = partida && partida.hud;
    if (!t || !j.ativo) return;
    const hp = j.vida | 0, max = Math.max(1, j.vidaMax | 0);
    const barra = $('#vida .barra', t);
    barra.style.width = (100 * hp / max) + '%';
    barra.style.background = hp > 50 ? '#4ADE80' : hp > 25 ? '#FBBF24' : '#F87171';
    $('#hp', t).textContent = hp;
    $('#kd', t).textContent = `${j.abates | 0} abates · ${j.mortes | 0} mortes`;
    $('#sp', t).textContent = (j.placar || [0, 0])[0]; $('#sb', t).textContent = (j.placar || [0, 0])[1];
    $('#rel', t).textContent = j.tempo < 0 ? '--:--' : `${Math.floor(j.tempo / 60)}:${String(j.tempo % 60).padStart(2, '0')}`;
    $('#meta', t).textContent = `até ${j.meta || 25} abates`;
    $('#ngr', t).textContent = j.granadas | 0;
    const armas = j.armas || [];
    $('#armas', t).innerHTML = armas.map((a, i) => `<div class="slot toque ${i === j.slot ? 'ativa' : ''}" data-i="${i}"><img src="armas/${a.arma}.png" alt="">
      <span style="color:${a.balas === 0 && !(j.recarregando && i === j.slot) ? '#F87171' : '#F3F4F6'}">${j.recarregando && i === j.slot ? 'recarregando…' : a.balas + ' / ' + (a.reserva < 0 ? '∞' : a.reserva)}</span></div>`).join('') +
      (armas.length > 1 ? '<button class="toque" id="btroca" style="width:46px;height:46px;padding:0;background:rgba(11,15,23,.72);border:2px solid rgba(255,255,255,.5);font-size:20px">⇄</button>' : '');
    for (const s of $$('.slot', t)) s.onclick = () => { if (Number(s.dataset.i) !== j.slot) motor.trocar(); };
    if ($('#btroca', t)) $('#btroca', t).onclick = () => motor.trocar();
    const luneta = j.mirando && j.luneta;
    const lu = $('#luneta', t);
    if (luneta && lu.style.display !== 'block') { lu.style.display = 'block'; desenharLuneta(lu); }
    if (!luneta) lu.style.display = 'none';
    const mira = $('#mira', t);
    mira.style.visibility = luneta ? 'hidden' : 'visible';
    desenharMira(mira, j.mirando, j.acerto, j.cabeca);
    $('#dano', t).style.display = j.dano ? 'block' : 'none';
    const centro = $('#centro', t);
    const msg = j.morto ? `Você foi abatido<br>Renasce em ${j.renasce} s` : j.estado ? esc(j.msg) : '';
    centro.innerHTML = msg; centro.style.display = msg ? 'block' : 'none';
    const feed = (j.abatidos || []).slice(-4);
    const cor = (tm) => (tm === 0 ? '#93C5FD' : '#FCD34D');
    $('#abates', t).innerHTML = feed.map((f) => `<span class="hud-caixa" style="${f.voce ? 'background:rgba(31,41,55,.85)' : ''}">${f.a ? `<b style="color:${cor(f.at)}">${esc(f.a)}</b> abateu <b style="color:${cor(f.vt)}">${esc(f.v)}</b> · ${esc(f.arma)}` : `<b style="color:${cor(f.vt)}">${esc(f.v)}</b> caiu`}</span>`).join('');
    desenharMinimapa($('#minimapa', t), j.mapa, j.time | 0);
  }

  // ---------------- controles ----------------
  const teclas = new Set();
  let agachado = false, mirandoToque = false;
  function enviarAndar() {
    if (!partida) return;
    const x = (teclas.has('KeyD') ? 1 : 0) - (teclas.has('KeyA') ? 1 : 0);
    const y = (teclas.has('KeyW') ? 1 : 0) - (teclas.has('KeyS') ? 1 : 0);
    const n = Math.hypot(x, y) || 1;
    motor.andar(x / n, y / n, (teclas.has('ShiftLeft') || teclas.has('ShiftRight')) && y > 0);
  }
  addEventListener('keydown', (e) => {
    if (!partida || e.target.tagName === 'INPUT') return;
    if (e.code === 'Escape') { pausar(); return; }
    if (e.repeat) return;
    teclas.add(e.code);
    switch (e.code) {
      case 'Space': motor.pular(); e.preventDefault(); break;
      case 'KeyC': case 'ControlLeft': agachado = !agachado; motor.agachar(agachado); break;
      case 'KeyR': motor.recarregar(); break;
      case 'KeyQ': case 'Digit1': case 'Digit2': motor.trocar(); break;
      case 'KeyG': motor.granada(); break;
      case 'KeyE': motor.interagir(); break;
      case 'KeyV': falar(true); break;
    }
    enviarAndar();
  });
  addEventListener('keyup', (e) => {
    teclas.delete(e.code);
    if (e.code === 'KeyV') falar(false);
    enviarAndar();
  });
  addEventListener('blur', () => { teclas.clear(); atirando = false; enviarAndar(); });
  const tela = $('#tela');
  tela.addEventListener('mousedown', (e) => {
    if (!partida || toque) return;
    ctxAudio();
    if (!document.pointerLockElement) { tela.requestPointerLock?.(); return; }
    if (e.button === 0) atirando = true;
    if (e.button === 2) motor.mirar(true);
  });
  addEventListener('mouseup', (e) => { if (e.button === 0) atirando = false; if (e.button === 2 && partida) motor.mirar(false); });
  addEventListener('contextmenu', (e) => { if (partida) e.preventDefault(); });
  addEventListener('mousemove', (e) => {
    if (!partida || document.pointerLockElement !== tela) return;
    const k = guardado.get('sensibilidade', 1) * 0.9;
    olhar.dx += e.movementX * k; olhar.dy += e.movementY * k;
  });
  // ui por cima do canvas: cliques nas áreas vazias do HUD também valem para o jogo no computador
  ui.addEventListener('mousedown', (e) => { if (partida && !toque && e.target.closest('#hud') && !e.target.closest('button,.slot')) tela.dispatchEvent(new MouseEvent('mousedown', e)); });

  // Toque: metade esquerda anda (analógico onde o dedo encostar), metade direita gira a câmera
  function ligarToque(t) {
    const stick = $('#stick', t), zonaA = $('#zonaAndar', t), zonaO = $('#zonaOlhar', t);
    const R = 58 * (devicePixelRatio > 1 ? 1 : 1);
    let ida = null, ox = 0, oy = 0, kx = 0, ky = 0;
    const desenha = () => {
      const c = stick.getContext('2d'); c.clearRect(0, 0, 300, 300);
      c.fillStyle = 'rgba(255,255,255,.18)'; c.beginPath(); c.arc(150, 150, 116, 0, 7); c.fill();
      c.strokeStyle = 'rgba(255,255,255,.45)'; c.lineWidth = 4; c.stroke();
      c.fillStyle = 'rgba(255,255,255,.6)'; c.beginPath(); c.arc(150 + kx * 116, 150 + ky * 116, 46, 0, 7); c.fill();
      const x = ida !== null ? ox : 26 + R, y = ida !== null ? oy : innerHeight - 20 - R;
      stick.style.left = (x - 75) + 'px'; stick.style.top = (y - 75) + 'px';
      stick.style.opacity = ida !== null ? 1 : 0.5;
    };
    desenha();
    zonaA.addEventListener('pointerdown', (e) => { if (ida !== null) return; ctxAudio(); ida = e.pointerId; ox = e.clientX; oy = e.clientY; kx = ky = 0; zonaA.setPointerCapture(e.pointerId); desenha(); });
    zonaA.addEventListener('pointermove', (e) => {
      if (e.pointerId !== ida) return;
      let dx = (e.clientX - ox) / R, dy = (e.clientY - oy) / R;
      const len = Math.hypot(dx, dy);
      if (len > 1) { ox += (dx - dx / len) * R; oy += (dy - dy / len) * R; dx /= len; dy /= len; }
      kx = dx; ky = dy;
      motor.andar(dx, -dy, -dy > 0.93);
      desenha();
    });
    const soltaA = (e) => { if (e.pointerId !== ida) return; ida = null; kx = ky = 0; motor.andar(0, 0, false); desenha(); };
    zonaA.addEventListener('pointerup', soltaA); zonaA.addEventListener('pointercancel', soltaA);
    const ult = new Map();
    zonaO.addEventListener('pointerdown', (e) => { ctxAudio(); ult.set(e.pointerId, [e.clientX, e.clientY]); zonaO.setPointerCapture(e.pointerId); });
    zonaO.addEventListener('pointermove', (e) => {
      const p = ult.get(e.pointerId); if (!p) return;
      olhar.dx += (e.clientX - p[0]) * 2.2; olhar.dy += (e.clientY - p[1]) * 2.2;
      ult.set(e.pointerId, [e.clientX, e.clientY]);
    });
    const soltaO = (e) => ult.delete(e.pointerId);
    zonaO.addEventListener('pointerup', soltaO); zonaO.addEventListener('pointercancel', soltaO);
    // tiro: segurar atira; arrastar no mesmo dedo também mira
    for (const id of ['#btiro', '#btiro2']) {
      const b = $(id, t); let px = 0, py = 0;
      b.addEventListener('pointerdown', (e) => { ctxAudio(); atirando = true; px = e.clientX; py = e.clientY; b.setPointerCapture(e.pointerId); b.style.opacity = 0.7; });
      b.addEventListener('pointermove', (e) => { if (!atirando) return; olhar.dx += (e.clientX - px) * 2.2; olhar.dy += (e.clientY - py) * 2.2; px = e.clientX; py = e.clientY; });
      const solta = () => { atirando = false; b.style.opacity = 1; };
      b.addEventListener('pointerup', solta); b.addEventListener('pointercancel', solta);
    }
    $('#bmira', t).onclick = (e) => { mirandoToque = !mirandoToque; motor.mirar(mirandoToque); e.currentTarget.classList.toggle('ligado', mirandoToque); };
    $('#bpula', t).onclick = () => motor.pular();
    $('#bagacha', t).onclick = (e) => { agachado = !agachado; motor.agachar(agachado); e.currentTarget.classList.toggle('ligado', agachado); };
    $('#bgranada', t).onclick = () => motor.granada();
    $('#brec', t).onclick = () => motor.recarregar();
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
      if (!partida) return;
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
    // 20 vezes por segundo: o meu estado e os eventos
    enviandoTimer = setInterval(() => {
      if (!ws || ws.readyState !== 1) return;
      const st = motor.redeColetar();
      if (st.length > 2) ws.send(`{"k":"st","d":${st}}`);
      try { for (const e of JSON.parse(motor.redeEventos())) ws.send(JSON.stringify({ k: 'ev', d: e })); } catch (_) {}
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
      if (st.length > 2) fb.patch(`salas/${s.codigo}/est`, JSON.parse(st)).catch(() => {});
      try { for (const e of JSON.parse(motor.redeEventos())) fb.post(`salas/${s.codigo}/ev`, { ...e, cel: fb.uid }).catch(() => {}); } catch (_) {}
    }, 150);
  }

  function desligarAoVivo() {
    clearInterval(enviandoTimer); enviandoTimer = null;
    if (ws) { try { ws.close(1000, 'saiu'); } catch (_) {} ws = null; }
    if (viaFirebase) { for (const o of viaFirebase.ouvintes) o.fechar(); viaFirebase = null; }
    if (voz) { voz.desligar(); voz = null; }
  }

  function falar(on) { if (voz) voz.falar(on); }

  // Chat de voz: 8 kHz μ-law, pacotes de 20 ms (o mesmo do app). Microfone só quando aperta para falar.
  class ChatDeVoz {
    constructor(enviar) { this.enviar = enviar; this.ouvindo = true; this.falando = false; this.prox = {}; this.mic = null; }
    static dec(u) { u = ~u & 0xFF; let s = ((u & 0x0F) << 3) + 0x84; s <<= (u & 0x70) >> 4; return (u & 0x80 ? 0x84 - s : s - 0x84) / 32768; }
    static cod(x) {
      let s = Math.max(-32635, Math.min(32635, Math.round(x * 32767))), sinal = 0;
      if (s < 0) { s = -s; sinal = 0x80; }
      s += 0x84; let exp = 7;
      for (let m = 0x4000; exp > 0 && !(s & m); m >>= 1) exp--;
      return ~(sinal | (exp << 4) | ((s >> (exp + 3)) & 0x0F)) & 0xFF;
    }
    receber(quem, dados) {
      const a = ctxAudio(); if (!a || !this.ouvindo) return;
      const buf = a.createBuffer(1, dados.length, 8000);
      const ch = buf.getChannelData(0);
      for (let i = 0; i < dados.length; i++) ch[i] = ChatDeVoz.dec(dados[i]);
      const src = a.createBufferSource(); src.buffer = buf; src.connect(a.destination);
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
        let pos = 0;
        const quadro = new Uint8Array(160);
        let n = 0;
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
        const mudo = a.createGain(); mudo.gain.value = 0;
        proc.connect(mudo).connect(a.destination);
        this.mic = { stream, proc, fonte };
      } catch (e) { aviso('Microfone bloqueado: libere no navegador para falar'); }
    }
    desligar() { this.falando = false; if (this.mic) { this.mic.stream.getTracks().forEach((t) => t.stop()); this.mic.proc.disconnect(); this.mic = null; } }
  }

  // ---------------- laço por quadro (chamado pelo motor depois de desenhar) ----------------
  let quadro = 0;
  function aposQuadro() {
    quadro++;
    if (olhar.dx || olhar.dy) { motor.olhar(olhar.dx, olhar.dy); olhar.dx = olhar.dy = 0; }
    if (menuGira && !partida) motor.olhar(-0.35, 0);
    if (atirando && partida) motor.atirar();
    const s = motor.sons();
    if (s && audio) for (const l of s.split('\n')) { const p = l.split('\t'); if (p.length >= 3) tocar(p[0], parseFloat(p[1]), parseFloat(p[2])); }
    if (partida && quadro % 6 === 0) {
      const j = motor.hud();
      atualizarHud(j);
      if (!partida.fim && j.estado) { partida.fim = true; atirando = false; setTimeout(() => telaFim(j), 2500); }
    }
  }

  // ---------------- tela do jogo (resolução pela qualidade) ----------------
  function ajustarTela() {
    const q = guardado.get('qualidade', toque ? 0 : 1);
    const escala = [0.55, 0.8, 1][q] * Math.min(devicePixelRatio || 1, 1.5);
    tela.width = Math.max(1, Math.round(innerWidth * escala));
    tela.height = Math.max(1, Math.round(innerHeight * escala));
  }
  addEventListener('resize', ajustarTela);
  ajustarTela();

  // ---------------- início ----------------
  async function iniciar() {
    const prog = $('#progresso'), st = $('#status');
    if (typeof window.FavelaMotor !== 'function') { st.textContent = 'O motor do jogo não carregou (favela.js).'; return; }
    try {
      M = await window.FavelaMotor({
        canvas: tela,
        locateFile: (p) => p,
        print: (t) => console.log(t),
        printErr: (t) => console.warn(t),
        setStatus: (t) => {
          const m = /\((\d+(?:\.\d+)?)\/(\d+)\)/.exec(t || '');
          if (m) { prog.style.width = (100 * m[1] / m[2]) + '%'; st.textContent = `Baixando o jogo… ${Math.round(m[1] / 1048576)} de ${Math.round(m[2] / 1048576)} MB`; }
          else if (t) st.textContent = t.replace('Downloading data...', 'Baixando o jogo…').replace('Running...', 'Abrindo…');
        },
        aposQuadro,
      });
    } catch (e) { st.textContent = 'Não consegui abrir o jogo: ' + e; return; }
    M.aposQuadro = aposQuadro;
    st.textContent = 'Preparando o mapa…';
    prog.style.width = '100%';
    await dorme(30);
    if (!M.ccall('web_iniciar', 'number', ['number'], [guardado.get('qualidade', toque ? 0 : 1)])) {
      st.textContent = 'Seu navegador não tem WebGL 2. Tente o Chrome ou o Firefox atualizados.'; return;
    }
    try {
      const proj = await (await fetch('exemplos/5_policia_x_bandidos.json')).json();
      proj.objetos = (proj.objetos || []).filter((o) => o.tipo !== 'loja');  // as barracas de loja ficam fora: a loja é no menu
      M.ccall('web_sem_editor', null, [], []);
      motor.abrir(JSON.stringify(proj));
      M.ccall('web_selecionar', null, ['number'], [-1]);
      visaoGeral();
    } catch (e) { console.warn('projeto da partida', e); }
    $('#carregando').remove();
    comecarLogin();
  }
  iniciar();
})();
