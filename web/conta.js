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

// Roupas (skins de personagem): os personagens da Meshy (motor: jogo/Partida.cpp kSkins, na mesma ordem). Todas
// gratuitas. Polícia só usa farda; bandido só roupa de bandido.
const ROUPAS = [
  { key: 'soldado', nome: 'Soldado', time: 0, i: 0, preco: 0, raridade: 'comum' },
  { key: 'pm', nome: 'PM', time: 0, i: 1, preco: 0, raridade: 'comum' },
  { key: 'bope', nome: 'BOPE', time: 0, i: 2, preco: 0, raridade: 'comum' },
  { key: 'exercito', nome: 'Selva', time: 0, i: 3, preco: 0, raridade: 'comum' },
  { key: 'choque', nome: 'Choque', time: 0, i: 4, preco: 0, raridade: 'comum' },
  { key: 'criminoso1', nome: 'Malandro', time: 1, i: 0, preco: 0, raridade: 'comum' },
  { key: 'criminoso2', nome: 'Vapor', time: 1, i: 1, preco: 0, raridade: 'comum' },
  { key: 'criminoso3', nome: 'Olheiro', time: 1, i: 2, preco: 0, raridade: 'comum' },
  { key: 'criminoso4', nome: 'Frente', time: 1, i: 3, preco: 0, raridade: 'comum' },
];

// Armas (compradas na partida com o $ dos abates). Os números são os do motor (Partida.cpp kGuns).
// Todas valem para os dois lados: policial pode usar AK e bandido pode usar M4.
const ARMAS = [
  { key: 'tt33', nome: 'TT-33', dano: 25, cadencia: 3, pente: 8, cai: 20, inicial: true },
  { key: 'm4a1', nome: 'M4A1', dano: 24, cadencia: 10, pente: 30, cai: 40, inicial: true },
  { key: 'm4colt', nome: 'M4 Colt', dano: 26, cadencia: 9, pente: 30, cai: 45, luneta: true },
  { key: 'ak47', nome: 'AK-47', dano: 30, cadencia: 8, pente: 30, cai: 35, inicial: true },
  { key: 'ak74', nome: 'AK-74', dano: 27, cadencia: 9, pente: 30, cai: 40 },
  { key: 'ak74_tatica', nome: 'AK-74 tática', dano: 28, cadencia: 9.5, pente: 30, cai: 45 },
];
const armaPorKey = (k) => ARMAS.find((a) => a.key === k) || ARMAS[0];
// Armas para começar a partida (o resto compra na base com o $ dos abates): as grátis (M4A1 e AK-47) e as que têm
// skin equipada (a skin comprada aparece na mão), de qualquer lado
const armaPadrao = (time) => (time === 0 ? 'm4a1' : 'ak47');
const opcoesArmaInicial = () => ARMAS.filter((a) => a.key !== 'tt33' && (a.inicial || conta.skinArma(a.key)));
// a escolhida na tela antes da partida; sem escolha: a que tem skin equipada, ou a padrão do lado
const armaInicial = (time) => {
  const opcoes = opcoesArmaInicial();
  const escolhida = guardado.get('arma_inicial', '');
  if (opcoes.some((a) => a.key === escolhida)) return escolhida;
  const comSkin = opcoes.find((a) => conta.skinArma(a.key));
  return comSkin ? comSkin.key : armaPadrao(time);
};

