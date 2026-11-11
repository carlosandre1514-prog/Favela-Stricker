// Favela Strike — parte social do servidor: chat global (WebSocket /chat) e presentes entre amigos (/api/social/...).
//
// Quem é quem: o token de login do Firebase do jogo (o mesmo do APK, do site no PC e do site no iPhone), conferido pela
// assinatura do Google (loja-nexus.js conferirLogin). Nome, ID e nível vêm do perfil no banco (perfis/<uid>), lidos com o
// próprio login da pessoa: ninguém escolhe o nome que aparece no chat nem fala em nome de outro.
//
// Chat global (texto, JSON):
//   jogo -> servidor  {"k":"auth","token":"…","plataforma":"android|pc|ios"}   primeiro
//   servidor -> jogo  {"k":"ok","eu":{…},"hist":[mensagens]}                   últimas 60 mensagens
//   jogo -> servidor  {"k":"msg","txt":"…"}
//   servidor -> jogo  {"k":"msg","m":{id,nome,jid,nivel,plataforma,txt,em}}    para todos
//   servidor -> jogo  {"k":"apagou","id":…} · {"k":"erro","txt":"…"}
// Proteções: até 200 letras, 1 mensagem a cada 1,5 s e no máximo 6 em 30 s (passou: 5 min calado), a mesma mensagem
// repetida não passa, palavrões viram ***, quem abre muitas conexões fica de fora. Moderadores (variável
// MODERADORES = IDs de 9 dígitos separados por vírgula): /calar ID minutos, /liberar ID, /apagar número-da-mensagem.
//
// Presentes (POST /api/social/presente {para, item, chave}, com Authorization: Bearer <token>):
//   o servidor confere tudo — amizade dos dois lados, item que pode ser presente (web/catalogo.js), quem recebe ainda
//   não tem, saldo de quem dá — tira as moedas de quem dá (gravação condicional: duas compras ao mesmo tempo não
//   gastam o mesmo saldo) e grava o presente para quem recebe. A mesma chave (repetir o pedido, internet caindo) não
//   cobra duas vezes. Precisa de acesso de administrador ao banco: variável FIREBASE_SERVICE_ACCOUNT (o JSON da conta
//   de serviço do projeto, Firebase → Configurações → Contas de serviço → Gerar chave). Sem ela, os presentes ficam
//   desligados (o jogo avisa) — nunca são feitos só pelo celular.
'use strict';
const crypto = require('crypto');
const { conferirLogin, limite } = require('./loja-nexus');
const { presenteavel } = require('../web/catalogo.js');

const PROJECT = process.env.FIREBASE_PROJECT_ID || 'navegador-f06a8';
const DB = (process.env.FIREBASE_DB_URL || `https://${PROJECT}-default-rtdb.firebaseio.com`).replace(/\/$/, '');
const MODERADORES = new Set(String(process.env.MODERADORES || '').split(',').map((s) => s.trim()).filter(Boolean));

