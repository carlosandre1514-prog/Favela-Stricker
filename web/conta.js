// Favela Stricker — catálogo, configurações, conta do jogador (Firebase) e salas do multiplayer.
'use strict';

// ---------------- guardado no aparelho ----------------
const guardado = {
  get(k, d) { try { const v = localStorage.getItem('fs_' + k); return v === null ? d : JSON.parse(v); } catch (_) { return d; } },
  set(k, v) { try { localStorage.setItem('fs_' + k, JSON.stringify(v)); } catch (_) {} },
  del(k) { try { localStorage.removeItem('fs_' + k); } catch (_) {} },
};

// ---------------- catálogo (preços provisórios) ----------------
const RARIDADES = { comum: 'Comum', rara: 'Rara', epica: 'Épica', lendaria: 'Lendária' };

// Roupas (skins de personagem): o personagem é sempre o mesmo e sempre usa uma delas. As 10 são gratuitas.
// A ordem de cada lado é a do motor (Game.cpp kSkins). Polícia só usa farda; bandido só roupa de bandido.
const ROUPAS = [
  { key: 'pm', nome: 'PM', time: 0, i: 0, preco: 0, raridade: 'comum' },
  { key: 'civil', nome: 'Polícia Civil', time: 0, i: 1, preco: 0, raridade: 'comum' },
  { key: 'exercito', nome: 'Exército', time: 0, i: 2, preco: 0, raridade: 'comum' },
  { key: 'choque', nome: 'Choque', time: 0, i: 3, preco: 0, raridade: 'comum' },
  { key: 'especial', nome: 'Especial', time: 0, i: 4, preco: 0, raridade: 'comum' },
  { key: 'rubro', nome: 'Rubro', time: 1, i: 0, preco: 0, raridade: 'comum' },
  { key: 'alvinegro', nome: 'Alvinegro', time: 1, i: 1, preco: 0, raridade: 'comum' },
  { key: 'tricolor', nome: 'Tricolor', time: 1, i: 2, preco: 0, raridade: 'comum' },
  { key: 'canarinho', nome: 'Canarinho', time: 1, i: 3, preco: 0, raridade: 'comum' },
  { key: 'faixa', nome: 'Faixa', time: 1, i: 4, preco: 0, raridade: 'comum' },
];

// Armas (compradas na partida com o $ dos abates). Os números são os do motor (Game.cpp kGuns).
const ARMAS = [
  { key: 'tt33', nome: 'TT-33', time: -1, dano: 25, cadencia: 3, pente: 8, cai: 20, inicial: true },
  { key: 'm4a1', nome: 'M4A1', time: 0, dano: 24, cadencia: 10, pente: 30, cai: 40, inicial: true },
  { key: 'm4colt', nome: 'M4 Colt', time: 0, dano: 26, cadencia: 9, pente: 30, cai: 45, luneta: true },
  { key: 'ak47', nome: 'AK-47', time: 1, dano: 30, cadencia: 8, pente: 30, cai: 35, inicial: true },
  { key: 'ak74', nome: 'AK-74', time: 1, dano: 27, cadencia: 9, pente: 30, cai: 40 },
  { key: 'ak74_tatica', nome: 'AK-74 tática', time: 1, dano: 28, cadencia: 9.5, pente: 30, cai: 45 },
];
const armaPorKey = (k) => ARMAS.find((a) => a.key === k) || ARMAS[0];
const armaInicial = (time) => (time === 0 ? 'm4a1' : 'ak47');

// Skins de arma (mercado, com moedas): só mudam a pintura, não o dano
const SKINS_ARMA = [
  { arma: 'ak47', key: 'dourada', nome: 'AK-47 Dourada', raridade: 'lendaria', preco: 1499 },
  { arma: 'm4colt', key: 'dourada', nome: 'M4 Colt Dourada', raridade: 'lendaria', preco: 1499 },
  { arma: 'tt33', key: 'gold_relic', nome: 'TT-33 Gold Relic', raridade: 'lendaria', preco: 1499 },
  { arma: 'ak47', key: 'dragao', nome: 'AK-47 Dragão', raridade: 'epica', preco: 899 },
  { arma: 'tt33', key: 'nebula_shadow', nome: 'TT-33 Nebula Shadow', raridade: 'epica', preco: 899 },
  { arma: 'm4a1', key: 'neon', nome: 'M4A1 Neon', raridade: 'rara', preco: 499 },
  { arma: 'ak74', key: 'favela', nome: 'AK-74 Favela', raridade: 'rara', preco: 499 },
  { arma: 'tt33', key: 'crimson_forge', nome: 'TT-33 Crimson Forge', raridade: 'rara', preco: 499 },
  { arma: 'ak74_tatica', key: 'selva', nome: 'AK-74 tática Selva', raridade: 'comum', preco: 199 },
  { arma: 'm4a1', key: 'urbana', nome: 'M4A1 Urbana', raridade: 'comum', preco: 199 },
  { arma: 'ak47', key: 'deserto', nome: 'AK-47 Deserto', raridade: 'comum', preco: 199 },
];

const PACOTES = [
  { moedas: 100, bonus: 0, preco: 'R$ 4,99' },
  { moedas: 310, bonus: 10, preco: 'R$ 14,99' },
  { moedas: 520, bonus: 20, preco: 'R$ 24,99', selo: 'POPULAR' },
  { moedas: 1060, bonus: 60, preco: 'R$ 49,99' },
  { moedas: 2180, bonus: 180, preco: 'R$ 99,99', selo: 'MELHOR VALOR' },
  { moedas: 5600, bonus: 600, preco: 'R$ 249,99' },
];

