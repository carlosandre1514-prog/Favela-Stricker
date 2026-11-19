// Favela Strike — painel de métricas (Supabase, função painel_enviar).
//
// A chave do painel fica SÓ aqui no servidor, na variável de ambiente PAINEL_CHAVE (Render → Environment). Sem ela,
// nada é enviado e o jogo segue normal. O app e o site mandam os eventos deles para POST /api/painel; o servidor
// junta tudo numa fila e envia ao painel em lotes de até 200 a cada 2 s, sem nunca travar as partidas.
//
// Eventos que saem daqui (o servidor confere ou só ele sabe):
//   servidor       a cada 60 s: jogadores conectados agora e a versão
//   servidor_parou quando o Render desliga o servidor (atualização, reinício)
//   entrou         alguém conectou numa partida ao vivo (marca ONLINE)
//   presente       presente entregue (moedas do jogo, não reais)
//   transacao      compra em reais confirmada pela loja (o valor vem do pedido, não do celular)
//   erro           erro inesperado no próprio servidor (no máximo 20 por hora)
// Eventos do app/site aceitos em /api/painel: instalar, abrir, ping, saiu, cadastro, login, partida, erro, feedback,
//   compra_moedas (skin comprada com moedas do jogo, não reais) e nivel_subiu.
// Plataforma no formato do painel: Android, iOS, PC (navegador do computador).
// Nunca vão senha, e-mail, CPF ou cartão: o "usuario" é o uid do Firebase (ou um id do aparelho antes do login).
'use strict';

const URL_PAINEL = process.env.PAINEL_URL || 'https://pxtxcbqqbjtmsmwbcise.supabase.co/rest/v1/rpc/painel_enviar';
const APIKEY = process.env.PAINEL_APIKEY || 'sb_publishable_URNeHmQn-EuIyqXbkbMSag_gWTtX2rf';  // chave publicável do Supabase (pública)
const CHAVE = process.env.PAINEL_CHAVE || '';
const VERSAO = (process.env.RENDER_GIT_COMMIT || '').slice(0, 7) || require('./package.json').version;

const DO_APP = new Set(['instalar', 'abrir', 'ping', 'saiu', 'cadastro', 'login', 'partida', 'erro', 'feedback', 'compra_moedas', 'nivel_subiu']);
// o jogo usa android/pc/ios; o painel entende Android | iOS | Web | PC
const PLATAFORMAS = { android: 'Android', pc: 'PC', ios: 'iOS', web: 'Web' };
const plataforma = (p) => PLATAFORMAS[String(p || '').toLowerCase()] || '';
const fila = [];
let enviando = false, avisouChave = false;

const ligado = () => !!CHAVE;

// um evento na fila (cheia: sai o mais antigo)
function evento(tipo, usuario, dados = {}, extra = {}) {
  if (!CHAVE) return;
  if (fila.length >= 5000) fila.shift();
  const ev = { tipo, usuario: String(usuario || '').slice(0, 100), dados: dados && typeof dados === 'object' ? dados : {}, em: extra.em || new Date().toISOString() };
  if (extra.nome) ev.nome = String(extra.nome).slice(0, 60);
  if (plataforma(extra.plataforma)) ev.plataforma = plataforma(extra.plataforma);
  fila.push(ev);
}

// manda o próximo lote; erro do painel (fora do ar) devolve o lote para a fila; chave errada descarta e avisa no log
async function enviar() {
  if (!CHAVE || enviando || !fila.length) return;
  enviando = true;
  const lote = fila.splice(0, 200);
  try {
    const r = await fetch(URL_PAINEL, {
      method: 'POST', headers: { apikey: APIKEY, 'Content-Type': 'application/json' },
      body: JSON.stringify({ chave: CHAVE, eventos: lote }), signal: AbortSignal.timeout(8000),
    });
    if (r.status >= 500 || r.status === 429) fila.unshift(...lote.slice(0, 5000 - fila.length));
    else if (!r.ok && !avisouChave) { avisouChave = true; console.error('painel: recusado', r.status, (await r.text()).slice(0, 200)); }
  } catch (_) {
    fila.unshift(...lote.slice(0, 5000 - fila.length));  // sem rede: tenta no próximo
  } finally { enviando = false; }
}

