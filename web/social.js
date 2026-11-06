// Favela Strike — parte social: ID único, nick único, amizades, quem está online, convites e o grupo (squad).
//
// No Firebase (Realtime Database; as regras estão no README):
//   ids/<ID de 9 dígitos>     = uid           cada número é de uma conta só (a regra só deixa gravar se estiver livre)
//   nicks/<nick normalizado>  = uid           cada nick é de uma conta só (maiúsculas e acentos não contam)
//   perfis/<uid>              = {nome, id, nivel, visto, jogando}   o que os outros veem (visto: último sinal de vida)
//   amigos/<uid>/<uid do amigo> = true        nos dois lados
//   pedidos/<uid>/<uid de quem pediu> = {nome, id, em}              caixa de pedidos de amizade recebidos
//   convites/<uid>/<uid de quem convidou> = {nome, lider, formato, em}   convite para o grupo
//   grupos/<uid do líder>     = {formato, membros: {uid: entrada}, sala}  o grupo (solo, duo, trio ou squad)
'use strict';

const FORMATOS = {
  solo: { nome: 'Solo', tam: 1 },
  duo: { nome: 'Duo', tam: 2 },
  trio: { nome: 'Trio', tam: 3 },
  squad: { nome: 'Squad', tam: 4 },
};
const ONLINE_MS = 150000;  // sinal de vida nos últimos 2,5 min: online

const social = {
  pronto: false,          // regras do Firebase ok e identidade feita
  erro: '',
  id: '',                 // ID de 9 dígitos
  pedidos: {},            // pedidos de amizade recebidos (uid -> {nome, id})
  convites: {},           // convites para grupo recebidos
  grupo: null,            // {lider, formato, membros, sala}
  ouvintes: [],
  aoMudar: null,          // a tela aberta se redesenha (badge, grupo)
};

// nick para a chave única: minúsculas, sem acento, só letras/números/_
function nickChave(n) {
  return String(n || '').normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase().replace(/[^a-z0-9_]/g, '').slice(0, 16);
}
// nick que aparece: letras (com acento), números, _ e espaço, de 3 a 16
function nickValido(n) {
  const t = String(n || '').trim();
  if (t.length < 3 || t.length > 16) return 'O nick precisa ter de 3 a 16 letras';
  if (!/^[\p{L}0-9_ ]+$/u.test(t)) return 'Use só letras, números, _ e espaço';
  if (nickChave(t).length < 3) return 'O nick precisa ter pelo menos 3 letras ou números';
  return '';
}
const idBonito = (id) => String(id || '').replace(/(\d{3})(\d{3})(\d{3})/, '$1 $2 $3');
const regrasFaltando = (code) => code === 401 || code === 403;
const avisoRegras = 'Amigos e ID: falta colar as regras novas no Firebase (Realtime Database → Regras, veja o README).';

// ---------------- identidade: ID único e nick único ----------------
async function garantirIdentidade() {
  if (!fb || !fb.token) return;
  try {
    const u = await fb.get('usuarios/' + fb.uid);
    let id = u.ok && u.dado && u.dado.id ? String(u.dado.id) : '';
    if (id) {  // confere se o número é mesmo desta conta
      const g = await fb.get('ids/' + id);
      if (regrasFaltando(g.code)) { social.erro = avisoRegras; return; }
      if (g.dado !== fb.uid) id = '';
    }
    for (let k = 0; !id && k < 10; k++) {  // sorteia um número livre (a regra do banco garante: nunca dois iguais)
      const cand = String(100000000 + Math.floor(Math.random() * 900000000));
      const g = await fb.get('ids/' + cand);
      if (regrasFaltando(g.code)) { social.erro = avisoRegras; return; }
      if (g.dado) continue;
      const r = await fb.put('ids/' + cand, fb.uid);
      if (r.ok) id = cand;
    }
    if (!id) { social.erro = 'Não consegui criar o seu ID (sem internet?)'; return; }
    social.id = id;
    guardado.set('meu_id', id);
    await fb.patch('usuarios/' + fb.uid, { id });
    // nick: o atual, se estiver livre (ou já for meu); senão, o mesmo com números no fim
    const atual = conta.nome();
    const meu = await fb.get('nicks/' + nickChave(atual));
    if (meu.dado !== fb.uid) {
      let ok = !nickValido(atual) && !(await trocarNick(atual, true));
      for (let k = 0; !ok && k < 6; k++) {
        const base = (nickValido(atual) ? 'Jogador' : atual.replace(/\d+$/, '').slice(0, 11)) || 'Jogador';
        ok = !(await trocarNick(base + Math.floor(1000 + Math.random() * 9000), true));
      }
    }
    social.pronto = true;
    social.erro = '';
    await sinalDeVida();
    ouvirSocial();
  } catch (e) { social.erro = 'Sem conexão com o banco: ' + e.message; }
}

