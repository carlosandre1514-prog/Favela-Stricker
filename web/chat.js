// Favela Strike — conversas com amigos, chat global e presentes. Igual no APK, no site do PC e no site do iPhone.
//
// Conversa com amigo (Firebase, ao vivo e com histórico; só amigos — as regras do banco conferem, veja o README):
//   conversas/<uidA_uidB>/<id>  = {de, txt, em}          (uids em ordem: a mesma conversa para os dois)
//   caixa/<uid>/<amigo>         = {nome, txt, em, nao}    a lista de conversas de cada um (nao: não lidas)
// Chat global: WebSocket /chat do servidor do jogo (servidor/social.js: nome vem da conta, limite de mensagens,
//   palavrões tapados, moderação). Só fica ligado com a tela do chat aberta (sem tráfego à toa).
// Presentes: o servidor confere e transfere (POST /api/social/presente); chegam em presentes/<uid> e quem recebe abre.
'use strict';

const chat = {
  caixa: {},          // amigo -> {nome, txt, em, nao}
  presentes: {},      // id -> presente recebido
  ouvintes: [],
  aberta: '',         // conversa aberta agora (amigo)
  ws: null,
  global: [],
  presentesLigados: null,  // o servidor faz presentes? (null: ainda não perguntou)
};
const idConversa = (a, b) => [a, b].sort().join('_');
const MAX_MSG = 300;

// ---------------- ao entrar na conta: caixa de conversas e presentes ao vivo ----------------
function ligarChatEPresentes() {
  if (!fb || !fb.token || !social.pronto) return;
  for (const o of chat.ouvintes) o.fechar();
  chat.ouvintes = [];
  const juntar = (alvo, aoNovo) => (ev, sub, dado) => {
    if (sub === '/') {
      if (ev === 'put') for (const k of Object.keys(alvo)) delete alvo[k];
      for (const [k, v] of Object.entries(dado || {})) { if (v) alvo[k] = ev === 'patch' ? Object.assign(alvo[k] || {}, v) : v; else delete alvo[k]; }
    } else {
      const [k, campo] = sub.slice(1).split('/');
      if (campo) { if (alvo[k]) alvo[k][campo] = dado; }
      else if (dado) { const novo = !alvo[k]; alvo[k] = ev === 'patch' ? Object.assign(alvo[k] || {}, dado) : dado; if (novo && aoNovo) aoNovo(k, alvo[k]); }
      else delete alvo[k];
    }
    atualizarBadgesSociais();
    if (social.aoMudar) social.aoMudar();
  };
  let primeiraCaixa = true;
  chat.ouvintes.push(fb.ouvir('caixa/' + fb.uid, (ev, sub, dado) => {
    const antes = JSON.stringify(chat.caixa);
    juntar(chat.caixa)(ev, sub, dado);
    // mensagem nova de um amigo (fora da conversa aberta): aviso
    if (!primeiraCaixa && sub !== '/' && dado && antes !== JSON.stringify(chat.caixa)) {
      const de = sub.slice(1).split('/')[0], c = chat.caixa[de];
      if (c && c.nao && chat.aberta !== de && !(typeof partida !== 'undefined' && partida)) aviso(`${c.nome}: ${c.txt}`);
    }
    primeiraCaixa = false;
  }));
  chat.ouvintes.push(fb.ouvir('presentes/' + fb.uid, juntar(chat.presentes, (id, p) => {
    if (p.estado === 'pendente' && !(typeof partida !== 'undefined' && partida)) aviso(`🎁 ${p.deNome} mandou um presente: ${p.nome}`, 4000);
  })));
}
function naoLidas() { return Object.values(chat.caixa).reduce((n, c) => n + (c && c.nao ? Number(c.nao) || 0 : 0), 0); }
function presentesPendentes() { return Object.values(chat.presentes).filter((p) => p && p.estado === 'pendente').length; }
// o número no botão Amigos: pedidos de amizade + mensagens não lidas + presentes para abrir
function atualizarBadgesSociais() {
  const n = Object.keys(social.pedidos).length + naoLidas() + presentesPendentes();
  for (const b of $$('.badge-amigos')) { b.textContent = n; b.style.display = n ? '' : 'none'; }
}