// Skins de arma (mercado, com moedas): só mudam o visual, não o dano. As com modelo: a arma inteira é outra (modelo
// próprio feito na Meshy, shaders/kit/armas/<arma>_<skin>.glb), com o mesmo dano e o mesmo som da arma
const SKINS_ARMA = [
  { arma: 'ak47', key: 'cosmica', nome: 'AK-47 Energia Cósmica', raridade: 'lendaria', preco: 2200, modelo: true },
  { arma: 'm4a1', key: 'dragao_raio', nome: 'M4A1 Dragão Relâmpago', raridade: 'lendaria', preco: 2200, modelo: true },
  { arma: 'm4a1', key: 'mitica', nome: 'M4A1 Mítica', raridade: 'lendaria', preco: 2000, modelo: true },
  { arma: 'ak47', key: 'dragao_azul', nome: 'AK-47 Dragão Azul', raridade: 'lendaria', preco: 1800 },
  { arma: 'm4colt', key: 'carmesim', nome: 'M4 Colt Vermelho Carmesim', raridade: 'lendaria', preco: 1800 },
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

// Armas giratórias da loja (tools/recortar_giro.py: giro/<arma>_<skin>.webp, quadros lado a lado): tamanho de cada
// quadro. A skin sem tira usa a foto parada (armas/<arma>_<skin>.png).
const GIRO = {"ak47_cosmica":{"altura":138,"largura":400,"quadros":24},"ak47_deserto":{"altura":132,"largura":400,"quadros":24},"ak47_dourada":{"altura":132,"largura":400,"quadros":24},"ak47_dragao":{"altura":132,"largura":400,"quadros":24},"ak47_dragao_azul":{"altura":132,"largura":400,"quadros":24},"ak74_favela":{"altura":115,"largura":400,"quadros":24},"ak74_tatica_selva":{"altura":118,"largura":400,"quadros":24},"m4a1_dragao_raio":{"altura":150,"largura":400,"quadros":24},"m4a1_mitica":{"altura":153,"largura":400,"quadros":24},"m4a1_neon":{"altura":143,"largura":400,"quadros":24},"m4a1_urbana":{"altura":143,"largura":400,"quadros":24},"m4colt_carmesim":{"altura":160,"largura":374,"quadros":24},"m4colt_dourada":{"altura":160,"largura":374,"quadros":24},"tt33_crimson_forge":{"altura":160,"largura":217,"quadros":24},"tt33_gold_relic":{"altura":160,"largura":217,"quadros":24},"tt33_nebula_shadow":{"altura":160,"largura":217,"quadros":24}};

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

// ---------------- nível, XP e patente (como no Call of Duty: o XP da partida sobe o nível; a cada 5 níveis, patente nova) ----------------
const NIVEL_MAX = 55;
// XP para ir do nível n ao n+1: começa em 1.000 e cresce 250 por nível
const xpDoNivel = (n) => 1000 + 250 * (n - 1);
// XP da partida: cada abate, o resultado e ter jogado até o fim
const XP_ABATE = 100, XP_VITORIA = 600, XP_EMPATE = 350, XP_DERROTA = 200;
// Patentes (5 níveis cada; a última no nível máximo). Divisão: I, II, III, IV, V dentro da patente
const PATENTES = [
  { nome: 'Recruta', de: 1, cor: '#B08D57', tipo: 'barra', n: 1 },
  { nome: 'Soldado', de: 5, cor: '#B08D57', tipo: 'divisa', n: 1 },
  { nome: 'Cabo', de: 10, cor: '#B08D57', tipo: 'divisa', n: 2 },
  { nome: '3º Sargento', de: 15, cor: '#C9CED6', tipo: 'divisa', n: 3 },
  { nome: '2º Sargento', de: 20, cor: '#C9CED6', tipo: 'divisa', n: 3, arcos: 1 },
  { nome: '1º Sargento', de: 25, cor: '#C9CED6', tipo: 'divisa', n: 3, arcos: 2 },
  { nome: 'Subtenente', de: 30, cor: '#C9CED6', tipo: 'losango', n: 1 },
  { nome: 'Aspirante', de: 35, cor: '#FBBF24', tipo: 'estrela', n: 1, vazada: true },
  { nome: '2º Tenente', de: 40, cor: '#FBBF24', tipo: 'estrela', n: 1 },
  { nome: '1º Tenente', de: 45, cor: '#FBBF24', tipo: 'estrela', n: 2 },
  { nome: 'Capitão', de: 50, cor: '#FBBF24', tipo: 'estrela', n: 3 },
  { nome: 'Comandante', de: 55, cor: '#FDE68A', tipo: 'estrela', n: 3, louros: true },
];
// Nível a partir do XP total: { nivel, noNivel (XP já feito no nível), precisa (XP do nível), patente, divisao }
function nivelDe(xp) {
  let n = 1, resto = Math.max(0, Math.floor(xp || 0));
  while (n < NIVEL_MAX && resto >= xpDoNivel(n)) { resto -= xpDoNivel(n); n++; }
  const pat = PATENTES.filter((p) => p.de <= n).pop();
  const divisao = n >= NIVEL_MAX ? '' : ['I', 'II', 'III', 'IV', 'V'][n - pat.de] || '';
  return { nivel: n, noNivel: n >= NIVEL_MAX ? 0 : resto, precisa: n >= NIVEL_MAX ? 0 : xpDoNivel(n), patente: pat, divisao };
}
// Insígnia da patente (SVG): barra, divisas (com arcos), losango ou estrelas (com louros no máximo)
function insignia(pat, tam = 28) {
  const c = pat.cor, sombra = 'rgba(0,0,0,.45)';
  let corpo = '';
  if (pat.tipo === 'barra') corpo = `<rect x="7" y="13" width="18" height="6" rx="1.5" fill="${c}" stroke="${sombra}" stroke-width="1"/>`;
  if (pat.tipo === 'divisa') {
    for (let i = 0; i < pat.n; i++) {
      const y = 9 + i * 6;
      corpo += `<path d="M6 ${y + 6} L16 ${y} L26 ${y + 6} L26 ${y + 9.5} L16 ${y + 3.5} L6 ${y + 9.5} Z" fill="${c}" stroke="${sombra}" stroke-width=".8"/>`;
    }
    for (let i = 0; i < (pat.arcos || 0); i++) {
      const y = 9 + pat.n * 6 + 1 + i * 4.5;
      corpo += `<path d="M7 ${y} Q16 ${y + 6} 25 ${y} L25 ${y + 2.6} Q16 ${y + 8.6} 7 ${y + 2.6} Z" fill="${c}" stroke="${sombra}" stroke-width=".8"/>`;
    }
  }
  if (pat.tipo === 'losango') corpo = `<path d="M16 5 L26 16 L16 27 L6 16 Z" fill="${c}" stroke="${sombra}" stroke-width="1"/><path d="M16 10 L21 16 L16 22 L11 16 Z" fill="${sombra}"/>`;
  if (pat.tipo === 'estrela') {
    const estrela = (cx, cy, r) => {
      let d = '';
      for (let i = 0; i < 10; i++) { const a = -Math.PI / 2 + i * Math.PI / 5, rr = i % 2 ? r * 0.45 : r; d += (i ? 'L' : 'M') + (cx + rr * Math.cos(a)).toFixed(2) + ' ' + (cy + rr * Math.sin(a)).toFixed(2); }
      return `<path d="${d}Z" fill="${pat.vazada ? 'none' : c}" stroke="${pat.vazada ? c : sombra}" stroke-width="${pat.vazada ? 1.8 : .8}"/>`;
    };
    const pos = pat.n === 1 ? [[16, 15, 9]] : pat.n === 2 ? [[10, 15, 6.5], [22, 15, 6.5]] : [[16, 10, 5.6], [9.5, 20, 5.6], [22.5, 20, 5.6]];
    corpo = pos.map(([x, y, r]) => estrela(x, y, r)).join('');
    if (pat.louros) corpo += `<path d="M5 12 Q4 24 15 28 M27 12 Q28 24 17 28" fill="none" stroke="${c}" stroke-width="2" stroke-linecap="round"/>`;
  }
  return `<svg width="${tam}" height="${tam}" viewBox="0 0 32 32" aria-hidden="true">${corpo}</svg>`;
}

// ---------------- configurações do jogador ----------------
const CONFIG_PADRAO = {
  sens: 1.0,          // câmera (arrastar a tela / mouse)
  sensMira: 0.7,      // mirando (ADS)
  sensLuneta: 0.45,   // olhando pela luneta
  inverterY: false,
  volume: 0.9,        // tiros, passos, explosões
  volumeVoz: 1.0,     // chat de voz do time
  vibrar: true,
  medidor: true,      // FPS, tempo do quadro e ping na partida
};
const config = Object.assign({}, CONFIG_PADRAO, guardado.get('config', {}));
function salvarConfig() { guardado.set('config', config); }

// ---------------- moedas e itens ----------------
// Moedas só entram compradas na loja (site): ninguém ganha moedas de graça, nem ao criar a conta nem jogando
const MOEDAS_INICIAIS = 0;
const conta = {
  get moedas() { return guardado.get('moedas', MOEDAS_INICIAIS); },
  set moedas(v) { guardado.set('moedas', Math.max(0, Math.round(v))); salvarConta(); },
  itens() { return new Set(guardado.get('itens', [])); },
  tem(id, preco) { return preco === 0 || this.itens().has(id); },
  ganhar(id) { const s = this.itens(); s.add(id); guardado.set('itens', [...s]); salvarConta(); },
  temRoupa(r) { return this.tem('skin:' + r.key, r.preco); },
  temSkinArma(s) { return this.tem(`skin_arma:${s.arma}:${s.key}`, s.preco); },
  nome() { return guardado.get('nome', 'Jogador'); },
  get xp() { return guardado.get('xp', 0); },
  // soma XP e devolve como estava e como ficou (para a tela do fim da partida mostrar o nível novo)
  ganharXp(q) { const antes = nivelDe(this.xp); guardado.set('xp', this.xp + Math.max(0, Math.round(q))); salvarConta(); return { antes, depois: nivelDe(this.xp) }; },
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
  async pedir(metodo, caminho, corpo, tentou) {
    const r = await fetch(await this.url(caminho), { method: metodo, headers: corpo !== undefined ? { 'Content-Type': 'application/json' } : {}, body: corpo !== undefined ? JSON.stringify(corpo) : undefined });
    // 401: o login pode ter vencido; renova uma vez e tenta de novo antes de culpar as regras
    if (r.status === 401 && !tentou && this.refresh && await this.restaurar(this.refresh)) return this.pedir(metodo, caminho, corpo, true);
    const texto = await r.text();
    let dado = null;
    try { dado = JSON.parse(texto); } catch (_) {}
    return { ok: r.ok, code: r.status, dado };
  }
  get(c) { return this.pedir('GET', c); }
  // escrita condicional (só se ninguém mudou o valor desde a leitura): devolve true se gravou
  async getComEtag(c) {
    const r = await fetch(await this.url(c), { headers: { 'X-Firebase-ETag': 'true' } });
    let dado = null;
    try { dado = await r.json(); } catch (_) {}
    return { ok: r.ok, dado, etag: r.headers.get('ETag') || '' };
  }
  async putSe(c, v, etag) {
    const r = await fetch(await this.url(c), { method: 'PUT', headers: { 'Content-Type': 'application/json', 'if-match': etag }, body: JSON.stringify(v) });
    return r.ok;
  }
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
  return { nome: conta.nome(), moedas: conta.moedas, xp: conta.xp, itens, atualizado: { '.sv': 'timestamp' } };
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
    if (!r.dado) {  // conta nova: começa do zero (sem moedas, sem itens, sem XP), mesmo num aparelho que já tinha outra conta
      guardado.set('moedas', MOEDAS_INICIAIS);
      guardado.set('itens', []);
      guardado.set('xp', 0);
      await fb.put('usuarios/' + fb.uid, contaJson());
      return;
    }
    guardado.set('moedas', r.dado.moedas ?? conta.moedas);
    guardado.set('xp', Math.max(conta.xp, r.dado.xp | 0));  // XP só sobe (não perde o que foi feito sem internet)
    guardado.set('itens', Object.keys(r.dado.itens || {}).map((k) => k.replace(/\|/g, ':')));
    if (r.dado.nome) guardado.set('nome', r.dado.nome);
  } catch (_) {}
}