// Troca o nick: reserva o novo (se ninguém tiver) e só então solta o antigo. Devolve '' ou o motivo.
async function trocarNick(novo, silencioso) {
  const t = String(novo || '').trim().replace(/\s+/g, ' ');
  const inval = nickValido(t);
  if (inval) return inval;
  const chave = nickChave(t);
  const antes = nickChave(conta.nome());
  const g = await fb.get('nicks/' + chave);
  if (regrasFaltando(g.code)) return avisoRegras;
  if (g.dado && g.dado !== fb.uid) return 'Esse nick já é de outro jogador. Mude alguma coisa (um número, uma letra).';
  if (!g.dado) {
    const r = await fb.put('nicks/' + chave, fb.uid);
    if (!r.ok) return 'Esse nick acabou de ser pego por outro jogador. Tente outro.';
  }
  if (antes && antes !== chave) {
    const v = await fb.get('nicks/' + antes);
    if (v.dado === fb.uid) await fb.del('nicks/' + antes);
  }
  guardado.set('nome', t);
  salvarConta();
  if (social.id) fb.patch('perfis/' + fb.uid, { nome: t }).catch(() => {});
  if (!silencioso) aviso('Nick salvo: ' + t);
  return '';
}

// a cada minuto com o jogo aberto: os amigos veem que estou online (e se estou numa partida)
async function sinalDeVida() {
  if (!social.id || !fb || !fb.token) return;
  const n = nivelDe(conta.xp);
  await fb.patch('perfis/' + fb.uid, { nome: conta.nome(), id: social.id, nivel: n.nivel, visto: { '.sv': 'timestamp' },
    jogando: typeof partida !== 'undefined' && partida ? (partida.modo === 1 ? 'Ressurgência' : 'Polícia x Bandidos') : '' }).catch(() => {});
}
setInterval(() => { if (!document.hidden) sinalDeVida(); }, 60000);

// pedidos de amizade e convites chegando ao vivo
function ouvirSocial() {
  for (const o of social.ouvintes) o.fechar();
  social.ouvintes = [];
  const juntar = (alvo) => (ev, sub, dado) => {
    if (sub === '/') { if (ev === 'put') for (const k of Object.keys(alvo)) delete alvo[k]; Object.assign(alvo, dado || {}); for (const [k, v] of Object.entries(dado || {})) if (!v) delete alvo[k]; }
    else { const k = sub.slice(1).split('/')[0]; if (dado) alvo[k] = Object.assign(alvo[k] || {}, dado); else delete alvo[k]; }
    if (social.aoMudar) social.aoMudar();
    atualizarBadgeAmigos();
  };
  social.ouvintes.push(fb.ouvir('pedidos/' + fb.uid, juntar(social.pedidos)));
  social.ouvintes.push(fb.ouvir('convites/' + fb.uid, (ev, sub, dado) => { juntar(social.convites)(ev, sub, dado); mostrarConvites(); }));
}
function atualizarBadgeAmigos() {
  const n = Object.keys(social.pedidos).length;
  for (const b of $$('.badge-amigos')) { b.textContent = n; b.style.display = n ? '' : 'none'; }
}

// ---------------- busca, pedidos e amizades ----------------
async function perfil(uid) {
  const r = await fb.get('perfis/' + uid);
  return r.ok && r.dado ? { uid, ...r.dado } : null;
}
const estaOnline = (p) => p && p.visto && Date.now() - Number(p.visto) < ONLINE_MS;