// ---------------- conversa com um amigo ----------------
async function enviarMensagem(amigo, nomeAmigo, txt) {
  txt = String(txt || '').replace(/\s+/g, ' ').trim().slice(0, MAX_MSG);
  if (!txt) return '';
  const agora = Date.now();
  if (agora - (chat.ultimoEnvio || 0) < 700) return 'Devagar';
  chat.ultimoEnvio = agora;
  const em = { '.sv': 'timestamp' };
  const r = await fb.post('conversas/' + idConversa(fb.uid, amigo), { de: fb.uid, txt, em });
  if (!r.ok) return r.code === 401 || r.code === 403
    ? 'O Firebase recusou a mensagem: os dois precisam ser amigos, e as regras do banco (docs/regras_firebase.json) precisam estar publicadas no Firebase'
    : 'Sem internet';
  // a caixa do amigo (com +1 não lida) e a minha (para a lista de conversas)
  fb.patch(`caixa/${amigo}/${fb.uid}`, { nome: conta.nome(), txt, em, nao: { '.sv': { increment: 1 } } }).catch(() => {});
  fb.patch(`caixa/${fb.uid}/${amigo}`, { nome: nomeAmigo, txt, em, nao: 0 }).catch(() => {});
  return '';
}
function marcarLida(amigo) {
  if (chat.caixa[amigo] && chat.caixa[amigo].nao) fb.patch(`caixa/${fb.uid}/${amigo}`, { nao: 0 }).catch(() => {});
}