// limpa um evento vindo do app/site (tipo da lista, campos curtos, dados pequenos, sem e-mail/senha)
function limpar(ev) {
  if (!ev || typeof ev !== 'object') return null;
  const tipo = String(ev.tipo || '').toLowerCase();
  if (!DO_APP.has(tipo)) return null;
  let dados = ev.dados && typeof ev.dados === 'object' && !Array.isArray(ev.dados) ? ev.dados : {};
  dados = Object.fromEntries(Object.entries(dados).filter(([k]) => !/mail|senha|password|cpf|cartao|token/i.test(k)).slice(0, 20)
    .map(([k, v]) => [k.slice(0, 30), typeof v === 'string' ? v.slice(0, tipo === 'feedback' ? 1000 : 300) : typeof v === 'number' || typeof v === 'boolean' ? v : String(v).slice(0, 100)]));
  const em = Date.parse(ev.em);
  return {
    tipo, usuario: String(ev.usuario || '').slice(0, 100), dados,
    nome: String(ev.nome || '').slice(0, 60), plataforma: plataforma(ev.plataforma),
    em: Number.isFinite(em) ? new Date(em).toISOString() : undefined,
  };
}

// POST /api/painel {eventos:[...]} — o app/site (também por sendBeacon, que manda como texto)
async function tratar(req, res, url, limite) {
  if (url.pathname !== '/api/painel') return false;
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
  res.setHeader('Access-Control-Allow-Methods', 'POST, OPTIONS');
  if (req.method === 'OPTIONS') { res.writeHead(204); res.end(); return true; }
  if (req.method !== 'POST') { res.writeHead(405); res.end(); return true; }
  const ip = String(req.headers['x-forwarded-for'] || req.socket.remoteAddress || '').split(',')[0].trim();
  if (!limite('painel:' + ip, 40)) { res.writeHead(429); res.end(); return true; }
  let s = '';
  req.on('data', (c) => { s += c; if (s.length > 64000) req.destroy(); });
  req.on('end', () => {
    try {
      const j = JSON.parse(s || '{}');
      for (const ev of (Array.isArray(j.eventos) ? j.eventos : []).slice(0, 60)) {
        const e = limpar(ev);
        if (e) evento(e.tipo, e.usuario, e.dados, e);
      }
    } catch (_) {}
    res.writeHead(204); res.end();
  });
  return true;
}

// sinal de vida do servidor e o "parou" ao desligar (Render manda SIGTERM ao atualizar)
function ligarServidor(contarOnline) {
  setInterval(() => enviar().catch(() => {}), 2000);
  if (!CHAVE) { console.log('painel: sem PAINEL_CHAVE, métricas desligadas'); return; }
  const sinal = () => evento('servidor', 'servidor', { online: contarOnline(), versao: VERSAO });
  sinal();
  setInterval(sinal, 60000);
  let saindo = false;
  for (const sig of ['SIGTERM', 'SIGINT']) {
    process.on(sig, async () => {
      if (saindo) return;
      saindo = true;
      evento('servidor_parou', 'servidor', {});
      const fim = Date.now() + 4000;
      while (fila.length && Date.now() < fim) { await enviar(); await new Promise((ok) => setTimeout(ok, 100)); }
      process.exit(0);
    });
  }
}

// erro inesperado do servidor (no máximo 20 por hora, para não inundar o painel se algo entrar em loop)
let errosHora = 0, horaErros = 0;
function erro(e, onde = 'servidor') {
  const h = Math.floor(Date.now() / 3600000);
  if (h !== horaErros) { horaErros = h; errosHora = 0; }
  if (++errosHora > 20) return;
  evento('erro', 'servidor', { mensagem: String((e && e.message) || e || 'erro').slice(0, 300), onde, versao: VERSAO });
}

module.exports = { evento, tratar, ligarServidor, ligado, limpar, erro, plataforma };