// por ID (9 números) ou pelo começo do nick
async function buscarJogadores(q) {
  q = String(q || '').trim();
  const soNumeros = q.replace(/\D/g, '');
  if (/^\d{9}$/.test(soNumeros) && soNumeros.length === q.replace(/\s/g, '').length) {
    const r = await fb.get('ids/' + soNumeros);
    if (regrasFaltando(r.code)) throw new Error(avisoRegras);
    const p = r.dado ? await perfil(r.dado) : null;
    return p ? [p] : [];
  }
  const k = nickChave(q);
  if (k.length < 2) return [];
  const url = (await fb.url('nicks')) + `&orderBy=${encodeURIComponent('"$key"')}&startAt=${encodeURIComponent(JSON.stringify(k))}&endAt=${encodeURIComponent(JSON.stringify(k + ''))}&limitToFirst=10`;
  const resp = await fetch(url);
  if (regrasFaltando(resp.status)) throw new Error(avisoRegras);
  const achados = (await resp.json()) || {};
  const ps = await Promise.all(Object.values(achados).map(perfil));
  return ps.filter(Boolean);
}
async function enviarPedido(uid) {
  if (uid === fb.uid) return 'Esse é você :)';
  const ja = await fb.get(`amigos/${fb.uid}/${uid}`);
  if (ja.dado) return 'Vocês já são amigos';
  const r = await fb.put(`pedidos/${uid}/${fb.uid}`, { nome: conta.nome(), id: social.id, em: { '.sv': 'timestamp' } });
  return r.ok ? '' : regrasFaltando(r.code) ? avisoRegras : 'Não consegui enviar (sem internet?)';
}
async function aceitarPedido(uid) {
  await fb.put(`amigos/${fb.uid}/${uid}`, true);
  await fb.put(`amigos/${uid}/${fb.uid}`, true);  // (a regra deixa porque existe o pedido dele para mim)
  await fb.del(`pedidos/${fb.uid}/${uid}`);
}
const recusarPedido = (uid) => fb.del(`pedidos/${fb.uid}/${uid}`);
async function desfazerAmizade(uid) {
  await fb.del(`amigos/${fb.uid}/${uid}`);
  await fb.del(`amigos/${uid}/${fb.uid}`);
}
async function meusAmigos() {
  const r = await fb.get('amigos/' + fb.uid);
  const ids = Object.keys((r.ok && r.dado) || {});
  const ps = (await Promise.all(ids.map(perfil))).filter(Boolean);
  return ps.sort((a, b) => (estaOnline(b) - estaOnline(a)) || String(a.nome).localeCompare(String(b.nome)));
}