async function telaConversa(p) {
  const amigo = p.uid, nome = p.nome || 'Amigo';
  chat.aberta = amigo;
  marcarLida(amigo);
  const el = html(`<div class="camada fixo"><div class="janela janela-chat">
    <div class="linha"><span class="ponto-on ${estaOnline(p) ? 'on' : ''}"></span><span class="t cresce">${esc(nome)}</span>
      <span class="dica">${estaOnline(p) ? (p.jogando ? 'jogando' : 'online') : 'offline'}${p.plataforma ? ' · ' + esc(nomePlataforma(p.plataforma)) : ''}</span>
      <button class="btn btn-fraco" id="cv-fechar" aria-label="Fechar">✕</button></div>
    <div class="msgs" id="cv-msgs"><span class="dica">Carregando…</span></div>
    <div class="linha"><input type="text" id="cv-txt" maxlength="${MAX_MSG}" placeholder="Mensagem" class="cresce" autocomplete="off"><button class="btn btn-ouro" id="cv-enviar">Enviar</button></div>
  </div></div>`);
  palco.appendChild(el);
  const lista = $('#cv-msgs', el), msgs = {};
  const desenhar = () => {
    const ord = Object.entries(msgs).sort((a, b) => (a[1].em || 0) - (b[1].em || 0) || (a[0] < b[0] ? -1 : 1));
    lista.innerHTML = ord.length ? ord.map(([, m]) => `<div class="msg ${m.de === fb.uid ? 'minha' : ''}"><span>${esc(m.txt)}</span><i>${hora(m.em)}</i></div>`).join('')
      : '<span class="dica">Nenhuma mensagem ainda. Mande um oi!</span>';
    lista.scrollTop = lista.scrollHeight;
  };
  const ouvinte = fb.ouvir('conversas/' + idConversa(fb.uid, amigo), (ev, sub, dado) => {
    if (sub === '/') { if (ev === 'put') for (const k of Object.keys(msgs)) delete msgs[k]; Object.assign(msgs, dado || {}); }
    else if (dado) msgs[sub.slice(1).split('/')[0]] = dado;
    desenhar();
    marcarLida(amigo);
  }, '&orderBy=%22%24key%22&limitToLast=60');
  const fechar = () => { ouvinte.fechar(); chat.aberta = ''; el.remove(); if (telaAtual() === 'amigos') telaAmigos('conversas'); };
  $('#cv-fechar', el).onclick = fechar;
  const txt = $('#cv-txt', el);
  const mandar = async () => {
    const erro = await enviarMensagem(amigo, nome, txt.value);
    if (erro) aviso(erro); else txt.value = '';
  };
  $('#cv-enviar', el).onclick = mandar;
  txt.addEventListener('keydown', (e) => { if (e.key === 'Enter') mandar(); });
  setTimeout(() => txt.focus(), 50);
}
const hora = (t) => { if (!t) return ''; const d = new Date(Number(t)); return d.toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' }); };
const nomePlataforma = (p) => ({ android: 'Android', pc: 'PC', ios: 'iPhone' }[p] || '');

// ---------------- chat global ----------------
function telaChatGlobal() {
  const el = html(`<div class="camada fixo"><div class="janela janela-chat">
    <div class="linha"><span class="t cresce">Chat global</span><span class="dica" id="cg-status">conectando…</span>
      <button class="btn btn-fraco" id="cg-fechar" aria-label="Fechar">✕</button></div>
    <div class="msgs" id="cg-msgs"></div>
    <div class="linha"><input type="text" id="cg-txt" maxlength="200" placeholder="Mensagem para todo mundo" class="cresce" autocomplete="off"><button class="btn btn-ouro" id="cg-enviar">Enviar</button></div>
    <span class="dica" style="font-size:10px">Respeito: palavrões são tapados, flood deixa 5 min sem falar. Jogadores de Android, PC e iPhone.</span>
  </div></div>`);
  palco.appendChild(el);
  const lista = $('#cg-msgs', el), st = $('#cg-status', el);
  const desenhar = () => {
    lista.innerHTML = chat.global.map((m) => `<div class="msg-g" data-id="${m.id}"><b style="color:${corNivel(m.nivel)}">${esc(m.nome)}</b><span class="dica"> NV ${m.nivel | 0}${m.plataforma ? ' · ' + nomePlataforma(m.plataforma) : ''} · ${hora(m.em)}</span><br>${esc(m.txt)}</div>`).join('')
      || '<span class="dica">Ninguém falou ainda. Comece a conversa!</span>';
    lista.scrollTop = lista.scrollHeight;
  };
  let fechado = false, tentativas = 0;
  const conectar = async () => {
    if (fechado) return;
    if (!fb || !fb.token) { st.textContent = 'entre na sua conta'; return; }
    const ws = new WebSocket(SERVIDOR.replace(/^http/, 'ws') + '/chat');
    chat.ws = ws;
    ws.onopen = async () => ws.send(JSON.stringify({ k: 'auth', token: await fb.tokenValido(), plataforma: PLATAFORMA }));
    ws.onmessage = (ev) => {
      let j; try { j = JSON.parse(ev.data); } catch (_) { return; }
      if (j.k === 'ok') { tentativas = 0; st.textContent = 'ao vivo'; st.style.color = '#86EFAC'; chat.global = j.hist || []; desenhar(); }
      else if (j.k === 'msg') { chat.global.push(j.m); if (chat.global.length > 60) chat.global.shift(); desenhar(); }
      else if (j.k === 'apagou') { chat.global = chat.global.filter((m) => m.id !== j.id); desenhar(); }
      else if (j.k === 'erro') aviso(j.txt);
    };
    ws.onclose = (ev) => {
      if (fechado) return;
      st.textContent = ev.code === 4004 ? 'chat cheio, tentando de novo…' : 'reconectando…'; st.style.color = '#FCD34D';
      if (++tentativas <= 8) setTimeout(conectar, Math.min(8000, 1000 * tentativas));
    };
  };
  acordarServidor();
  conectar();
  desenhar();
  $('#cg-fechar', el).onclick = () => { fechado = true; if (chat.ws) chat.ws.close(1000); chat.ws = null; el.remove(); };
  const txt = $('#cg-txt', el);
  const mandar = () => {
    const t = txt.value.trim();
    if (!t || !chat.ws || chat.ws.readyState !== 1) return;
    chat.ws.send(JSON.stringify({ k: 'msg', txt: t }));
    txt.value = '';
  };
  $('#cg-enviar', el).onclick = mandar;
  txt.addEventListener('keydown', (e) => { if (e.key === 'Enter') mandar(); });
}
const corNivel = (n) => (n >= 35 ? '#FBBF24' : n >= 15 ? '#C9CED6' : '#D6A86A');

// ---------------- presentes ----------------
async function socialApi(metodo, rota, corpo) {
  try {
    const t = await fb.tokenValido();
    const r = await fetch(SERVIDOR + '/api/social/' + rota, { method: metodo, headers: { Authorization: 'Bearer ' + t, 'Content-Type': 'application/json' },
      body: corpo ? JSON.stringify(corpo) : undefined });
    const j = await r.json().catch(() => ({}));
    return { status: r.status, ...j };
  } catch (_) { return { status: 0, erro: 'sem internet' }; }
}
// O servidor faz presentes? Só guarda o "sim": antes um "não" (o servidor grátis do Render dormindo e demorando a
// acordar, internet caindo) ficava guardado até fechar o jogo e os presentes nunca mais funcionavam
async function presentesProntos() {
  if (chat.presentesLigados) return true;
  const e = await socialApi('GET', 'estado');
  if (e.presentes) { chat.presentesLigados = true; return true; }
  if (!e.status) janela('Presentes', 'Não consegui falar com o servidor do jogo (ele pode estar acordando: espere uns 30 segundos e tente de novo).');
  else janela('Presentes', 'O servidor do jogo está sem a chave de administrador do Firebase (FIREBASE_SERVICE_ACCOUNT no Render). Sem ela, os presentes ficam desligados.');
  return false;
}
// escolher o presente para um amigo: só skins pagas do mercado; mostra o que ele já tem
async function escolherPresente(p) {
  if (fb.anonimo) { janela('Entre na sua conta', 'Convidado não manda presentes. Entre com e-mail e senha.'); return; }
  if (!(await presentesProntos())) return;
  const el = html(`<div class="camada fixo"><div class="janela" style="width:min(640px,94%);max-height:88%">
    <div class="linha"><span class="t cresce">Presente para ${esc(p.nome)}</span><span class="linha" style="gap:4px;font-weight:800;color:#FCD34D">${ICONE.moeda(14)} ${fmt(conta.moedas)}</span>
      <button class="btn btn-fraco" id="pr-fechar" aria-label="Fechar">✕</button></div>
    <span class="dica">Você paga o preço da loja com as suas moedas e a skin vai para o seu amigo (não sai do seu inventário).</span>
    <div class="grade-presentes">${SKINS_ARMA.map((s) => `<button class="item-presente fundo-${s.raridade}" data-item="skin_arma:${s.arma}:${s.key}">
      <img src="armas/${s.arma}_${s.key}.png" alt=""><b>${esc(s.nome)}</b><span>${ICONE.moeda(12)} ${fmt(s.preco)}</span></button>`).join('')}</div>
  </div></div>`);
  palco.appendChild(el);
  $('#pr-fechar', el).onclick = () => el.remove();
  for (const b of $$('[data-item]', el)) b.onclick = async () => {
    b.disabled = true;
    if (await enviarPresente(p, b.dataset.item)) el.remove();
    b.disabled = false;
  };
}
// confirma e manda (o servidor cobra e entrega); true se foi
async function enviarPresente(p, item) {
  const s = presenteavel(item);
  if (!s) return false;
  if (conta.moedas < s.preco) { aviso(`Moedas insuficientes: precisa de ${fmt(s.preco)}`); return false; }
  const sim = await confirmar('Mandar presente?', `${s.nome} (${RARIDADES[s.raridade]}) para ${p.nome} por ${fmt(s.preco)} moedas.`, 'Mandar');
  if (!sim) return false;
  const chave = (crypto.randomUUID ? crypto.randomUUID() : Date.now().toString(36) + Math.random().toString(36).slice(2)).slice(0, 36);
  let r = await socialApi('POST', 'presente', { para: p.uid, item, chave });
  if (r.status === 0) r = await socialApi('POST', 'presente', { para: p.uid, item, chave });  // caiu a internet: a mesma chave não cobra duas vezes
  if (typeof r.moedas === 'number') conta.moedas = r.moedas;  // o saldo que o servidor deixou
  if (!r.ok) { aviso(r.erro || 'Não consegui mandar o presente'); return false; }
  janela('Presente enviado!', `${p.nome} recebeu ${s.nome}. Ele vai ver o aviso ao abrir o jogo.`);
  return true;
}
// mercado → ENVIAR: escolher o amigo que vai ganhar a skin
async function escolherAmigoParaPresente(skin) {
  if (!fb || !fb.token || fb.anonimo) { janela('Entre na sua conta', 'Para mandar presentes, entre com a sua conta.'); return; }
  if (!(await presentesProntos())) return;
  const amigos = social.pronto ? await meusAmigos() : [];
  if (!amigos.length) { janela('Mandar presente', 'Você ainda não tem amigos. Adicione pela tela Amigos (pelo ID ou pelo nick).'); return; }
  const item = `skin_arma:${skin.arma}:${skin.key}`;
  const el = html(`<div class="camada fixo"><div class="janela" style="min-width:340px;max-height:86%">
    <div class="linha"><span class="t cresce">🎁 ${esc(skin.nome)}</span><button class="btn btn-fraco" id="pa-fechar" aria-label="Fechar">✕</button></div>
    <span class="dica">Para qual amigo? Você paga ${fmt(skin.preco)} moedas.</span>
    <div class="coluna" style="gap:6px;overflow:auto">${amigos.map((p) => `<button class="btn btn-fraco" data-uid="${esc(p.uid)}" style="display:flex;justify-content:space-between">
      <span>${esc(p.nome)}</span><span class="dica">NV ${p.nivel || 1}</span></button>`).join('')}</div></div></div>`);
  palco.appendChild(el);
  $('#pa-fechar', el).onclick = () => el.remove();
  for (const b of $$('[data-uid]', el)) b.onclick = async () => {
    const p = amigos.find((x) => x.uid === b.dataset.uid);
    b.disabled = true;
    if (await enviarPresente(p, item)) el.remove();
    b.disabled = false;
  };
}
// abrir um presente recebido: a skin entra no inventário (e marca como recebido)
async function abrirPresente(id) {
  const p = chat.presentes[id];
  if (!p || p.estado !== 'pendente') return;
  if (!presenteavel(p.item)) return;
  conta.ganhar(p.item);
  const r = await fb.put(`presentes/${fb.uid}/${id}/estado`, 'recebido');
  if (!r.ok) aviso('A skin entrou, mas não consegui marcar o presente (sem internet?)');
  janela('Presente aberto!', `${p.nome} é sua, de ${p.deNome}. Equipe no Arsenal.`);
}
// histórico: recebidos (com o botão de abrir) e enviados
async function listaPresentesHtml() {
  const rec = Object.entries(chat.presentes).sort((a, b) => (b[1].em || 0) - (a[1].em || 0));
  const env = await fb.get('enviados/' + fb.uid);
  const envs = Object.values((env.ok && env.dado) || {}).sort((a, b) => (b.em || 0) - (a.em || 0));
  const data = (t) => (t ? new Date(Number(t)).toLocaleDateString('pt-BR') : '');
  return `<span class="dica" style="font-weight:800;letter-spacing:1px">RECEBIDOS</span>
    ${rec.map(([id, p]) => `<div class="linha amigo"><span class="cresce"><b>${esc(p.nome)}</b><span class="dica"> de ${esc(p.deNome)} · ${data(p.em)}</span></span>
      ${p.estado === 'pendente' ? `<button class="btn btn-ouro" data-abrir="${esc(id)}">Abrir</button>` : '<span class="dica">recebido</span>'}</div>`).join('') || '<span class="dica">Nenhum presente recebido.</span>'}
    <span class="dica" style="font-weight:800;letter-spacing:1px;margin-top:8px">ENVIADOS</span>
    ${envs.map((p) => `<div class="linha amigo"><span class="cresce"><b>${esc(p.nome)}</b><span class="dica"> para ${esc(p.paraNome)} · ${data(p.em)}</span></span><span class="dica">${ICONE.moeda(12)} ${fmt(p.preco)}</span></div>`).join('') || '<span class="dica">Nenhum presente enviado.</span>'}`;
}