const NIVEIS = [
  { nome: 'Leve', info: 'Celulares de 3 a 4 GB (ex.: Galaxy A14). 30 quadros, sem sombras, personagens simplificados.' },
  { nome: 'Padrão', info: 'Celulares de 4 a 6 GB. 40 quadros, chão detalhado, mais luzes.' },
  { nome: 'Intermediária', info: 'Celulares de 6 a 8 GB. 50 quadros, sombras perto, texturas melhores.' },
  { nome: 'Ultra', info: 'Celulares top e computadores. Tudo liberado: texturas e modelos originais, sombras nítidas e longas, antisserrilhado, reflexos, visão longe, 60 quadros.' },
];

// ---------------- configurações do jogador ----------------
const CONFIG_PADRAO = {
  sens: 1.0,          // câmera (arrastar a tela / mouse)
  sensMira: 0.7,      // mirando (ADS)
  sensLuneta: 0.45,   // olhando pela luneta
  inverterY: false,
  volume: 0.9,        // tiros, passos, explosões
  volumeVoz: 1.0,     // chat de voz do time
  vibrar: true,
};
const config = Object.assign({}, CONFIG_PADRAO, guardado.get('config', {}));
function salvarConfig() { guardado.set('config', config); }

// ---------------- moedas e itens ----------------
const conta = {
  get moedas() { return guardado.get('moedas', 1000); },
  set moedas(v) { guardado.set('moedas', Math.max(0, Math.round(v))); salvarConta(); },
  itens() { return new Set(guardado.get('itens', [])); },
  tem(id, preco) { return preco === 0 || this.itens().has(id); },
  ganhar(id) { const s = this.itens(); s.add(id); guardado.set('itens', [...s]); salvarConta(); },
  temRoupa(r) { return this.tem('skin:' + r.key, r.preco); },
  temSkinArma(s) { return this.tem(`skin_arma:${s.arma}:${s.key}`, s.preco); },
  nome() { return guardado.get('nome', 'Jogador'); },
  skinArma(arma) { return guardado.get('skin_arma_' + arma, ''); },
  usarSkinArma(arma, key) { guardado.set('skin_arma_' + arma, key || ''); },
  roupa(time) {
    const minhas = ROUPAS.filter((r) => r.time === time);
    return minhas.find((r) => r.key === guardado.get('roupa_' + time, '') && this.temRoupa(r)) || minhas.find((r) => this.temRoupa(r));
  },
  usarRoupa(time, key) { guardado.set('roupa_' + time, key); },
};

// ---------------- Firebase (API REST, sem biblioteca) ----------------
class Firebase {
  constructor(apiKey, dbUrl) { this.key = apiKey; this.db = (dbUrl || '').replace(/\/$/, ''); this.uid = ''; this.email = ''; this.token = null; this.refresh = null; this.expira = 0; this.anonimo = false; }
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
    this.token = j.idToken || j.id_token;
    this.refresh = j.refreshToken || j.refresh_token || this.refresh;
    this.uid = j.localId || j.user_id || this.uid;
    this.email = j.email || this.email;
    this.expira = Date.now() + Number(j.expiresIn || j.expires_in || 3600) * 1000 - 300000;
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
      this.refresh = refresh;
      this.pegar(await r.json());
      return true;
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
const novoFirebase = () => (CONFIG.firebase && CONFIG.firebase.apiKey ? new Firebase(CONFIG.firebase.apiKey, CONFIG.firebase.databaseURL) : null);

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
// 1º login: a conta recebe o que já está no aparelho; depois o aparelho segue a conta
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

// ---------------- sala do multiplayer (Firebase: salas/<código>) ----------------
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
        for (const [k, v] of Object.entries(dado || {})) {
          if (v && typeof v === 'object') this.jogadores[k] = Object.assign(ev === 'patch' ? (this.jogadores[k] || {}) : {}, v);
          else delete this.jogadores[k];
        }
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
  // o que o motor precisa de cada pessoa: lado, roupa, as duas armas e as skins das armas dela (os outros veem)
  roster() {
    return JSON.stringify(this.lista().map((p) => ({ id: p.id, nome: p.nome || 'Jogador', time: p.time | 0, skin: p.skin | 0,
      arma: p.arma || '', arma2: p.arma2 || 'tt33', skins: p.skins || {} })));
  }
  atualizar(eu) { return fb.patch(`salas/${this.codigo}/jogadores/${fb.uid}`, eu); }
  async comecar() {
    await fb.del(`salas/${this.codigo}/est`);
    await fb.del(`salas/${this.codigo}/ev`);
    return (await fb.patch('salas/' + this.codigo, { estado: 'jogo', inicio: { '.sv': 'timestamp' } })).ok;
  }
  async voltar() {
    if (!this.host) return;
    await fb.del(`salas/${this.codigo}/est`);
    await fb.del(`salas/${this.codigo}/ev`);
    await fb.patch('salas/' + this.codigo, { estado: 'espera' });
  }
  async sair() {
    this.parar();
    if (this.host) await fb.del('salas/' + this.codigo);
    else await fb.del(`salas/${this.codigo}/jogadores/${fb.uid}`);
    this.fechada = true;
  }
  parar() { for (const o of this.ouvintes) o.fechar(); this.ouvintes = []; }
}

// O que vai para a sala sobre mim: lado, roupa, armas iniciais e as skins das minhas armas
function minhaEntrada(time) {
  const r = conta.roupa(time);
  const skins = {};
  for (const a of ARMAS) { const sk = conta.skinArma(a.key); if (sk) skins[a.key] = sk; }
  return { nome: conta.nome(), time, skin: r.i, arma: armaInicial(time), arma2: 'tt33', skins };
}