// ---------------- sala do multiplayer (Firebase: salas/<código>) ----------------
const LETRAS = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';
class Sala {
  // modo: 'times' (Polícia x Bandidos, até 10) ou 'br' (Ressurgência, até 40, só gente)
  constructor(codigo, host, modo = 'times', publica = false, formato = 'squad') {
    this.codigo = codigo; this.host = host; this.modo = modo; this.publica = publica; this.formato = formato;
    this.jogadores = {}; this.estado = 'espera'; this.fechada = false; this.ouvintes = []; this.comecaEm = 0;
  }
  get max() { return this.modo === 'br' ? Sala.maxBR(this.formato) : 10; }
  static tamanho(formato) { return { solo: 1, duo: 2, trio: 3, squad: 4 }[formato] || 4; }
  static maxBR(formato) { const t = Sala.tamanho(formato); return Math.floor(40 / t) * t; }  // trio: 39
  // 401/403: o banco (Realtime Database) recusou pelas regras; o resto é rede ou endereço do banco
  static erroBanco(code) {
    if (code === 401 || code === 403) return 'O banco do Firebase recusou (' + code + '): no Firebase, abra Realtime Database → Regras e cole as regras do README (salas e usuarios para quem está logado).';
    if (code === 404) return 'Banco não encontrado (404): confira o databaseURL em web/config_jogo.js.';
    return 'o Firebase recusou (' + code + ')';
  }
  static novoCodigo() { let s = ''; for (let i = 0; i < 6; i++) s += LETRAS[Math.floor(Math.random() * LETRAS.length)]; return s; }
  static async criar(eu, modo = 'times', publica = false, formato = 'squad') {
    for (let k = 0; k < 6; k++) {
      const c = Sala.novoCodigo();
      const g = await fb.get('salas/' + c);
      if (!g.ok) return [null, Sala.erroBanco(g.code)];
      if (g.dado !== null) continue;
      const r = await fb.put('salas/' + c, { host: fb.uid, estado: 'espera', modo, publica, formato, criada: { '.sv': 'timestamp' },
        jogadores: { [fb.uid]: { ...eu, entrou: { '.sv': 'timestamp' } } } });
      if (!r.ok) return [null, `não consegui criar a sala (${r.code})`];
      return [new Sala(c, true, modo, publica, formato), ''];
    }
    return [null, 'não achei um código livre, tente de novo'];
  }
  static async entrar(codigo, eu, vagasPrecisas = 1) {
    const c = codigo.trim().toUpperCase();
    const g = await fb.get('salas/' + c);
    if (!g.ok) return [null, Sala.erroBanco(g.code)];
    if (!g.dado || !g.dado.estado) return [null, 'não existe sala com o código ' + c];
    if (g.dado.estado !== 'espera') return [null, 'essa sala já está jogando'];
    const modo = g.dado.modo === 'br' ? 'br' : 'times', formato = g.dado.formato || 'squad';
    const max = modo === 'br' ? Sala.maxBR(formato) : 10;
    const js = g.dado.jogadores || {};
    if (Object.keys(js).length + vagasPrecisas > max && !js[fb.uid]) return [null, `a sala está cheia (${max} pessoas)`];
    const r = await fb.put(`salas/${c}/jogadores/${fb.uid}`, { ...eu, entrou: { '.sv': 'timestamp' } });
    if (!r.ok) return [null, `não consegui entrar (${r.code})`];
    return [new Sala(c, g.dado.host === fb.uid, modo, !!g.dado.publica, formato), ''];
  }
  // Ressurgência, partida rápida: entra na sala pública que está esperando gente; se não tem (ou encheu, ou já
  // começou), cria uma e avisa as próximas pessoas (salas/BR_PUBLICA aponta para ela)
  // uma sala pública por formato (solo, duo, trio, squad); o grupo inteiro precisa caber
  static async partidaRapida(eu, formato = 'squad', grupo = 1) {
    const ponteiro = 'salas/BR_PUBLICA_' + formato.toUpperCase();
    const p = await fb.get(ponteiro);
    if (p.ok && p.dado && p.dado.codigo) {
      const [s] = await Sala.entrar(p.dado.codigo, eu, grupo);
      if (s && s.modo === 'br' && s.formato === formato) return [s, ''];
    }
    const [s, erro] = await Sala.criar(eu, 'br', true, formato);
    if (!s) return [null, erro];
    await fb.put(ponteiro, { codigo: s.codigo, criada: { '.sv': 'timestamp' } });
    return [s, ''];
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
    this.ouvintes.push(fb.ouvir(`salas/${this.codigo}/host`, (_ev, _sub, dado) => { if (dado && fb && dado === fb.uid && !this.host) { this.host = true; aoMudar(); } }));
    if (this.modo === 'br')  // sala pública: quando começa (contagem de quem criou)
      this.ouvintes.push(fb.ouvir(`salas/${this.codigo}/comeca_em`, (_ev, _sub, dado) => { this.comecaEm = Number(dado) || 0; aoMudar(); }));
  }
  lista() { return Object.entries(this.jogadores).map(([id, p]) => ({ id, ...p })); }
  // o que o motor precisa de cada pessoa: lado, roupa, as duas armas e as skins das armas dela (os outros veem)
  roster() {
    return JSON.stringify(this.lista().map((p) => ({ id: p.id, nome: p.nome || 'Jogador', time: p.time | 0, skin: p.skin | 0,
      arma: p.arma || '', arma2: p.arma2 || 'tt33', skins: p.skins || {} })));
  }
  atualizar(eu) { return fb.patch(`salas/${this.codigo}/jogadores/${fb.uid}`, eu); }
  // Ressurgência: quem criou a sala monta os times do formato (solo 1, duo 2, trio 3, squad 4): cada grupo de amigos
  // fica junto no mesmo time; quem veio sozinho completa os times pela ordem de chegada. Sorteia a semente (zona e
  // pontos de queda iguais em todos os celulares). Precisa de pelo menos 2 times.
  static montarTimes(lista, tam) {
    const l = lista.slice().sort((a, b) => (Number(a.entrou) || 0) - (Number(b.entrou) || 0) || (a.id < b.id ? -1 : 1));
    const grupos = new Map();
    for (const p of l) { const k = p.grupo || 'sozinho:' + p.id; if (!grupos.has(k)) grupos.set(k, []); grupos.get(k).push(p); }
    const pedacos = [];
    for (const g of grupos.values()) for (let i = 0; i < g.length; i += tam) pedacos.push(g.slice(i, i + tam));
    pedacos.sort((a, b) => b.length - a.length);
    const times = [];
    for (const g of pedacos) {
      const t = times.find((x) => x.length + g.length <= tam);
      if (t) t.push(...g); else times.push([...g]);
    }
    // um time só (ex.: 3 amigos sozinhos num squad): divide para ter adversário
    if (times.length === 1 && times[0].length > 1) times.push(times[0].splice(Math.ceil(times[0].length / 2)));
    const mapa = {};
    times.forEach((t, i) => t.forEach((p) => { mapa[p.id] = i; }));
    return [mapa, times.length];
  }
  async comecarBR() {
    const l = this.lista();
    const tam = Sala.tamanho(this.formato);
    const [times, nt] = Sala.montarTimes(l, tam);
    if (l.length < 2 || nt < 2) return false;
    // só um celular começa (o de quem criou ou, se ele sumiu, outro da sala): "espera" -> "comecando" só se ninguém
    // mudou antes; quem perde não sorteia de novo (sortear duas vezes deixava cada celular com times diferentes)
    const e = await fb.getComEtag(`salas/${this.codigo}/estado`);
    if (!e.ok || e.dado !== 'espera' || !(await fb.putSe(`salas/${this.codigo}/estado`, 'comecando', e.etag))) return true;
    const semente = 1 + Math.floor(Math.random() * 2e9);
    await fb.del(`salas/${this.codigo}/est`);
    await fb.del(`salas/${this.codigo}/ev`);
    if (this.publica) {  // a sala pública começou: as próximas pessoas vão para outra
      const ponteiro = 'salas/BR_PUBLICA_' + this.formato.toUpperCase();
      const p = await fb.get(ponteiro);
      if (p.ok && p.dado && p.dado.codigo === this.codigo) await fb.del(ponteiro);
    }
    return (await fb.patch('salas/' + this.codigo, { estado: 'jogo', inicio: { '.sv': 'timestamp' }, times, semente, tamanho: tam })).ok;
  }
  // o que o motor precisa na Ressurgência: semente e cada pessoa com o time, o lado da roupa, roupa, armas e skins
  async rosterBR() {
    const g = await fb.get('salas/' + this.codigo);
    const d = (g.ok && g.dado) || {};
    const times = d.times || {};
    const jogadores = Object.entries(d.jogadores || this.jogadores).map(([id, p]) => ({ id, nome: p.nome || 'Jogador', time: times[id] | 0,
      lado: p.time | 0, skin: p.skin | 0, arma: p.arma || '', arma2: p.arma2 || 'tt33', skins: p.skins || {} }));
    return { semente: d.semente | 0 || 1, tamanho: d.tamanho | 0 || 4, jogadores, meuTime: times[fb.uid] | 0 };
  }
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
    // Partida rolando ou sala com mais gente: quem criou sai só ele (antes apagava a sala e a partida de todo mundo
    // acabava junto, por ex. quando o time de quem criou caía na Ressurgência). Na espera, a sala passa para o próximo.
    const outros = this.lista().filter((p) => p.id !== fb.uid).sort((a, b) => (Number(a.entrou) || 0) - (Number(b.entrou) || 0));
    if (this.host && (this.estado !== 'espera' || (this.modo === 'br' && outros.length))) {
      await fb.del(`salas/${this.codigo}/jogadores/${fb.uid}`);
      const g = await fb.get(`salas/${this.codigo}/jogadores`);
      const resto = Object.keys((g.ok && g.dado) || {});
      if (!resto.length) await fb.del('salas/' + this.codigo);  // era o último: limpa
      else if (this.estado === 'espera' && outros.length) await fb.patch('salas/' + this.codigo, { host: outros[0].id });
      this.fechada = true;
      return;
    }
    if (this.host) {
      await fb.del('salas/' + this.codigo);
      if (this.publica) {
        const ponteiro = 'salas/BR_PUBLICA_' + this.formato.toUpperCase();
        const p = await fb.get(ponteiro);
        if (p.ok && p.dado && p.dado.codigo === this.codigo) await fb.del(ponteiro);
      }
    } else {
      await fb.del(`salas/${this.codigo}/jogadores/${fb.uid}`);
      if (this.estado !== 'espera') {  // fim da partida: o último a sair apaga a sala
        const g = await fb.get(`salas/${this.codigo}/jogadores`);
        if (g.ok && !g.dado) await fb.del('salas/' + this.codigo);
      }
    }
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