// ---------------- banco: com o login da pessoa e como administrador ----------------
async function lerComo(token, caminho) {
  const r = await fetch(`${DB}/${caminho}.json?auth=${encodeURIComponent(token)}`, { signal: AbortSignal.timeout(8000) });
  return r.ok ? r.json() : null;
}
let admin = { token: '', ate: 0 };
const contaServico = (() => { try { return process.env.FIREBASE_SERVICE_ACCOUNT ? JSON.parse(process.env.FIREBASE_SERVICE_ACCOUNT) : null; } catch { return null; } })();
async function tokenAdmin() {
  if (!contaServico) return '';
  if (admin.token && Date.now() < admin.ate) return admin.token;
  const agora = Math.floor(Date.now() / 1000);
  const b64 = (o) => Buffer.from(JSON.stringify(o)).toString('base64url');
  const corpo = b64({ alg: 'RS256', typ: 'JWT' }) + '.' + b64({ iss: contaServico.client_email, aud: 'https://oauth2.googleapis.com/token', iat: agora, exp: agora + 3600,
    scope: 'https://www.googleapis.com/auth/firebase.database https://www.googleapis.com/auth/userinfo.email' });
  const assinatura = crypto.createSign('RSA-SHA256').update(corpo).sign(contaServico.private_key, 'base64url');
  const r = await fetch('https://oauth2.googleapis.com/token', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&assertion=' + corpo + '.' + assinatura, signal: AbortSignal.timeout(10000) });
  const j = await r.json();
  if (!j.access_token) throw new Error('conta de serviço recusada');
  admin = { token: j.access_token, ate: Date.now() + (Number(j.expires_in || 3600) - 300) * 1000 };
  return admin.token;
}
async function bancoAdmin(metodo, caminho, corpo, cabecalhos = {}) {
  const t = await tokenAdmin();
  const r = await fetch(`${DB}/${caminho}.json?access_token=${encodeURIComponent(t)}`, {
    method: metodo, headers: { ...(corpo !== undefined ? { 'Content-Type': 'application/json' } : {}), ...cabecalhos },
    body: corpo !== undefined ? JSON.stringify(corpo) : undefined, signal: AbortSignal.timeout(10000) });
  let dado = null;
  try { dado = await r.json(); } catch {}
  return { ok: r.ok, status: r.status, dado, etag: r.headers.get('ETag') || '' };
}

// ---------------- presentes ----------------
function responder(res, status, corpo) {
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(JSON.stringify(corpo));
}
function lerCorpo(req) {
  return new Promise((ok) => { let s = ''; req.on('data', (c) => { s += c; if (s.length > 4096) req.destroy(); }); req.on('end', () => { try { ok(JSON.parse(s || '{}')); } catch { ok({}); } }); });
}
const chaveItem = (item) => item.replace(/:/g, '|');  // ':' não pode em chave do Firebase (conta.js contaJson)
const emAndamento = new Set();

async function presente(eu, corpo) {
  const para = String(corpo.para || ''), item = String(corpo.item || ''), chave = String(corpo.chave || '');
  if (!/^[A-Za-z0-9]{10,40}$/.test(para) || para === eu.uid) return [400, { erro: 'amigo inválido' }];
  if (!/^[a-z0-9-]{8,40}$/.test(chave)) return [400, { erro: 'pedido inválido' }];
  const skin = presenteavel(item);
  if (!skin) return [400, { erro: 'esse item não pode ser presente' }];
  const log = `presentes_log/${eu.uid}_${chave}`;
  const ja = await bancoAdmin('GET', log);
  if (ja.ok && ja.dado) return [200, { ok: true, repetido: true, id: ja.dado.id, moedas: ja.dado.moedas }];  // repetiu: não cobra de novo
  const trava = eu.uid;
  if (emAndamento.has(trava)) return [429, { erro: 'aguarde o presente anterior terminar' }];
  emAndamento.add(trava);
  try {
    const [a, b] = await Promise.all([bancoAdmin('GET', `amigos/${eu.uid}/${para}`), bancoAdmin('GET', `amigos/${para}/${eu.uid}`)]);
    if (!a.ok || !b.ok) return [502, { erro: 'banco indisponível, tente de novo' }];
    if (!a.dado || !b.dado) return [403, { erro: 'só dá para mandar presente para amigos' }];
    const k = chaveItem(item);
    const [tem, pendentes, perfilPara, perfilEu] = await Promise.all([bancoAdmin('GET', `usuarios/${para}/itens/${k}`),
      bancoAdmin('GET', `presentes/${para}`), bancoAdmin('GET', `perfis/${para}`), bancoAdmin('GET', `perfis/${eu.uid}`)]);
    if (tem.dado) return [409, { erro: 'seu amigo já tem essa skin' }];
    if (Object.values(pendentes.dado || {}).some((p) => p && p.item === item && p.estado === 'pendente')) return [409, { erro: 'seu amigo já tem essa skin esperando para abrir' }];
    // tira as moedas de quem dá (gravação condicional; tenta de novo se o saldo mudou no meio)
    let moedas = null;
    for (let t = 0; t < 4 && moedas === null; t++) {
      const m = await bancoAdmin('GET', `usuarios/${eu.uid}/moedas`, undefined, { 'X-Firebase-ETag': 'true' });
      const saldo = Number(m.dado) || 0;
      if (saldo < skin.preco) return [402, { erro: `moedas insuficientes (precisa de ${skin.preco})`, moedas: saldo }];
      const w = await bancoAdmin('PUT', `usuarios/${eu.uid}/moedas`, saldo - skin.preco, { 'if-match': m.etag });
      if (w.ok) moedas = saldo - skin.preco;
      else if (w.status !== 412) return [502, { erro: 'banco indisponível, tente de novo' }];
    }
    if (moedas === null) return [409, { erro: 'o saldo mudou, tente de novo' }];
    const id = Date.now().toString(36) + crypto.randomBytes(4).toString('hex');
    const agora = { '.sv': 'timestamp' };
    const pe = perfilEu.dado || {}, pp = perfilPara.dado || {};
    const r = await bancoAdmin('PATCH', '', {
      [`presentes/${para}/${id}`]: { de: eu.uid, deNome: String(pe.nome || 'Amigo').slice(0, 16), deId: String(pe.id || ''), item, nome: skin.nome, raridade: skin.raridade, preco: skin.preco, em: agora, estado: 'pendente' },
      [`enviados/${eu.uid}/${id}`]: { para, paraNome: String(pp.nome || 'Amigo').slice(0, 16), item, nome: skin.nome, preco: skin.preco, em: agora },
      [log]: { id, moedas, em: agora },
    });
    if (!r.ok) {  // não gravou o presente: devolve as moedas
      await bancoAdmin('PATCH', `usuarios/${eu.uid}`, { moedas: { '.sv': { increment: skin.preco } } });
      return [502, { erro: 'não consegui entregar, suas moedas voltaram' }];
    }
    return [200, { ok: true, id, moedas }];
  } finally { emAndamento.delete(trava); }
}

async function tratar(req, res, url) {
  if (!url.pathname.startsWith('/api/social/')) return false;
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Headers', 'Authorization, Content-Type');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  if (req.method === 'OPTIONS') { res.writeHead(204); res.end(); return true; }
  const eu = await conferirLogin(req);
  if (!eu) return responder(res, 401, { erro: 'faça login' }), true;
  if (!limite('social:' + eu.uid, 15)) return responder(res, 429, { erro: 'muitas tentativas, aguarde um minuto' }), true;
  try {
    if (req.method === 'GET' && url.pathname === '/api/social/estado') return responder(res, 200, { presentes: !!contaServico }), true;
    if (req.method === 'POST' && url.pathname === '/api/social/presente') {
      if (!contaServico) return responder(res, 503, { erro: 'presentes desligados no servidor (falta FIREBASE_SERVICE_ACCOUNT)' }), true;
      const [st, corpo] = await presente(eu, await lerCorpo(req));
      return responder(res, st, corpo), true;
    }
    responder(res, 404, { erro: 'rota não encontrada' });
  } catch (e) {
    console.error('social:', e && e.message);
    responder(res, 502, { erro: 'servidor ocupado, tente de novo' });
  }
  return true;
}

// ---------------- chat global ----------------
const HIST = 60, MAX_TXT = 200, INTERVALO_MS = 1500, JANELA_MS = 30000, MAX_JANELA = 6, CALA_MS = 5 * 60000, MAX_CHAT = 3000;
const historico = [];
let proxId = 1;
const calados = new Map();   // ID do jogador (ou uid) -> até quando
const clientes = new Set();
// palavrões e ofensas mais comuns (a palavra inteira, com ou sem acento); o resto o moderador resolve
const PALAVROES = ['porra', 'caralho', 'buceta', 'puta', 'puto', 'viado', 'arrombado', 'cuzao', 'fdp', 'vsf', 'vtnc', 'pqp', 'merda',
  'desgracado', 'otario', 'babaca', 'cacete', 'piranha', 'vagabunda', 'macaco', 'retardado', 'corno', 'xoxota', 'pau no cu'];
const semAcento = (s) => s.normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase();
function limpar(txt) {
  let t = String(txt || '').replace(/[\u0000-\u001f\u007f\u200b-\u200f\u2028-\u202e\ufeff]/g, ' ').replace(/\s+/g, ' ').trim().slice(0, MAX_TXT);
  const base = semAcento(t);
  for (const p of PALAVROES) {
    const re = new RegExp(`(^|[^a-z0-9])(${p.replace(/ /g, '\\s+')})(?=$|[^a-z0-9])`, 'g');
    let m;
    while ((m = re.exec(base))) {
      const ini = m.index + m[1].length, fim = ini + m[2].length;
      t = t.slice(0, ini) + '*'.repeat(fim - ini) + t.slice(fim);
    }
  }
  return t;
}
function enviarTodos(obj) {
  const s = JSON.stringify(obj);
  for (const c of clientes) if (c.ws.readyState === 1 && c.ws.bufferedAmount < 256 * 1024) c.ws.send(s);
}

function ligarChat(wss) {
  wss.on('connection', (ws) => {
    if (clientes.size >= MAX_CHAT) { ws.close(4004, 'chat cheio'); return; }
    const c = { ws, eu: null, envios: [], ultimoTxt: '', ultimoEm: 0 };
    clientes.add(c);
    const fechar = setTimeout(() => { if (!c.eu) ws.close(4001, 'sem login'); }, 15000);
    ws.vivo = true;
    ws.on('pong', () => { ws.vivo = true; });
    ws.on('message', async (dado, binario) => {
      if (binario || dado.length > 4096) return;
      let j;
      try { j = JSON.parse(dado.toString()); } catch { return; }
      try {
        if (j.k === 'auth' && !c.eu) {
          const eu = await conferirLogin({ headers: { authorization: 'Bearer ' + String(j.token || '') } });
          if (!eu) { ws.close(4001, 'login inválido'); return; }
          const p = (await lerComo(String(j.token), 'perfis/' + eu.uid).catch(() => null)) || {};
          const plataforma = ['android', 'pc', 'ios'].includes(j.plataforma) ? j.plataforma : 'pc';
          c.eu = { uid: eu.uid, nome: String(p.nome || eu.nome || 'Jogador').slice(0, 16), jid: String(p.id || ''), nivel: p.nivel | 0 || 1, plataforma };
          clearTimeout(fechar);
          ws.send(JSON.stringify({ k: 'ok', eu: { nome: c.eu.nome, jid: c.eu.jid, mod: MODERADORES.has(c.eu.jid) }, hist: historico }));
          return;
        }
        if (j.k !== 'msg' || !c.eu) return;
        const agora = Date.now();
        const quem = c.eu.jid || c.eu.uid;
        const cal = calados.get(quem);
        if (cal && agora < cal) { ws.send(JSON.stringify({ k: 'erro', txt: `Você está sem poder falar por ${Math.ceil((cal - agora) / 60000)} min` })); return; }
        const bruto = String(j.txt || '');
        // comandos de moderador
        if (bruto.startsWith('/') && MODERADORES.has(c.eu.jid)) {
          const [cmd, alvo, min] = bruto.trim().split(/\s+/);
          if (cmd === '/calar' && alvo) { calados.set(alvo.replace(/\D/g, ''), agora + Math.min(1440, Math.max(1, +min || 10)) * 60000); ws.send(JSON.stringify({ k: 'erro', txt: `ID ${alvo} calado` })); }
          else if (cmd === '/liberar' && alvo) { calados.delete(alvo.replace(/\D/g, '')); ws.send(JSON.stringify({ k: 'erro', txt: `ID ${alvo} liberado` })); }
          else if (cmd === '/apagar' && alvo) {
            const i = historico.findIndex((m) => m.id === +alvo);
            if (i >= 0) historico.splice(i, 1);
            enviarTodos({ k: 'apagou', id: +alvo });
          }
          return;
        }
        const txt = limpar(bruto);
        if (!txt) return;
        if (agora - c.ultimoEm < INTERVALO_MS) { ws.send(JSON.stringify({ k: 'erro', txt: 'Devagar: espere um pouco entre as mensagens' })); return; }
        c.envios = c.envios.filter((t) => agora - t < JANELA_MS);
        if (c.envios.length >= MAX_JANELA) { calados.set(quem, agora + CALA_MS); ws.send(JSON.stringify({ k: 'erro', txt: 'Muitas mensagens seguidas: 5 min sem falar' })); return; }
        if (semAcento(txt) === semAcento(c.ultimoTxt) && agora - c.ultimoEm < 20000) { ws.send(JSON.stringify({ k: 'erro', txt: 'Mensagem repetida' })); return; }
        c.envios.push(agora); c.ultimoEm = agora; c.ultimoTxt = txt;
        const m = { id: proxId++, nome: c.eu.nome, jid: c.eu.jid, nivel: c.eu.nivel, plataforma: c.eu.plataforma, txt, em: agora };
        historico.push(m);
        if (historico.length > HIST) historico.shift();
        enviarTodos({ k: 'msg', m });
      } catch (e) { console.error('chat:', e && e.message); }
    });
    ws.on('close', () => { clearTimeout(fechar); clientes.delete(c); });
    ws.on('error', () => {});
  });
  setInterval(() => {
    for (const ws of wss.clients) { if (!ws.vivo) { ws.terminate(); continue; } ws.vivo = false; try { ws.ping(); } catch {} }
    const t = Date.now();
    for (const [k, ate] of calados) if (t > ate) calados.delete(k);
  }, 20000).unref();
}

module.exports = { tratar, ligarChat, limpar };