// ---------------- grupo (squad) e convites ----------------
// o grupo é do líder: ele escolhe o formato, convida amigos e procura a partida para todos juntos
function entradaGrupo() { return { ...minhaEntrada(guardado.get('lado', 0)) }; }
async function criarGrupo(formato) {
  const g = { formato, membros: { [fb.uid]: { ...entradaGrupo(), lider: true } }, sala: '' };
  await fb.put('grupos/' + fb.uid, g);
  return entrarNoGrupoLocal(fb.uid);
}
async function convidarAmigo(uid) {
  if (!social.grupo || social.grupo.lider !== fb.uid) await criarGrupo(social.grupo ? social.grupo.formato : guardado.get('formato_br', 'squad'));
  const tam = FORMATOS[social.grupo.formato].tam;
  if (Object.keys(social.grupo.membros || {}).length >= tam) return `O grupo ${FORMATOS[social.grupo.formato].nome} já está cheio`;
  const r = await fb.put(`convites/${uid}/${fb.uid}`, { nome: conta.nome(), lider: fb.uid, formato: social.grupo.formato, em: { '.sv': 'timestamp' } });
  return r.ok ? '' : 'Não consegui convidar (sem internet?)';
}
async function aceitarConvite(deUid) {
  const c = social.convites[deUid];
  await fb.del(`convites/${fb.uid}/${deUid}`);
  if (!c) return 'O convite expirou';
  const g = await fb.get('grupos/' + c.lider);
  if (!g.ok || !g.dado) return 'O grupo não existe mais';
  const tam = (FORMATOS[g.dado.formato] || FORMATOS.squad).tam;
  if (Object.keys(g.dado.membros || {}).length >= tam) return 'O grupo já está cheio';
  if (social.grupo) await sairDoGrupo();
  await fb.put(`grupos/${c.lider}/membros/${fb.uid}`, entradaGrupo());
  entrarNoGrupoLocal(c.lider);
  return '';
}
// acompanha o grupo: membros, formato e a sala onde o líder colocou todo mundo
function entrarNoGrupoLocal(lider) {
  if (social.grupo && social.grupo.ouvinte) social.grupo.ouvinte.fechar();
  social.grupo = { lider, formato: 'squad', membros: {}, sala: '' };
  const g = social.grupo;
  g.ouvinte = fb.ouvir('grupos/' + lider, (ev, sub, dado) => {
    if (sub === '/') {
      if (!dado) { if (social.grupo === g) { social.grupo = null; g.ouvinte.fechar(); if (lider !== fb.uid) aviso('O grupo foi desfeito'); if (social.aoMudar) social.aoMudar(); } return; }
      if (ev === 'put') { g.formato = dado.formato || 'squad'; g.membros = dado.membros || {}; g.sala = dado.sala || ''; }
      else { if (dado.formato) g.formato = dado.formato; if (dado.membros) Object.assign(g.membros, dado.membros); if ('sala' in dado) g.sala = dado.sala || ''; }
    } else {
      const partes = sub.slice(1).split('/');
      if (partes[0] === 'membros') { if (partes[1]) { if (dado) g.membros[partes[1]] = Object.assign(g.membros[partes[1]] || {}, dado); else delete g.membros[partes[1]]; } else g.membros = dado || {}; }
      else if (partes[0] === 'formato') g.formato = dado || 'squad';
      else if (partes[0] === 'sala') g.sala = dado || '';
    }
    if (lider !== fb.uid && !g.membros[fb.uid] && ev === 'put') { social.grupo = null; g.ouvinte.fechar(); aviso('Você saiu do grupo'); }
    // o líder achou a partida: quem é do grupo entra na mesma sala
    if (g.sala && lider !== fb.uid && (!sala || sala.codigo !== g.sala) && !partida) entrarNaSalaDoGrupo(g.sala);
    if (social.aoMudar) social.aoMudar();
  });
  return g;
}
async function sairDoGrupo() {
  const g = social.grupo;
  if (!g) return;
  social.grupo = null;
  if (g.ouvinte) g.ouvinte.fechar();
  if (g.lider === fb.uid) await fb.del('grupos/' + fb.uid);
  else await fb.del(`grupos/${g.lider}/membros/${fb.uid}`);
}
async function mudarFormatoGrupo(formato) {
  guardado.set('formato_br', formato);
  if (social.grupo && social.grupo.lider === fb.uid) await fb.patch('grupos/' + fb.uid, { formato });
}
async function entrarNaSalaDoGrupo(codigo) {
  const [s, erro] = await Sala.entrar(codigo, { ...minhaEntrada(guardado.get('lado', 0)), grupo: social.grupo ? social.grupo.lider : '' });
  if (!s) { aviso('Não consegui entrar na partida do grupo: ' + erro); return; }
  if (sala && sala !== s) sairDaSala();
  sala = s;
  ligarSala(s);
  telaSala();
}

// convite chegando: janelinha com aceitar / recusar (some sozinho em 2 min)
let convitesMostrados = new Set();
function mostrarConvites() {
  for (const [de, c] of Object.entries(social.convites)) {
    if (convitesMostrados.has(de + (c.em || ''))) continue;
    if (c.em && Date.now() - Number(c.em) > 120000) { fb.del(`convites/${fb.uid}/${de}`); continue; }
    convitesMostrados.add(de + (c.em || ''));
    if (typeof partida !== 'undefined' && partida) continue;  // em partida: fica para depois (aparece na tela de amigos)
    janela('Convite para jogar', `${c.nome} chamou você para o grupo de Ressurgência (${(FORMATOS[c.formato] || FORMATOS.squad).nome}).`,
      [{ texto: 'Recusar', classe: 'btn-fraco', valor: false }, { texto: 'Entrar no grupo', classe: 'btn-ouro', valor: true }]).then(async (sim) => {
      if (!sim) { fb.del(`convites/${fb.uid}/${de}`); return; }
      const erro = await aceitarConvite(de);
      if (erro) aviso(erro); else { aviso('Você entrou no grupo de ' + c.nome); telaRessurgencia(); }
    });
  }
}
